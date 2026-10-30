#include "ResourceSchemaCli.h"

#include "crawler/infrastructure/music/persistence/mysql/MySqlConnectionPool.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMigrationRunner.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMusicCatalogQueryRepository.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlValueReader.h"
#include "crawler/infrastructure/music/persistence/sql/SqlFileLoader.h"
#include "crawler/infrastructure/platform/Environment.h"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/mysql/results.hpp>
#include <nlohmann/json.hpp>
#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <future>
#include <initializer_list>
#include <iostream>
#include <memory>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <system_error>
#include <utility>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::app {
namespace {
namespace mysql = music::persistence::mysql;
namespace loader = music::persistence::sql;
namespace env = platform;
using Json = nlohmann::json;
using Options = std::map<std::string, std::string>;

// 必填字段只产生固定诊断，认证和原始输入不进入错误输出。
const std::string &required(const Options &options, const char *key)
{
    const auto found = options.find(key);
    if (found == options.end() || found->second.empty()) throw std::invalid_argument("missing schema option");
    return found->second;
}

// 整数必须完整解析；连接端口和等待预算都不接受静默默认回退。
unsigned bounded(std::string_view text, unsigned maximum)
{
    unsigned value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || parsed.ec != std::errc {} || parsed.ptr != text.data() + text.size() || value == 0 || value > maximum) throw std::invalid_argument("invalid schema numeric option");
    return value;
}

// 命令参数在建立连接前检查，迁移必须绑定从只读检查取得的实际实例UUID。
void validate(std::string_view command, const Options &options)
{
    const std::set<std::string> allowed {"--database", "--expected-server-uuid", "--timeout-seconds"};
    for (const auto &[key, value] : options)
        if (!allowed.contains(key) || value.empty()) throw std::invalid_argument("unsupported schema option");
    const auto &database = required(options, "--database");
    auto normalized = database;
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char value) { return static_cast<char>(value >= 'A' && value <= 'Z' ? value - 'A' + 'a' : value); });
    if (!std::regex_match(database, std::regex("[A-Za-z0-9_]{1,64}")) || normalized == "mysql" || normalized == "sys" || normalized == "information_schema" || normalized == "performance_schema") throw std::invalid_argument("invalid schema database");
    if (command == "schema-migrate") (void)required(options, "--expected-server-uuid");
    if (options.contains("--expected-server-uuid") && !std::regex_match(options.at("--expected-server-uuid"), std::regex("[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}"))) throw std::invalid_argument("invalid expected server uuid");
    if (options.contains("--timeout-seconds")) (void)bounded(options.at("--timeout-seconds"), 3600);
}

// 只选择显式现有库；schema迁移由独立命令执行，建池本身不会初始化。
mysql::MySqlConfig configuration(const Options &options)
{
    const auto file = env::Environment::getString("CRAWLER_ENV_FILE");
    env::Environment::loadDotEnvFile(file ? *file : ".env");
    mysql::MySqlConfig config;
    config.database = env::Environment::getStringOr("CRAWLER_MYSQL_DATABASE", "");
    if (config.database != required(options, "--database")) throw std::invalid_argument("explicit schema database does not match configuration");
    config.host = env::Environment::getStringOr("CRAWLER_MYSQL_HOST", "");
    config.username = env::Environment::getStringOr("CRAWLER_MYSQL_USER", "");
    if (config.host.empty() || config.username.empty()) throw std::invalid_argument("explicit schema connection required");
    config.port = static_cast<unsigned short>(bounded(env::Environment::getStringOr("CRAWLER_MYSQL_PORT", ""), 65535));
    config.password = env::Environment::getStringOr("CRAWLER_MYSQL_PASSWORD", "");
    config.migrations_path = env::Environment::getStringOr("CRAWLER_MYSQL_MIGRATIONS_PATH", config.migrations_path);
    const auto path = loader::SqlFileLoader::resolvePath(config.migrations_path);
    if (!path || !std::filesystem::is_directory(*path)) throw std::invalid_argument("existing schema migrations directory required");
    config.migrations_path = std::filesystem::absolute(*path).string();
    config.initialize_schema = false;
    config.connect_timeout = std::chrono::seconds(5);
    config.io_threads = 2;
    config.max_pool_size = 2;
    return config;
}

// 组件版本不能停在旧值；完整状态必须同时满足当前组件、ledger和源码版本。
Json statusJson(const application::model::MiguSchemaStatus &status)
{
    const bool versions_match = status.schema_version == status.latest_applied_version && status.latest_applied_version == status.latest_available_version && status.latest_available_version != 0;
    return {{"ok", status.ok && versions_match}, {"schema_version", status.schema_version}, {"latest_applied_version", status.latest_applied_version}, {"latest_available_version", status.latest_available_version}, {"component_version_matches_ledger", versions_match}, {"pending_migrations", status.pending_migrations}, {"checksum_mismatches", status.checksum_mismatches}, {"missing_tables", status.missing_tables}, {"missing_columns", status.missing_columns}, {"missing_constraints", status.missing_constraints}};
}

