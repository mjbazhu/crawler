#include "crawler/infrastructure/music/persistence/mysql/MySqlLegacyResourceImport.h"
#include "ResourceBaselineCli.h"
#include "ResourceTargetProbeCli.h"
#include "ResourceRecoveryCli.h"
#include "ResourceArchiveCli.h"
#include "ResourceHistoryCli.h"
#include "ResourceCloudCleanupCli.h"
#include "ResourceSchemaCli.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMusicCatalogQueryRepository.h"
#include "crawler/infrastructure/platform/Environment.h"
#include "crawler/infrastructure/storage/filesystem/ResourceCloudImportManifest.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cstdint>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iostream>
#include <map>
#include <memory>
#include <random>
#include <stdexcept>
#include <string>
#include <string_view>

#include <nlohmann/json.hpp>

namespace c = orchestrator::modules::crawler;
namespace model = c::application::model;
namespace mysql = c::infrastructure::music::persistence::mysql;
namespace files = c::infrastructure::storage::filesystem;
namespace env = c::infrastructure::platform;

namespace {
// 维护选项只有显式名单；认证和连接密码不通过命令行接受。
using Options = std::map<std::string, std::string>;

// 解析不重复的键值选项。
Options options(int argc, char **argv)
{
    Options result;
    for (int i = 2; i < argc; ++i) {
        const std::string key(argv[i]);
        if (!key.starts_with("--") || result.contains(key)) throw std::invalid_argument("invalid or duplicated option");
        if (key == "--attest-both" || key == "--exclude-all" || key == "--writers-stopped")
            result[key] = "true";
        else {
            if (++i >= argc) throw std::invalid_argument("option value is missing");
            result[key] = argv[i];
        }
    }
    return result;
}

// 读取必填选项，不回显可能敏感的输入值。
const std::string &required(const Options &value, const char *name)
{
    const auto found = value.find(name);
    if (found == value.end() || found->second.empty()) throw std::invalid_argument("required option is missing");
    return found->second;
}

// 按动作拒绝不相关参数，防止看似生效的错误配置。
void allowed(const Options &value, std::initializer_list<std::string_view> names)
{
    for (const auto &[key, ignored] : value)
        if (std::find(names.begin(), names.end(), key) == names.end()) throw std::invalid_argument("unsupported option for command");
}

// 无符号整数不能溢出或接受未解析后缀。
std::uint64_t number(std::string_view value)
{
    std::uint64_t result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc {} || parsed.ptr != value.data() + value.size() || value.empty()) throw std::invalid_argument("invalid unsigned option");
    return result;
}

// 只接受完整 SHA-256，不接受算法前缀或大小指纹。
std::string sha256(const std::string &value)
{
    const auto parsed = model::parseLegacyResourceChecksum(value);
    if (value.size() != 64 || parsed.sha256.empty()) throw std::invalid_argument("expected a complete SHA-256");
    return parsed.sha256;
}

// 仅读取现有私有配置，目标库必须由参数和环境双重一致选择，永不初始化迁移。
std::shared_ptr<mysql::MySqlConnectionPool> pool(const Options &args)
{
    const auto env_file = env::Environment::getString("CRAWLER_ENV_FILE");
    env::Environment::loadDotEnvFile(env_file ? *env_file : ".env");
    mysql::MySqlConfig config;
    config.database = env::Environment::getStringOr("CRAWLER_MYSQL_DATABASE", "");
    if (config.database != required(args, "--database")) throw std::invalid_argument("explicit database does not match selected private configuration");
    config.host = env::Environment::getStringOr("CRAWLER_MYSQL_HOST", config.host);
    config.port = env::Environment::getPortOr("CRAWLER_MYSQL_PORT", config.port);
    config.username = env::Environment::getStringOr("CRAWLER_MYSQL_USER", config.username);
    config.password = env::Environment::getStringOr("CRAWLER_MYSQL_PASSWORD", config.password);
    config.initialize_schema = false;
    config.io_threads = 2;
    config.max_pool_size = 4;
    auto result = std::make_shared<mysql::MySqlConnectionPool>(std::move(config));
    mysql::MySqlMusicCatalogQueryRepository catalog(result);
    if (!catalog.schemaStatus().ok) throw std::invalid_argument("expanded schema or identity constraints are not ready");
    return result;
}

