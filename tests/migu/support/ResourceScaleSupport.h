#pragma once

#include "crawler/application/model/ResourceTransferJobs.h"
#include "crawler/application/result/ServiceResult.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlConnectionPool.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlValueReader.h"
#include "migu/support/MySqlTestSupport.h"

#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <regex>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace orchestrator::modules::crawler::tests::scale {
namespace mysql = infrastructure::music::persistence::mysql;
namespace model = application::model;
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;

// 仅承载测试源码固定诊断，可安全写入机器可读失败记录。
class Assertion final : public std::runtime_error {
public:
    // 调用方只能提供源码中的固定代码，不传数据库原始异常。
    explicit Assertion(const char *message) : std::runtime_error(message)
    {}
};

// 只输出固定断言，避免认证、来源正文或数据库异常进入公开记录。
inline void require(bool condition, const char *message)
{
    if (!condition) throw Assertion(message);
}

// 密码仅从本次显式进程环境取得，不使用dotenv或运行服务默认配置。
inline std::string environment(const char *name)
{
    const auto *value = std::getenv(name);
    require(value != nullptr, "explicit_scale_environment_required");
    return value;
}

// 仅接受本轮已授权的恢复副本，其他实例和正式库失败关闭。
inline mysql::MySqlConfig configuration()
{
    mysql::MySqlConfig value;
    value.host = environment("CRAWLER_SCALE_MYSQL_HOST");
    const auto port = environment("CRAWLER_SCALE_MYSQL_PORT");
    require(value.host == "127.0.0.1" && port == "6111", "isolated_restore_endpoint_required");
    value.port = 6111;
    value.username = environment("CRAWLER_SCALE_MYSQL_USER");
    value.password = environment("CRAWLER_SCALE_MYSQL_PASSWORD");
    value.database = environment("CRAWLER_SCALE_MYSQL_DATABASE");
    require(!value.username.empty() && (value.database == "crawler_ownership_test_release_1030_331763" || value.database == "crawler_ownership_test_release_scale4_1030" || value.database == "crawler_ownership_test_release_scale4b_1030" || value.database == "crawler_ownership_test_release_scale4c_1030"), "authorized_restore_database_required");
    value.initialize_schema = false;
    value.io_threads = 2;
    value.initial_pool_size = 1;
    value.max_pool_size = 8;
    value.connect_timeout = std::chrono::seconds(5);
    value.retry_interval = std::chrono::seconds(1);
    return value;
}

// 记录自身生成输入的SHA，支持测前封存与测后原文复核。
inline std::string sha256(std::string_view text)
{
    std::array<unsigned char, 32> bytes {};
    unsigned int size = 0;
    require(EVP_Digest(text.data(), text.size(), bytes.data(), &size, EVP_sha256(), nullptr) == 1 && size == bytes.size(), "scale_digest_failed");
    constexpr char digits[] = "0123456789abcdef";
    std::string value;
    for (const auto byte : bytes) {
        value.push_back(digits[byte >> 4]);
        value.push_back(digits[byte & 15]);
    }
    return value;
}

// 输入只能是本轮有界JSON，拒绝符号链接和未封存的任意超大文件。
inline Json readJson(const std::filesystem::path &path)
{
    require(!std::filesystem::is_symlink(path) && std::filesystem::is_regular_file(path) && std::filesystem::file_size(path) <= 2 * 1024 * 1024, "bounded_scale_json_required");
    std::ifstream input(path, std::ios::binary);
    return Json::parse(input);
}

// 补充运行绑定原JSON的原始字节；重新排版也必须视作输入变更。
inline std::string jsonFileSha256(const std::filesystem::path &path)
{
    require(!std::filesystem::is_symlink(path) && std::filesystem::is_regular_file(path) && std::filesystem::file_size(path) <= 2 * 1024 * 1024, "bounded_original_scale_evidence_required");
    std::ifstream input(path, std::ios::binary);
    const std::string text {std::istreambuf_iterator<char>(input), {}};
    return sha256(text);
}

