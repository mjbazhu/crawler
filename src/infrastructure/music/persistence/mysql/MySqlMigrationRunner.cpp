#include "crawler/infrastructure/music/persistence/mysql/MySqlMigrationRunner.h"

#include "MySqlMigrationRecovery.h"

#include "crawler/infrastructure/music/persistence/mysql/MySqlValueReader.h"
#include "crawler/infrastructure/music/persistence/sql/SqlFileLoader.h"

#include <boost/asio/redirect_error.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/mysql/error_code.hpp>
#include <boost/mysql/results.hpp>

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <map>
#include <optional>
#include <regex>
#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {

namespace {

namespace sql_loader = infrastructure::music::persistence::sql;

// 单个 migration 文件描述。
struct MigrationFile {
    std::uint64_t version = 0; // 版本号，例如 2026052101
    std::string name;          // 文件名中的说明部分
    std::string path;          // 可加载路径
    std::string checksum;      // 文件内容 checksum
    std::string script;        // SQL 脚本
};

// 计算稳定 32 字符 checksum，用于发现已执行 migration 被修改。
std::string checksumOf(const std::string &value)
{
    auto fnv = [](const std::string &input, std::uint64_t seed) {
        std::uint64_t hash = 1469598103934665603ULL ^ seed;
        for (unsigned char ch : input) {
            hash ^= ch;
            hash *= 1099511628211ULL;
        }
        return hash;
    };

    std::ostringstream out;
    out << std::hex << std::setfill('0') << std::setw(16) << fnv(value, 0x9e3779b97f4a7c15ULL)
        << std::setw(16) << fnv(value, 0xbf58476d1ce4e5b9ULL);
    return out.str();
}

// 从文件名解析 migration 版本和名称。
std::optional<std::pair<std::uint64_t, std::string>> parseMigrationName(const std::filesystem::path &path)
{
    static const std::regex pattern(R"(^V([0-9]+)__(.+)\.sql$)");
    std::smatch match;
    const std::string filename = path.filename().string();
    if (!std::regex_match(filename, match, pattern)) {
        return std::nullopt;
    }
    return std::make_pair(static_cast<std::uint64_t>(std::stoull(match[1].str())), match[2].str());
}

// 读取 migration 目录下所有合法版本文件。
std::vector<MigrationFile> loadMigrations(const std::string &migrations_path)
{
    const auto resolved = sql_loader::SqlFileLoader::resolvePath(migrations_path);
    if (!resolved || !std::filesystem::is_directory(*resolved)) {
        throw std::runtime_error("[MySqlMigrationRunner] migrations directory not found: " + migrations_path);
    }

    std::vector<MigrationFile> migrations;
    for (const auto &entry : std::filesystem::directory_iterator(*resolved)) {
        if (!entry.is_regular_file()) {
            continue;
        }
        const auto parsed = parseMigrationName(entry.path());
        if (!parsed) {
            continue;
        }
        const std::string path = entry.path().string();
        const std::string script = sql_loader::SqlFileLoader::loadOrThrow(path);
        migrations.push_back({parsed->first, parsed->second, path, checksumOf(script), script});
    }

    std::sort(migrations.begin(), migrations.end(), [](const MigrationFile &left, const MigrationFile &right) {
        return left.version < right.version;
    });
    return migrations;
}

// 创建 migration 元数据表。
boost::asio::awaitable<void> ensureMigrationTable(boost::mysql::pooled_connection &connection)
{
    boost::mysql::results result;
    co_await connection->async_execute(
        sql_loader::SqlFileLoader::loadOrThrow("sql/mysql/schema_migrations/create.sql"),
        result,
        boost::asio::use_awaitable
    );
}

// 获取 migration 分布式执行锁。
boost::asio::awaitable<bool> acquireMigrationLock(boost::mysql::pooled_connection &connection)
{
    boost::mysql::results result;
    co_await connection->async_execute("SELECT GET_LOCK('crawler_mysql_migrations', 30)", result, boost::asio::use_awaitable);
    if (result.rows().empty()) {
        co_return false;
    }
    co_return MySqlValueReader::intAt(result.rows().front(), 0) == 1;
}

// 释放 migration 执行锁。
boost::asio::awaitable<void> releaseMigrationLock(boost::mysql::pooled_connection &connection)
{
    boost::mysql::results result;
    boost::mysql::error_code ignored_error;
    co_await connection->async_execute("SELECT RELEASE_LOCK('crawler_mysql_migrations')", result, boost::asio::redirect_error(boost::asio::use_awaitable, ignored_error));
}

// 读取已执行 migration 的 checksum。
boost::asio::awaitable<std::map<std::uint64_t, std::string>> loadAppliedMigrations(boost::mysql::pooled_connection &connection)
{
    boost::mysql::results result;
    co_await connection->async_execute(sql_loader::SqlFileLoader::loadOrThrow("sql/mysql/schema_migrations/list_applied.sql"), result, boost::asio::use_awaitable);
    std::map<std::uint64_t, std::string> applied;
    for (boost::mysql::row_view row : result.rows()) {
        applied[MySqlValueReader::uint64At(row, 0)] = MySqlValueReader::stringAt(row, 1);
    }
    co_return applied;
}

// 写入单个 migration 执行记录。
boost::asio::awaitable<void> recordMigration(boost::mysql::pooled_connection &connection, const MigrationFile &migration, std::uint64_t execution_ms)
{
    boost::mysql::results result;
    auto statement = co_await connection->async_prepare_statement(
        sql_loader::SqlFileLoader::loadOrThrow("sql/mysql/schema_migrations/insert_applied.sql"),
        boost::asio::use_awaitable
    );
    co_await connection->async_execute(statement.bind(migration.version, migration.name, migration.checksum, execution_ms), result, boost::asio::use_awaitable);
}

// 执行一个 migration 文件中的所有语句。
boost::asio::awaitable<void> executeMigration(boost::mysql::pooled_connection &connection, const MigrationFile &migration)
{
    const auto statements = sql_loader::SqlFileLoader::splitMigrationStatements(migration.script);
    if (statements.empty()) {
        throw std::runtime_error("[MySqlMigrationRunner] migration has no executable statements: " + migration.path);
    }
    boost::mysql::results result;
    for (const auto &statement : statements) {
        co_await connection->async_execute(statement, result, boost::asio::use_awaitable);
    }
}

} // namespace