// 私有副本在校验后保持不变，源清单被编辑也不影响当前执行；只删除本次成功创建的副本。
class ManifestSnapshot {
public:
    // 原文件必须是普通文件；copy_file 默认拒绝覆盖已有目标。
    explicit ManifestSnapshot(const std::filesystem::path &source)
    {
        if (!std::filesystem::is_regular_file(source)) throw std::invalid_argument("manifest must be a regular file");
        path_ = source;
        path_ += ".apply." + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "." + std::to_string(std::random_device {}());
        owned_ = std::filesystem::copy_file(source, path_);
        if (!owned_) throw std::runtime_error("private manifest snapshot could not be created");
    }
    // 清理精确临时文件，不递归删除，也不触碰原清单。
    ~ManifestSnapshot()
    {
        if (owned_) {
            std::error_code ignored;
            std::filesystem::remove(path_, ignored);
        }
    }
    // 快照只由当前实例管理。
    ManifestSnapshot(const ManifestSnapshot &) = delete;
    ManifestSnapshot &operator=(const ManifestSnapshot &) = delete;
    // 暴露私有只读路径给摘要与有界读取器。
    const std::filesystem::path &path() const
    {
        return path_;
    }

private:
    std::filesystem::path path_; // 本次私有副本
    bool owned_ = false;         // 仅清理本次创建成功的文件
};

// 输出分批预览清单，不写回执/策略，不执行远端或内容读取。
int preview(const Options &args)
{
    allowed(args, {"--database", "--owner", "--output", "--run-id", "--page-size", "--attest-both", "--exclude-all", "--legacy-rclone-provider"});
    const auto owner = model::parseResourceOwner(required(args, "--owner"));
    if (!owner) throw std::invalid_argument("unsupported resource owner");
    const auto limit = args.contains("--page-size") ? number(args.at("--page-size")) : 500;
    if (!limit || limit > 5000) throw std::invalid_argument("page size must be within 1..5000");
    files::ResourceCloudManifestHeader header;
    header.plan.run_id = required(args, "--run-id");
    if (header.plan.run_id.size() > 96) throw std::invalid_argument("run id exceeds its bound");
    header.plan.operator_attested_both = args.contains("--attest-both");
    header.plan.operator_excluded_all = args.contains("--exclude-all");
    if (header.plan.operator_attested_both && header.plan.operator_excluded_all) throw std::invalid_argument("attestation and exclusion are mutually exclusive");
    if (args.contains("--legacy-rclone-provider")) {
        header.plan.legacy_rclone_provider = model::parseCloudProvider(args.at("--legacy-rclone-provider"));
        if (!header.plan.legacy_rclone_provider) throw std::invalid_argument("rclone provider is invalid");
    }
    const std::filesystem::path output(required(args, "--output"));
    auto temporary = output;
    temporary += ".part";
    if (std::filesystem::exists(output) || std::filesystem::exists(temporary)) throw std::invalid_argument("manifest output or partial file already exists");
    auto connection = pool(args);
    mysql::MySqlLegacyResourceImport importer(connection);
    auto first = importer.preview(*owner, 0, 0, static_cast<std::size_t>(limit));
    if (!first.isSuccess()) throw std::invalid_argument(first.message());
    header.scope = *first.data();
    header.scope.rows.clear();
    std::ofstream stream(temporary, std::ios::binary);
    if (!stream) throw std::runtime_error("manifest output cannot be created");
    files::writeResourceCloudManifestHeader(stream, header);
    auto page = *first.data();
    std::uint64_t count = 0, eligible = 0;
    for (;;) {
        if (page.database_identity != header.scope.database_identity || page.high_water_id != header.scope.high_water_id) throw std::invalid_argument("preview scope changed");
        for (std::size_t i = 0; i < 3; ++i)
            if (page.targets[i].revision != header.scope.targets[i].revision || page.targets[i].fingerprint != header.scope.targets[i].fingerprint) throw std::invalid_argument("preview target changed");
        files::writeResourceCloudManifestPage(stream, page);
        count += page.rows.size();
        for (const auto &row : page.rows)
            if (row.status == 2 && !header.plan.operator_excluded_all) ++eligible;
        if (page.next_id == page.high_water_id) break;
        if (page.next_id <= page.after_id) throw std::invalid_argument("preview cursor did not advance");
        auto next = importer.preview(*owner, page.next_id, page.high_water_id, static_cast<std::size_t>(limit));
        if (!next.isSuccess()) throw std::invalid_argument(next.message());
        page = *next.data();
    }
    stream.close();
    if (!stream) throw std::runtime_error("manifest output flush failed");
    if (std::filesystem::exists(output)) throw std::invalid_argument("manifest destination appeared during export");
    std::filesystem::rename(temporary, output);
    std::cout << nlohmann::json {{"manifest_sha256", files::resourceCloudManifestSha256(output)}, {"resources", count}, {"eligible_completed", eligible}, {"attested", header.plan.operator_attested_both}, {"content_requests", 0}, {"uploads", 0}}.dump() << '\n';
    return 0;
}