// 所有SQL都是测试源码固定语句，资源身份只来自受控fixture清单。
inline boost::mysql::results execute(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, std::string text)
{
    return migu::support::runOnExecutor(pool->executor(), pool->coExecute(std::move(text)), "scale SQL", std::chrono::seconds(120));
}

// 单个非空整数查询，不用information_schema估计值冒充实际行数。
inline std::uint64_t scalar(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, std::string text)
{
    const auto result = execute(pool, std::move(text));
    require(result.rows().size() == 1, "scale_scalar_row_required");
    return mysql::MySqlValueReader::uint64At(result.rows().front(), 0);
}

// ServiceResult保留成功值；异常详情只在调用方固定错误代码中体现。
template <typename T>
T get(application::result::ServiceResult<T> result)
{
    require(result.isSuccess() && result.data() != nullptr, "scale_repository_failed");
    return *result.data();
}

// 建池前用唯一的短直连核对排他状态，关闭后才允许池创建自己的连接。
inline boost::asio::awaitable<Json> identity(mysql::MySqlConfig config, Json plan)
{
    const auto executor = co_await boost::asio::this_coro::executor;
    boost::mysql::any_connection connection(executor);
    boost::mysql::connect_params parameters;
    parameters.server_address = boost::mysql::host_and_port {config.host, config.port};
    parameters.username = config.username;
    parameters.password = config.password;
    parameters.database = config.database;
    parameters.ssl = config.ssl_mode;
    co_await connection.async_connect(parameters, boost::asio::use_awaitable);
    boost::mysql::results rows;
    co_await connection.async_execute("SELECT @@server_uuid,DATABASE(),(SELECT MAX(version) FROM schema_migrations),(SELECT COUNT(*) FROM information_schema.PROCESSLIST WHERE ID<>CONNECTION_ID() AND USER NOT IN ('event_scheduler','system user'))", rows, boost::asio::use_awaitable);
    require(rows.rows().size() == 1, "scale_identity_missing");
    const auto row = rows.rows().front();
    Json observed = {{"server_uuid", mysql::MySqlValueReader::stringAt(row, 0)}, {"database", mysql::MySqlValueReader::stringAt(row, 1)}, {"schema_version", mysql::MySqlValueReader::uint64At(row, 2)}, {"other_client_connections", mysql::MySqlValueReader::uint64At(row, 3)}};
    require(observed.at("server_uuid") == plan.at("scope").at("server_uuid") && observed.at("server_uuid") == "79c305ac-d413-11f1-8dd9-b025aa3a9cbb", "scale_server_identity_changed");
    require(observed.at("database") == plan.at("scope").at("database") && observed.at("schema_version") == plan.at("scope").at("schema_version"), "scale_database_or_schema_changed");
    require(observed.at("other_client_connections") == 0, "scale_other_clients_present");
    co_await connection.async_close(boost::asio::use_awaitable);
    co_return observed;
}

// 首轮背景范围与旧克隆审计一致，精确COUNT排除新增schema元数据。
inline Json backgroundCounts(const std::shared_ptr<mysql::MySqlConnectionPool> &pool)
{
    Json counts = Json::object();
    for (const auto *table : {"album_artist_refs", "album_resources", "album_song_refs", "album_source_refs", "albums", "crawl_batch_events", "crawl_batch_items", "crawl_batches", "governance_events", "migu_dalbum_details", "singer_index_memberships", "singer_resources", "singer_source_refs", "singers", "song_artist_refs", "song_resources", "song_source_audio_formats", "song_source_refs", "songs"}) {
        counts[table] = scalar(pool, "SELECT COUNT(*) FROM `" + std::string(table) + "`");
    }
    return counts;
}

