#include "crawler/application/use_cases/SongResourceDownloadWorkflow.h"
#include "crawler/infrastructure/http/client/AsioHttpClient.h"
#include "crawler/infrastructure/http/client/HttpSongResourceContentFetcher.h"
#include "crawler/infrastructure/music/migu/parser/MiguSongResourceParser.h"
#include "crawler/infrastructure/music/migu/provider/MiguPacmtokenClient.h"
#include "crawler/infrastructure/music/migu/provider/MiguProvider.h"
#include "crawler/infrastructure/music/migu/provider/MiguRuntimePacmtokenStore.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlConnectionPool.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlSongResourceQueue.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlValueReader.h"
#include "crawler/infrastructure/platform/Environment.h"
#include "crawler/infrastructure/platform/PlatformUtils.h"
#include "crawler/infrastructure/storage/filesystem/FileStorage.h"
#include "migu/support/MySqlTestSupport.h"

#include <boost/asio/use_awaitable.hpp>
#include <boost/mysql/results.hpp>
#include <boost/mysql/statement.hpp>

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <vector>

namespace crawler = orchestrator::modules::crawler;
namespace model = crawler::application::model;
namespace use_cases = crawler::application::use_cases;
namespace http_client = crawler::infrastructure::http::client;
namespace migu_parser = crawler::infrastructure::music::migu::parser;
namespace migu_provider = crawler::infrastructure::music::migu::provider;
namespace mysql = crawler::infrastructure::music::persistence::mysql;
namespace platform = crawler::infrastructure::platform;
namespace filesystem_storage = crawler::infrastructure::storage::filesystem;
namespace mysql_support = crawler::tests::migu::support;

namespace {

// 单首歌的资源处理候选，来自 MySQL 待处理资源队列。
struct SongResourceCandidate {
    std::string content_id;        // Migu contentId
    std::string title;             // 歌曲标题，仅用于调试输出
    std::uint64_t resource_count;  // 符合条件的待处理资源数量
};

// 批量处理统计，用于调试入口最终判断是否跑通。
struct BatchTotals {
    int song_count = 0;              // 读取到的歌曲数量
    int claimed_count = 0;           // 认领主资源数量
    int succeeded_count = 0;         // 主资源成功数量
    int failed_count = 0;            // 主资源失败数量
    int skipped_count = 0;           // 主资源跳过数量
    int related_succeeded_count = 0; // 关联歌词资源成功数量
    int related_failed_count = 0;    // 关联歌词资源失败数量
    int related_skipped_count = 0;   // 关联歌词资源跳过数量
};

// 从环境变量读取出站 HTTP 配置，资源下载默认放宽响应体上限。
http_client::AsioHttpClient::Options makeHttpOptionsFromEnv()
{
    http_client::AsioHttpClient::Options options;
    options.io_threads = platform::Environment::getIntOr("CRAWLER_OUTBOUND_HTTP_IO_THREADS", 2);
    options.verify_tls = platform::Environment::getBoolOr("CRAWLER_OUTBOUND_VERIFY_TLS", options.verify_tls);
    options.request_timeout = std::chrono::milliseconds(platform::Environment::getIntOr("CRAWLER_OUTBOUND_HTTP_TIMEOUT_MS", 30000));
    options.max_response_bytes = static_cast<std::size_t>(platform::Environment::getIntOr("CRAWLER_RESOURCE_MAX_RESPONSE_BYTES", 200 * 1024 * 1024));
    return options;
}

// 返回当前 Unix 秒，用于标记测试注入的运行期 pacmtoken 更新时间。
std::uint64_t currentUnixSeconds()
{
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}

// 从 Cookie 字符串中提取 pacmtoken；环境变量值本身是 token 时原样返回。
std::string normalizePacmtoken(std::string value)
{
    constexpr std::string_view marker = "pacmtoken=";
    const std::size_t marker_pos = value.find(marker);
    if (marker_pos == std::string::npos) {
        return value;
    }

    const std::size_t value_begin = marker_pos + marker.size();
    const std::size_t value_end = value.find(';', value_begin);
    return value.substr(value_begin, value_end == std::string::npos ? std::string::npos : value_end - value_begin);
}

// 创建测试期运行 token store；只读取环境变量，不输出 token 明文。
std::shared_ptr<migu_provider::MiguRuntimePacmtokenStore> makePacmtokenStoreFromEnv()
{
    auto store = std::make_shared<migu_provider::MiguRuntimePacmtokenStore>();
    auto token = platform::Environment::getString("MIGU_PACMTOKEN");
    if (!token) {
        token = platform::Environment::getString("CRAWLER_MIGU_PACMTOKEN");
    }
    if (!token) {
        token = platform::Environment::getString("MIGU_COOKIE");
    }
    if (token && !token->empty()) {
        store->updatePacmtoken(normalizePacmtoken(std::move(*token)), currentUnixSeconds());
    }
    return store;
}

// 根据资源类型和音质过滤，读取最多 N 首仍有待处理资源的歌曲。
boost::asio::awaitable<std::vector<SongResourceCandidate>> loadSongCandidates(
    const std::shared_ptr<mysql::MySqlConnectionPool> &pool,
    std::string source,
    std::string resource_kind,
    std::string format_type,
    int song_limit
)
{
    std::vector<SongResourceCandidate> candidates;
    auto connection = co_await pool->coAcquire();
    boost::mysql::statement statement = co_await connection->async_prepare_statement(
        R"SQL(
SELECT
    r.source_content_id,
    COALESCE(MAX(s.title), ''),
    COUNT(*) AS resource_count
FROM song_resources sr
JOIN song_source_refs r ON r.id = sr.song_source_ref_id
JOIN songs s ON s.id = r.song_db_id
WHERE r.source = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
  AND (? = '' OR sr.resource_kind = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci)
  AND (? = '' OR sr.format_type = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci)
  AND sr.status IN (0, 3)
  AND r.source_content_id IS NOT NULL
  AND r.source_content_id <> ''
GROUP BY r.source_content_id
ORDER BY MIN(sr.updated_at) ASC, MIN(sr.id) ASC
LIMIT ?
)SQL",
        boost::asio::use_awaitable
    );