// 先校验完整私有快照，再连接明确目标库，顺序处理尚未提交的有界页。
int applyOrStatus(const Options &args, bool apply)
{
    allowed(args, {"--database", "--manifest", "--sha256"});
    ManifestSnapshot copy(required(args, "--manifest"));
    const auto digest = files::resourceCloudManifestSha256(copy.path());
    if (digest != sha256(required(args, "--sha256"))) throw std::invalid_argument("reviewed manifest checksum does not match");
    std::ifstream input(copy.path(), std::ios::binary);
    const auto header = files::readResourceCloudManifestHeader(input, digest);
    if (apply && header.plan.operator_attested_both == header.plan.operator_excluded_all) throw std::invalid_argument("manifest requires one explicit attestation or exclusion");
    auto connection = pool(args);
    mysql::MySqlLegacyResourceImport importer(connection);
    auto status = importer.progress(header.plan, header.scope);
    if (!status.isSuccess()) throw std::invalid_argument(status.message());
    if (apply && (status.data()->control_state != "active" || status.data()->state == "cancelled")) throw std::invalid_argument("import control is not active");
    const auto execution_generation = status.data()->control_generation;
    // 同摘要清单已经完整提交时只读进度；私有副本摘要及库/范围身份仍已核对。
    if (apply && !(status.data()->state == "completed" && status.data()->cursor == header.scope.high_water_id)) {
        std::uint64_t file_cursor = 0;
        std::size_t pages = 0;
        while (auto page = files::readResourceCloudManifestPage(input, header)) {
            if (page->after_id != file_cursor || page->next_id < file_cursor || page->next_id > header.scope.high_water_id || (header.scope.high_water_id && page->next_id == file_cursor) || (pages && file_cursor == header.scope.high_water_id)) throw std::invalid_argument("manifest pages are not contiguous");
            if (page->next_id > status.data()->cursor || (pages == 0 && header.scope.high_water_id == 0)) {
                auto plan = header.plan;
                plan.control_generation = execution_generation;
                const auto result = importer.importPage(plan, *page);
                if (!result.isSuccess()) throw std::invalid_argument(result.message());
                status = importer.progress(header.plan, header.scope);
                if (!status.isSuccess()) throw std::invalid_argument(status.message());
                if (status.data()->control_state != "active" || status.data()->control_generation != execution_generation) throw std::invalid_argument("import control changed");
            }
            file_cursor = page->next_id;
            ++pages;
        }
        if (!pages || file_cursor != header.scope.high_water_id) throw std::invalid_argument("manifest ended before its fixed high-water mark");
        status = importer.progress(header.plan, header.scope);
        if (!status.isSuccess()) throw std::invalid_argument(status.message());
    }
    const auto &value = *status.data();
    std::cout << nlohmann::json {{"state", value.state}, {"control_state", value.control_state}, {"control_generation", value.control_generation}, {"cursor", value.cursor}, {"processed_resources", value.processed_resources}, {"imported_resources", value.imported_resources}, {"conflicts", value.conflicts}, {"baseline_activated", false}, {"content_requests", 0}, {"uploads", 0}}.dump() << '\n';
    return value.conflicts ? 2 : 0;
}
} // namespace

