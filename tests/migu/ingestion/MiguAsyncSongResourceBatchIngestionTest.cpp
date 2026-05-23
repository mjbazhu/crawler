#include "crawler/application/use_cases/AsyncResourcePortAdapters.h"
#include "crawler/application/use_cases/AsyncSongResourceDownloadWorkflow.h"
#include "crawler/application/use_cases/SongResourceDownloadWorkflow.h"
#include "crawler/infrastructure/execution/asio/AsioAsyncRuntime.h"
#include "crawler/infrastructure/execution/threading/ThreadPool.h"
#include "crawler/infrastructure/http/client/AsioHttpClient.h"
#include "crawler/infrastructure/http/client/AsyncHttpSongResourceContentFetcher.h"
#include "crawler/infrastructure/music/migu/parser/MiguSongResourceParser.h"
#include "crawler/infrastructure/music/migu/provider/MiguAsyncSongResourceProvider.h"
#include "crawler/infrastructure/music/migu/provider/MiguPacmtokenClient.h"
#include "crawler/infrastructure/music/migu/provider/MiguRuntimePacmtokenStore.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlConnectionPool.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMusicCatalogQueryRepository.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlSongResourceQueue.h"
#include "crawler/infrastructure/platform/Environment.h"
#include "crawler/infrastructure/platform/PlatformUtils.h"
#include "crawler/infrastructure/storage/filesystem/FileStorage.h"
#include "migu/support/MySqlTestSupport.h"

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <atomic>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>

namespace crawler = orchestrator::modules::crawler;
namespace app_async = crawler::application::async;
namespace model = crawler::application::model;
namespace use_cases = crawler::application::use_cases;
namespace asio_runtime = crawler::infrastructure::execution::asio;
namespace threading = crawler::infrastructure::execution::threading;
namespace http_client = crawler::infrastructure::http::client;
namespace migu_parser = crawler::infrastructure::music::migu::parser;
namespace migu_provider = crawler::infrastructure::music::migu::provider;
namespace mysql = crawler::infrastructure::music::persistence::mysql;
namespace platform = crawler::infrastructure::platform;
namespace filesystem_storage = crawler::infrastructure::storage::filesystem;
namespace mysql_support = crawler::tests::migu::support;

namespace {

// 真实异步资源测试进度监听器；只输出脱敏阶段信息，不输出临时 URL 或 token。
class ProgressLoggingListener : public use_cases::SongResourceProcessListener {
public:
    explicit ProgressLoggingListener(bool verbose = false) : verbose_(verbose)
    {}

    void onItemStarted(const model::SongResourceProcessItem &item) override
    {
        ++started_count_;
        if (verbose_) {
            std::cout << "[Trace] started resource_id=" << item.resource_id << ", kind=" << item.resource_kind << ", format=" << item.format_type << std::endl;
        }
    }

    void onItemFinished(const model::SongResourceProcessItem &item) override
    {
        ++finished_count_;
        if (verbose_) {
            std::cout << "[Trace] finished resource_id=" << item.resource_id << ", kind=" << item.resource_kind << ", ok=" << (item.ok ? "true" : "false") << ", skipped=" << (item.skipped ? "true" : "false") << ", code=" << item.result_code << ", category=" << item.error_category << std::endl;
        }
    }

    void onItemEvent(const model::SongResourceProcessItem &item, std::string event_type, std::string) override
    {
        ++event_count_;
        if (verbose_) {
            std::cout << "[Trace] event resource_id=" << item.resource_id << ", kind=" << item.resource_kind << ", format=" << item.format_type << ", event=" << event_type << std::endl;
        }
    }

    void onPacmtokenRefreshed(bool refreshed, std::size_t token_length, std::uint64_t updated_at) override
    {
        (void)refreshed;
        (void)token_length;
        (void)updated_at;
        ++pacmtoken_refresh_count_;
    }

    int startedCount() const
    {
        return started_count_.load();
    }

    int finishedCount() const
    {
        return finished_count_.load();
    }

    int eventCount() const
    {
        return event_count_.load();
    }

