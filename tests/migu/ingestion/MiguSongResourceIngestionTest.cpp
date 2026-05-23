#include "crawler/application/use_cases/SongResourceDownloadWorkflow.h"
#include "crawler/infrastructure/http/client/AsioHttpClient.h"
#include "crawler/infrastructure/http/client/HttpSongResourceContentFetcher.h"
#include "crawler/infrastructure/music/migu/parser/MiguSongResourceParser.h"
#include "crawler/infrastructure/music/migu/provider/MiguProvider.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlConnectionPool.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlSongResourceQueue.h"
#include "crawler/infrastructure/platform/Environment.h"
#include "crawler/infrastructure/platform/PlatformUtils.h"
#include "crawler/infrastructure/storage/filesystem/FileStorage.h"
#include "migu/support/MySqlTestSupport.h"

#include <chrono>
#include <cstddef>
#include <exception>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

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

// 执行真实 Migu 资源获取、下载、本地保存和 MySQL 回写。
void testMiguSongResourceIngestion()
{
    const mysql::MySqlConfig mysql_config = mysql_support::makeMySqlConfigFromEnv();
    mysql_support::assertDirectMySqlAvailable(mysql_config);

    std::shared_ptr<mysql::MySqlConnectionPool> mysql_pool = std::make_shared<mysql::MySqlConnectionPool>(mysql_config);
    mysql_support::assertMySqlAvailable(mysql_pool);

    const std::string source = platform::Environment::getStringOr("CRAWLER_TEST_MIGU_RESOURCE_SOURCE", "migu");
    const std::string kind = platform::Environment::getStringOr("CRAWLER_TEST_MIGU_RESOURCE_KIND", "audio");
    const std::string content_id = platform::Environment::getStringOr("CRAWLER_TEST_MIGU_RESOURCE_CONTENT_ID", "600902000006889266");
    const std::string default_format = kind == "audio" ? "ZQ24" : "";
    const std::string format_type = platform::Environment::getStringOr("CRAWLER_TEST_MIGU_RESOURCE_FORMAT", default_format);
    const int limit = platform::Environment::getIntOr("CRAWLER_TEST_MIGU_RESOURCE_LIMIT", 1);
    const std::string storage_root = platform::Environment::getStringOr("CRAWLER_TEST_RESOURCE_STORAGE_ROOT", "build/migu-resource-ingestion");
    if (limit <= 0) {
        mysql_pool->cancel();
        throw std::runtime_error("CRAWLER_TEST_MIGU_RESOURCE_LIMIT must be positive");
    }

    std::shared_ptr<http_client::AsioHttpClient> http = std::make_shared<http_client::AsioHttpClient>(makeHttpOptionsFromEnv());
    std::shared_ptr<migu_provider::MiguProvider> provider = std::make_shared<migu_provider::MiguProvider>(http);
    std::shared_ptr<migu_parser::MiguSongResourceParser> parser = std::make_shared<migu_parser::MiguSongResourceParser>();
    std::shared_ptr<http_client::HttpSongResourceContentFetcher> fetcher = std::make_shared<http_client::HttpSongResourceContentFetcher>(http);
    std::shared_ptr<filesystem_storage::FileStorage> storage = std::make_shared<filesystem_storage::FileStorage>(storage_root);
    std::shared_ptr<mysql::MySqlSongResourceQueue> queue = std::make_shared<mysql::MySqlSongResourceQueue>(mysql_pool);

    use_cases::SongResourceDownloadWorkflow workflow(queue, provider, parser, fetcher, storage);
    model::SongResourceClaimQuery query;
    query.source = source;
    query.resource_kind = kind;
    query.source_content_id = content_id;
    query.format_type = format_type;
    query.limit = static_cast<std::size_t>(limit);
    query.batch_id = "migu-resource-ingestion-test";

    std::cout << "[Check] Claiming and downloading Migu resources: source=" << source << ", kind=" << kind << ", contentId=" << content_id << ", format=" << format_type << ", limit=" << limit << std::endl;
    const auto result = workflow.processPending(query);

    std::cout << "[Result] claimed=" << result.claimed_count << ", succeeded=" << result.succeeded_count << ", failed=" << result.failed_count << std::endl;
    std::cout << "[Result] related_succeeded=" << result.related_succeeded_count << ", related_failed=" << result.related_failed_count << ", related_skipped=" << result.related_skipped_count << std::endl;
    for (const auto &item : result.items) {
        std::cout << "[Item] resource_id=" << item.resource_id << ", kind=" << item.resource_kind << ", format=" << item.format_type << ", ok=" << (item.ok ? "true" : "false") << ", code=" << item.result_code << ", message=" << item.message << std::endl;
    }

    if (result.claimed_count == 0) {
        mysql_pool->cancel();
        throw std::runtime_error("no pending resource was claimed; prepare song_resources with status=0/3 for CRAWLER_TEST_MIGU_RESOURCE_CONTENT_ID or reset one resource before running this test");
    }
    if (result.succeeded_count == 0) {
        mysql_pool->cancel();
        throw std::runtime_error("no Migu resource was downloaded successfully");
    }

    mysql_pool->cancel();
}

} // namespace

int main()
{
    try {
        platform::setupConsole();
        testMiguSongResourceIngestion();
        std::cout << "MiguSongResourceIngestionTest passed." << std::endl;
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "MiguSongResourceIngestionTest failed: " << e.what() << std::endl;
        return 1;
    }
}