    boost::mysql::results result;
    co_await connection->async_execute(
        statement.bind(std::move(source), resource_kind, resource_kind, format_type, format_type, static_cast<std::uint64_t>(song_limit)),
        result,
        boost::asio::use_awaitable
    );

    candidates.reserve(result.rows().size());
    for (boost::mysql::row_view row : result.rows()) {
        SongResourceCandidate candidate;
        candidate.content_id = mysql::MySqlValueReader::stringAt(row, 0);
        candidate.title = mysql::MySqlValueReader::stringAt(row, 1);
        candidate.resource_count = mysql::MySqlValueReader::uint64At(row, 2);
        candidates.push_back(std::move(candidate));
    }
    co_return candidates;
}

// 打印单首歌处理结果，方便 VSCode 调试时直接看控制台。
void printSongResult(const SongResourceCandidate &candidate, const model::SongResourceProcessResult &result)
{
    std::cout << "[Song] contentId=" << candidate.content_id << ", title=" << candidate.title << ", pending_resources=" << candidate.resource_count << std::endl;
    std::cout << "[SongResult] claimed=" << result.claimed_count << ", succeeded=" << result.succeeded_count << ", failed=" << result.failed_count << ", skipped=" << result.skipped_count
              << ", related_succeeded=" << result.related_succeeded_count << ", related_failed=" << result.related_failed_count << ", related_skipped=" << result.related_skipped_count << std::endl;
    for (const auto &item : result.items) {
        std::cout << "[Item] resource_id=" << item.resource_id << ", kind=" << item.resource_kind << ", format=" << item.format_type
                  << ", ok=" << (item.ok ? "true" : "false") << ", code=" << item.result_code << ", message=" << item.message << std::endl;
    }
}

// 汇总单首歌处理结果。
void addTotals(BatchTotals &totals, const model::SongResourceProcessResult &result)
{
    totals.claimed_count += result.claimed_count;
    totals.succeeded_count += result.succeeded_count;
    totals.failed_count += result.failed_count;
    totals.skipped_count += result.skipped_count;
    totals.related_succeeded_count += result.related_succeeded_count;
    totals.related_failed_count += result.related_failed_count;
    totals.related_skipped_count += result.related_skipped_count;
}