// 所有新资源治理表必须全空，仅三端预置状态行允许存在。
inline Json requireEmptyNewTables(const std::shared_ptr<mysql::MySqlConnectionPool> &pool)
{
    const auto tables = execute(pool, "SELECT table_name FROM information_schema.tables WHERE table_schema=DATABASE() AND table_type='BASE TABLE' AND LEFT(table_name,9)='resource_' AND table_name<>'resource_destination_states' ORDER BY table_name");
    require(tables.rows().size() >= 10, "scale_latest_resource_schema_missing");
    Json counts = Json::object();
    for (const auto row : tables.rows()) {
        const auto table = mysql::MySqlValueReader::stringAt(row, 0);
        require(std::regex_match(table, std::regex("resource_[a-z_]+")), "scale_table_name_invalid");
        const auto count = scalar(pool, "SELECT COUNT(*) FROM `" + table + "`");
        counts[table] = count;
        require(count == 0, "scale_new_resource_tables_not_empty");
    }
    return counts;
}

// Windows目录枚举可缓存尚未关闭文件的旧大小，必须另开共享读流取实际EOF。
struct DirectoryObservation {
    std::uint64_t eof_bytes = 0;      // 打开每个生成文件并seek EOF所得真实大小
    std::uint64_t metadata_bytes = 0; // 目录枚举缓存，仅用于诊断比较
    std::uint64_t files = 0;          // 本次实际扫描的文件数
};

// 只打开本轮生成文件的属性/EOF，不读取内容，也不遍历历史local根。
inline DirectoryObservation directoryObservation(const std::filesystem::path &root)
{
    DirectoryObservation observed;
    std::error_code error;
    std::filesystem::recursive_directory_iterator iterator(root, error), end;
    require(!error, "scale_staging_scan_failed");
    for (; iterator != end; iterator.increment(error)) {
        require(!error && !iterator->is_symlink(error), "scale_staging_link_or_scan_failed");
        if (iterator->is_regular_file(error)) {
            const auto metadata_size = iterator->file_size(error);
            require(!error, "scale_staging_size_failed");
            std::ifstream opened(iterator->path(), std::ios::binary | std::ios::ate);
            require(opened.is_open(), "scale_staging_EOF_open_failed");
            const auto size = opened.tellg();
            require(size >= std::streampos(0), "scale_staging_EOF_unavailable");
            observed.eof_bytes += static_cast<std::uint64_t>(size);
            observed.metadata_bytes += metadata_size;
            ++observed.files;
        }
    }
    require(!error, "scale_staging_iterator_failed");
    return observed;
}

// 最终残留与峰值统一采用实际EOF，不能混用目录缓存口径。
inline std::uint64_t directoryBytes(const std::filesystem::path &root)
{
    return directoryObservation(root).eof_bytes;
}

// 与正式受理相同的三端热点行锁；只记录SELECT发出至返回的总耗时。
inline boost::asio::awaitable<double> lockAcquisition(std::shared_ptr<mysql::MySqlConnectionPool> pool)
{
    auto connection = co_await pool->coAcquire();
    boost::mysql::results result;
    co_await connection->async_execute("START TRANSACTION", result, boost::asio::use_awaitable);
    const auto started = Clock::now();
    co_await connection->async_execute("SELECT destination,target_revision,target_fingerprint,dispatch_state,baseline_ready FROM resource_destination_states ORDER BY destination FOR UPDATE", result, boost::asio::use_awaitable);
    const auto elapsed = std::chrono::duration<double, std::milli>(Clock::now() - started).count();
    require(result.rows().size() == 3, "scale_hotspot_rows_changed");
    co_await connection->async_execute("ROLLBACK", result, boost::asio::use_awaitable);
    co_return elapsed;
}

// 原始JSONL逐样本刷新，父进程终止时仍保留已完成证据。
inline void output(const Json &value)
{
    std::cout << value.dump() << std::endl;
}
} // namespace orchestrator::modules::crawler::tests::scale