// 只准备和关闭运行期模板；既不绑定资源参数，也不执行其中的DML。
boost::asio::awaitable<Json> checkSql(boost::mysql::pooled_connection &connection, const std::filesystem::path &root)
{
    std::vector<std::filesystem::path> paths;
    for (const auto &entry : std::filesystem::recursive_directory_iterator(root)) {
        const auto &path = entry.path();
        if (!entry.is_regular_file() || path.extension() != ".sql" || path.parent_path().filename() == "migrations" || path.filename() == "create.sql" || path.filename() == "catalog_view.sql" || path.parent_path().filename() == "transactions") continue;
        if (entry.is_symlink() || entry.file_size() > 2 * 1024 * 1024 || paths.size() >= 4096) throw std::invalid_argument("runtime sql inputs exceed fixed bounds");
        paths.push_back(path);
    }
    std::sort(paths.begin(), paths.end());
    if (paths.empty()) throw std::invalid_argument("runtime sql directory is empty");
    std::uint64_t prepared = 0;
    auto failures = Json::array();
    for (const auto &path : paths) {
        try {
            auto text = loader::SqlFileLoader::loadOrThrow(path.string());
            for (const std::string token : {"/*__CLOUD_CURRENT__*/", "/*__CLOUD_STATUS__*/"})
                for (auto offset = text.find(token); offset != std::string::npos; offset = text.find(token)) text.replace(offset, token.size(), "0");
            for (const auto &statement : loader::SqlFileLoader::splitStatements(text)) {
                auto handle = co_await connection->async_prepare_statement(statement, boost::asio::use_awaitable);
                co_await connection->async_close_statement(handle, boost::asio::use_awaitable);
                ++prepared;
            }
        } catch (const std::exception &) {
            failures.push_back(path.lexically_relative(root).generic_string());
        }
    }
    co_return Json {{"prepared_statements", prepared}, {"checked_files", paths.size()}, {"failed_files", failures}, {"prepared_sql_executed", false}, {"ok", failures.empty()}};
}

// 只有schema-migrate进入正式Runner；另两命令只读元数据或prepare。
boost::asio::awaitable<Json> work(std::shared_ptr<mysql::MySqlConnectionPool> pool, const mysql::MySqlConfig config, std::string command, Options options)
{
    auto connection = co_await pool->coAcquire();
    boost::mysql::results identity;
    co_await connection->async_execute("SELECT DATABASE(),@@server_uuid", identity, boost::asio::use_awaitable);
    if (identity.rows().size() != 1 || mysql::MySqlValueReader::stringAt(identity.rows().front(), 0) != config.database) throw std::invalid_argument("selected schema database mismatch");
    const auto uuid = mysql::MySqlValueReader::stringAt(identity.rows().front(), 1);
    if (options.contains("--expected-server-uuid") && uuid != options.at("--expected-server-uuid")) throw std::invalid_argument("selected schema server uuid mismatch");
    Json report {{"database", config.database}, {"server_uuid", uuid}, {"migration_attempted", false}, {"http_started", false}, {"workers_started", false}, {"baseline_activated", false}, {"content_requests", 0}, {"uploads", 0}};
    if (command == "schema-migrate") {
        const bool complete = co_await mysql::MySqlMigrationRunner::coRun(connection, config.migrations_path);
        report["migration_attempted"] = true;
        report["migration_completed"] = complete;
        if (!complete) report["ok"] = false;
    } else if (command == "schema-check-sql") {
        report["sql_check"] = co_await checkSql(connection, std::filesystem::path(config.migrations_path).parent_path());
    }
    co_return report;
}
} // namespace

int resourceSchemaCommand(std::string_view command, const std::map<std::string, std::string> &options)
{
    validate(command, options);
    const auto config = configuration(options);
    const auto timeout = options.contains("--timeout-seconds") ? bounded(options.at("--timeout-seconds"), 3600) : 900;
    auto pool = std::make_shared<mysql::MySqlConnectionPool>(config);
    auto pending = boost::asio::co_spawn(pool->executor(), work(pool, config, std::string(command), options), boost::asio::use_future);
    if (pending.wait_for(std::chrono::seconds(timeout)) != std::future_status::ready) {
        pool->cancel();
        throw std::runtime_error("schema operation deadline exceeded; database progress must be inspected before retry");
    }
    auto report = pending.get();
    const auto status = statusJson(mysql::MySqlMusicCatalogQueryRepository(pool).schemaStatus());
    report["schema_status"] = status;
    report["ok"] = status.at("ok").get<bool>() && report.value("migration_completed", true) && (!report.contains("sql_check") || report.at("sql_check").at("ok").get<bool>());
    std::cout << report.dump() << '\n';
    return report.at("ok").get<bool>() ? 0 : 2;
}
} // namespace orchestrator::modules::crawler::infrastructure::app