// 执行真实 Migu 资源批量获取、下载、本地保存和 MySQL 回写，默认从 MySQL 读取 10 首歌。
void testMiguSongResourceBatchIngestion()
{
    const mysql::MySqlConfig mysql_config = mysql_support::makeMySqlConfigFromEnv();
    mysql_support::assertDirectMySqlAvailable(mysql_config);

    std::shared_ptr<mysql::MySqlConnectionPool> mysql_pool = std::make_shared<mysql::MySqlConnectionPool>(mysql_config);
    mysql_support::assertMySqlAvailable(mysql_pool);

    const std::string source = platform::Environment::getStringOr("CRAWLER_TEST_MIGU_RESOURCE_SOURCE", "migu");
    const std::string kind = platform::Environment::getStringOr("CRAWLER_TEST_MIGU_RESOURCE_KIND", "audio");
    const std::string default_format = kind == "audio" ? "ZQ24" : "";
    const std::string format_type = platform::Environment::getStringOr("CRAWLER_TEST_MIGU_RESOURCE_FORMAT", default_format);
    const int song_limit = platform::Environment::getIntOr("CRAWLER_TEST_MIGU_RESOURCE_SONG_LIMIT", 10);
    const int default_per_song_limit = kind == "audio" && format_type.empty() ? 20 : 1;
    const int per_song_limit = platform::Environment::getIntOr("CRAWLER_TEST_MIGU_RESOURCE_PER_SONG_LIMIT", default_per_song_limit);
    const bool allow_partial = platform::Environment::getBoolOr("CRAWLER_TEST_MIGU_RESOURCE_ALLOW_PARTIAL", true);
    const std::string storage_root = platform::Environment::getStringOr("CRAWLER_TEST_RESOURCE_STORAGE_ROOT", "build/migu-resource-ingestion");
    if (song_limit <= 0 || per_song_limit <= 0) {
        mysql_pool->cancel();
        throw std::runtime_error("CRAWLER_TEST_MIGU_RESOURCE_SONG_LIMIT and CRAWLER_TEST_MIGU_RESOURCE_PER_SONG_LIMIT must be positive");
    }

    std::vector<SongResourceCandidate> candidates = mysql_support::runOnExecutor(
        mysql_pool->executor(),
        loadSongCandidates(mysql_pool, source, kind, format_type, song_limit),
        "load Migu song resource candidates",
        mysql_support::mysqlOperationTimeout()
    );
    if (candidates.empty()) {
        mysql_pool->cancel();
        throw std::runtime_error("no pending song resource candidate was found; reset resources or adjust resource kind/format filters");
    }

    std::shared_ptr<http_client::AsioHttpClient> http = std::make_shared<http_client::AsioHttpClient>(makeHttpOptionsFromEnv());
    auto pacmtoken_store = makePacmtokenStoreFromEnv();
    const auto current_pacmtoken = pacmtoken_store->currentPacmtoken();
    std::cout << "[Check] Runtime pacmtoken configured=" << (current_pacmtoken ? "true" : "false")
              << ", length=" << (current_pacmtoken ? current_pacmtoken->size() : 0) << std::endl;

    std::shared_ptr<migu_provider::MiguProvider> provider = std::make_shared<migu_provider::MiguProvider>(http, pacmtoken_store);
    auto pacmtoken_refresher = std::make_shared<migu_provider::MiguPacmtokenClient>(http);
    std::shared_ptr<migu_parser::MiguSongResourceParser> parser = std::make_shared<migu_parser::MiguSongResourceParser>();
    std::shared_ptr<http_client::HttpSongResourceContentFetcher> fetcher = std::make_shared<http_client::HttpSongResourceContentFetcher>(http);
    std::shared_ptr<filesystem_storage::FileStorage> storage = std::make_shared<filesystem_storage::FileStorage>(storage_root);
    std::shared_ptr<mysql::MySqlSongResourceQueue> queue = std::make_shared<mysql::MySqlSongResourceQueue>(mysql_pool);

    use_cases::SongResourceDownloadWorkflow workflow(queue, provider, parser, fetcher, storage, {}, nullptr, pacmtoken_refresher, pacmtoken_store);
    BatchTotals totals;
    totals.song_count = static_cast<int>(candidates.size());

    std::cout << "[Check] Loaded " << candidates.size() << " songs from MySQL: source=" << source << ", kind=" << kind
              << ", format=" << format_type << ", per_song_limit=" << per_song_limit << std::endl;

    for (const auto &candidate : candidates) {
        model::SongResourceClaimQuery query;
        query.source = source;
        query.resource_kind = kind;
        query.source_content_id = candidate.content_id;
        query.format_type = format_type;
        query.limit = static_cast<std::size_t>(per_song_limit);
        query.batch_id = "migu-resource-batch-ingestion-test";

        const model::SongResourceProcessResult result = workflow.processPending(query);
        printSongResult(candidate, result);
        addTotals(totals, result);
    }

    std::cout << "[Total] songs=" << totals.song_count << ", claimed=" << totals.claimed_count << ", succeeded=" << totals.succeeded_count
              << ", failed=" << totals.failed_count << ", skipped=" << totals.skipped_count << std::endl;
    std::cout << "[Total] related_succeeded=" << totals.related_succeeded_count << ", related_failed=" << totals.related_failed_count << ", related_skipped=" << totals.related_skipped_count << std::endl;

    if (totals.claimed_count == 0) {
        mysql_pool->cancel();
        throw std::runtime_error("no resource was claimed from selected songs");
    }
    if (!allow_partial && totals.succeeded_count == 0) {
        mysql_pool->cancel();
        throw std::runtime_error("no Migu resource was downloaded successfully");
    }
    if (!allow_partial && totals.failed_count > 0) {
        mysql_pool->cancel();
        throw std::runtime_error("some Migu resources failed and CRAWLER_TEST_MIGU_RESOURCE_ALLOW_PARTIAL=false");
    }
    if (allow_partial && totals.succeeded_count == 0) {
        std::cout << "[Warn] No resource was downloaded successfully; selected resources were claimed but rejected or failed. Set CRAWLER_TEST_MIGU_RESOURCE_ALLOW_PARTIAL=false to make this fail." << std::endl;
    }

    mysql_pool->cancel();
}

} // namespace

int main()
{
    try {
        platform::setupConsole();
        testMiguSongResourceBatchIngestion();
        std::cout << "MiguSongResourceBatchIngestionTest passed." << std::endl;
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "MiguSongResourceBatchIngestionTest failed: " << e.what() << std::endl;
        return 1;
    }
}