    int pacmtokenRefreshCount() const
    {
        return pacmtoken_refresh_count_.load();
    }

private:
    std::atomic<int> started_count_ {0};           // 已收到 started 回调数
    std::atomic<int> finished_count_ {0};          // 已收到 finished 回调数
    std::atomic<int> event_count_ {0};             // 已收到事件回调数
    std::atomic<int> pacmtoken_refresh_count_ {0}; // 已收到 pacmtoken 刷新回调数
    bool verbose_ = false;                         // 是否输出脱敏事件轨迹
};

// 从环境变量读取真实 HTTP 配置，资源下载默认放宽响应体上限。
http_client::AsioHttpClient::Options makeHttpOptionsFromEnv()
{
    http_client::AsioHttpClient::Options options;
    options.io_threads = platform::Environment::getIntOr("CRAWLER_OUTBOUND_HTTP_IO_THREADS", 4);
    options.verify_tls = platform::Environment::getBoolOr("CRAWLER_OUTBOUND_VERIFY_TLS", options.verify_tls);
    options.request_timeout = std::chrono::milliseconds(platform::Environment::getIntOr("CRAWLER_OUTBOUND_HTTP_TIMEOUT_MS", 120000));
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

// 异步执行一批真实资源下载，用于验证协程 provider、流式下载和 async blob sink。
app_async::Task<model::SongResourceProcessResult> runAsyncResourceBatch(
    std::shared_ptr<use_cases::AsyncSongResourceDownloadWorkflow> workflow,
    model::SongResourceClaimQuery query,
    use_cases::SongResourceProcessListener *listener
)
{
    co_return co_await workflow->processPending(std::move(query), listener);
}

// 执行真实 Migu 异步资源批量获取、流式下载、本地保存和 MySQL 回写。
void testMiguAsyncSongResourceBatchIngestion()
{
    platform::Environment::loadDotEnvFile(".env.local");
    const mysql::MySqlConfig mysql_config = mysql_support::makeMySqlConfigFromEnv();
    mysql_support::assertDirectMySqlAvailable(mysql_config);

    auto mysql_pool = std::make_shared<mysql::MySqlConnectionPool>(mysql_config);
    mysql_support::assertMySqlAvailable(mysql_pool);

    auto http = std::make_shared<http_client::AsioHttpClient>(makeHttpOptionsFromEnv());
    auto blocking_executor = std::make_shared<threading::ThreadPool>(static_cast<std::size_t>(platform::Environment::getIntOr("CRAWLER_TEST_ASYNC_BLOCKING_THREADS", 4)));
    auto runtime = std::make_shared<asio_runtime::AsioAsyncRuntime>(http->executor(), blocking_executor);

    auto pacmtoken_store = makePacmtokenStoreFromEnv();
    const auto current_pacmtoken = pacmtoken_store->currentPacmtoken();
    if (!current_pacmtoken) {
        mysql_pool->cancel();
        throw std::runtime_error("MIGU_PACMTOKEN, CRAWLER_MIGU_PACMTOKEN or MIGU_COOKIE must be configured");
    }
    std::cout << "[Check] Runtime pacmtoken configured=true, length=" << current_pacmtoken->size() << std::endl;

    auto queue = std::make_shared<mysql::MySqlSongResourceQueue>(mysql_pool);
    auto async_queue = std::make_shared<use_cases::BlockingAsyncSongResourceQueue>(queue, runtime);
    auto provider = std::make_shared<migu_provider::MiguAsyncSongResourceProvider>(http, pacmtoken_store);
    auto parser = std::make_shared<migu_parser::MiguSongResourceParser>();
    auto fetcher = std::make_shared<http_client::AsyncHttpSongResourceContentFetcher>(http);
    const std::string storage_root = platform::Environment::getStringOr("CRAWLER_TEST_ASYNC_RESOURCE_STORAGE_ROOT", "build/migu-async-resource-ingestion-real");
    auto storage = std::make_shared<filesystem_storage::FileStorage>(storage_root);
    auto async_blob_store = std::make_shared<use_cases::BlockingAsyncSongResourceBlobStore>(storage, runtime);
    auto pacmtoken_refresher = std::make_shared<migu_provider::MiguPacmtokenClient>(http);

    auto workflow = std::make_shared<use_cases::AsyncSongResourceDownloadWorkflow>(
        runtime,
        async_queue,
        provider,
        parser,
        fetcher,
        async_blob_store,
        use_cases::SongResourcePathPlanner {},
        pacmtoken_refresher,
        pacmtoken_store
    );

    model::SongResourceClaimQuery query;
    query.source = "migu";
    query.resource_kind = "audio";
    query.format_type = platform::Environment::getStringOr("CRAWLER_TEST_MIGU_ASYNC_RESOURCE_FORMAT", "ZQ24");
    query.limit = static_cast<std::size_t>(platform::Environment::getIntOr("CRAWLER_TEST_MIGU_ASYNC_RESOURCE_LIMIT", 3));
    query.batch_id = "migu-async-resource-ingestion-test";
    query.options.resource_download_concurrency = static_cast<std::size_t>(platform::Environment::getIntOr("CRAWLER_TEST_MIGU_ASYNC_RESOURCE_CONCURRENCY", 3));
    query.options.resource_download_retries = platform::Environment::getIntOr("CRAWLER_TEST_MIGU_ASYNC_RESOURCE_RETRIES", 1);
    query.options.resource_download_timeout_ms = platform::Environment::getIntOr("CRAWLER_TEST_MIGU_ASYNC_RESOURCE_TIMEOUT_MS", 120000);
    query.options.pacmtoken_refresh_interval_seconds = platform::Environment::getIntOr("CRAWLER_TEST_MIGU_ASYNC_PACMTOKEN_REFRESH_INTERVAL_SECONDS", 300);
    query.options.auto_refresh_pacmtoken = platform::Environment::getBoolOr("CRAWLER_TEST_MIGU_ASYNC_AUTO_REFRESH_PACMTOKEN", true);
    query.options.refresh_pacmtoken_on_440013 = platform::Environment::getBoolOr("CRAWLER_TEST_MIGU_ASYNC_REFRESH_ON_440013", true);

    const int recover_stale_seconds = platform::Environment::getIntOr("CRAWLER_TEST_MIGU_ASYNC_RECOVER_STALE_SECONDS", 0);
    if (recover_stale_seconds > 0) {
        mysql::MySqlMusicCatalogQueryRepository catalog_repository(mysql_pool);
        model::MiguResourceActionQuery recover_query;
        recover_query.source = query.source;
        recover_query.resource_kind = query.resource_kind;
        recover_query.format_type = query.format_type;
        recover_query.limit = platform::Environment::getIntOr("CRAWLER_TEST_MIGU_ASYNC_RECOVER_STALE_LIMIT", static_cast<int>(query.limit));
        recover_query.stale_after_seconds = recover_stale_seconds;

        int recovered_total = 0;
        for (;;) {
            const int recovered = catalog_repository.recoverStaleResources(recover_query, "async resource ingestion test recovered stale running resource");
            recovered_total += recovered;
            if (recovered == 0 || recovered < recover_query.limit) {
                break;
            }
        }
        std::cout << "[Check] Recovered stale resources=" << recovered_total << ", stale_after_seconds=" << recover_stale_seconds << std::endl;
    }

    std::cout << "[Check] Async resource process: limit=" << query.limit << ", format=" << query.format_type
              << ", concurrency=" << query.options.resource_download_concurrency << ", storage_root=" << storage_root << std::endl;

    const bool verbose_progress = platform::Environment::getBoolOr("CRAWLER_TEST_MIGU_ASYNC_VERBOSE_PROGRESS", false);
    ProgressLoggingListener listener(verbose_progress);
    const bool log_progress = platform::Environment::getBoolOr("CRAWLER_TEST_MIGU_ASYNC_LOG_PROGRESS", false);
    const model::SongResourceProcessResult result = runtime->runForTest(runAsyncResourceBatch(workflow, std::move(query), log_progress ? &listener : nullptr));
    if (log_progress) {
        std::cout << "[Listener] started=" << listener.startedCount() << ", finished=" << listener.finishedCount()
                  << ", events=" << listener.eventCount() << ", pacmtoken_refreshes=" << listener.pacmtokenRefreshCount() << std::endl;
    }
    std::cout << "[Result] claimed=" << result.claimed_count << ", succeeded=" << result.succeeded_count
              << ", failed=" << result.failed_count << ", skipped=" << result.skipped_count << std::endl;
    std::cout << "[Result] related_succeeded=" << result.related_succeeded_count << ", related_failed=" << result.related_failed_count
              << ", related_skipped=" << result.related_skipped_count << ", items=" << result.items.size() << std::endl;
    for (const auto &item : result.items) {
        std::cout << "[Item] resource_id=" << item.resource_id << ", kind=" << item.resource_kind << ", format=" << item.format_type
                  << ", ok=" << (item.ok ? "true" : "false") << ", skipped=" << (item.skipped ? "true" : "false")
                  << ", code=" << item.result_code << ", category=" << item.error_category << std::endl;
    }

    mysql_pool->cancel();

    if (result.claimed_count == 0) {
        throw std::runtime_error("no resource was claimed from MySQL");
    }
    if (result.succeeded_count == 0) {
        throw std::runtime_error("no async Migu resource was downloaded successfully");
    }
    if (result.failed_count > 0) {
        throw std::runtime_error("some async Migu resources failed");
    }
}

} // namespace

int main()
{
    try {
        platform::setupConsole();
        testMiguAsyncSongResourceBatchIngestion();
        std::cout << "MiguAsyncSongResourceBatchIngestionTest passed." << std::endl;
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "MiguAsyncSongResourceBatchIngestionTest failed: " << e.what() << std::endl;
        return 1;
    }
}