// 独立维护 CLI；只有显式schema-migrate执行迁移，不启动HTTP或后台worker。
int main(int argc, char **argv)
{
    try {
        if (argc < 2 || std::string_view(argv[1]) == "--help") {
            std::cout << "crawler_cloud_receipts schema-status|schema-migrate|schema-check-sql --database <explicit-name> [--expected-server-uuid <uuid>] [--timeout-seconds <1..3600>] (schema-migrate requires expected uuid)\n";
            std::cout << "crawler_cloud_receipts target-probe --destination <local|baidu|gdrive>\ncrawler_cloud_receipts preview|apply|status|configure-target|baseline-review|baseline-seal|baseline-activate|baseline-maintenance|recover-tasks|history-preview|history-apply|history-status|history-control|import-control|cleanup-capture|cleanup-preview|cleanup-status|queue-status|queue-limits|discover-start|discover-run|discover-status|discover-pause|discover-resume|discover-cancel|discover-export --database <explicit-name> [command options]\n";
            return argc < 2 ? 1 : 0;
        }
        const std::string_view command(argv[1]);
        const auto args = options(argc, argv);
        if (command == "schema-status" || command == "schema-migrate" || command == "schema-check-sql") return c::infrastructure::app::resourceSchemaCommand(command, args);
        if (command == "target-probe") return c::infrastructure::app::resourceTargetProbeCommand(args);
        if (command == "preview") return preview(args);
        if (command == "apply" || command == "status") return applyOrStatus(args, command == "apply");
        if (command == "baseline-review" || command == "baseline-seal" || command == "baseline-activate" || command == "baseline-maintenance") return c::infrastructure::app::resourceBaselineCommand(command, args, pool(args));
        if (command == "recover-tasks") return c::infrastructure::app::resourceRecoveryCommand(args, pool(args));
        if (command.starts_with("history-") || command == "import-control") return c::infrastructure::app::resourceHistoryCommand(command, args, pool(args));
        if (command.starts_with("cleanup-")) return c::infrastructure::app::resourceCloudCleanupCommand(command, args, pool(args));
        if (command.starts_with("discover-") || command == "queue-status" || command == "queue-limits") return c::infrastructure::app::resourceArchiveCommand(command, args, pool(args));
        if (command == "configure-target") {
            allowed(args, {"--database", "--destination", "--expected-revision", "--fingerprint"});
            const auto destination = model::parseResourceDestination(required(args, "--destination"));
            if (!destination) throw std::invalid_argument("destination is invalid");
            mysql::MySqlLegacyResourceImport importer(pool(args));
            const auto result = importer.configureTarget(*destination, number(required(args, "--expected-revision")), sha256(required(args, "--fingerprint")));
            if (!result.isSuccess()) throw std::invalid_argument(result.message());
            std::cout << nlohmann::json {{"target_revision", *result.data()}, {"baseline_activated", false}}.dump() << '\n';
            return 0;
        }
        throw std::invalid_argument("unknown maintenance command");
    } catch (const std::invalid_argument &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
    } catch (const std::exception &) {
        std::cerr << "FAIL: resource cloud maintenance dependency or file operation failed\n";
    }
    return 1;
}