boost::asio::awaitable<bool> MySqlMigrationRunner::coRun(
    boost::mysql::pooled_connection &connection,
    std::string migrations_path
)
{
    bool locked = false;
    try {
        const auto migrations = loadMigrations(migrations_path);
        if (migrations.empty()) {
            throw std::runtime_error("[MySqlMigrationRunner] no migration files found: " + migrations_path);
        }

        locked = co_await acquireMigrationLock(connection);
        if (!locked) {
            throw std::runtime_error("[MySqlMigrationRunner] failed to acquire migration lock");
        }

        co_await ensureMigrationTable(connection);
        auto applied = co_await loadAppliedMigrations(connection);
        if (!applied.empty() && applied.rbegin()->first > migrations.back().version) {
            throw std::runtime_error("[MySqlMigrationRunner] database has migrations newer than available files");
        }

        for (const auto &migration : migrations) {
            const auto found = applied.find(migration.version);
            if (found != applied.end()) {
                if (found->second != migration.checksum) {
                    throw std::runtime_error("[MySqlMigrationRunner] migration checksum mismatch: V" + std::to_string(migration.version));
                }
                continue;
            }

            const auto started = std::chrono::steady_clock::now();
            const bool reconciled = co_await migration_recovery::reconcileCompletedDdl(connection, migration.version, migration.checksum);
            if (!reconciled) co_await executeMigration(connection, migration);
            const auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started).count();
            co_await recordMigration(connection, migration, static_cast<std::uint64_t>((std::max<long long>)(0, elapsed)));
            std::cout << "[MySqlMigrationRunner] " << (reconciled ? "reconciled V" : "applied V") << migration.version << "__" << migration.name << std::endl;
        }

        co_await migration_recovery::synchronizeComponent(connection, migrations.back().version, migrations.back().name);

        co_await releaseMigrationLock(connection);
        co_return true;
    } catch (const std::exception &e) {
        std::cerr << "[MySqlMigrationRunner] migration failed: " << e.what() << std::endl;
    }

    if (locked) {
        co_await releaseMigrationLock(connection);
    }
    co_return false;
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
