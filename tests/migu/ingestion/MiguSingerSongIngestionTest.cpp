#include "crawler/application/use_cases/CrawlerService.h"
#include "migu/support/MySqlTestSupport.h"
#include "crawler/infrastructure/execution/threading/ThreadPool.h"
#include "crawler/infrastructure/http/client/AsioHttpClient.h"
#include "crawler/infrastructure/music/migu/parser/MiguParser.h"
#include "crawler/infrastructure/music/migu/provider/MiguProvider.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlConnectionPool.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlSongSourceRepository.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlValueReader.h"
#include "crawler/infrastructure/music/persistence/sql/SqlFileLoader.h"
#include "crawler/infrastructure/platform/Environment.h"
#include "crawler/infrastructure/platform/PlatformUtils.h"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/mysql/any_connection.hpp>
#include <boost/mysql/connect_params.hpp>
#include <boost/mysql/diagnostics.hpp>
#include <boost/mysql/results.hpp>
#include <boost/mysql/statement.hpp>

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

namespace crawler = orchestrator::modules::crawler;
namespace app = crawler::application;
namespace use_cases = crawler::application::use_cases;
namespace threading = crawler::infrastructure::execution::threading;
namespace http_client = crawler::infrastructure::http::client;
namespace migu_parser = crawler::infrastructure::music::migu::parser;
namespace migu_provider = crawler::infrastructure::music::migu::provider;
namespace mysql = crawler::infrastructure::music::persistence::mysql;
namespace sql_loader = crawler::infrastructure::music::persistence::sql;
namespace platform = crawler::infrastructure::platform;
namespace mysql_support = crawler::tests::migu::support;

namespace {

// 从环境变量读取出站 HTTP 配置。
http_client::AsioHttpClient::Options makeHttpOptionsFromEnv()
{
    http_client::AsioHttpClient::Options options;
    options.io_threads = platform::Environment::getIntOr("CRAWLER_OUTBOUND_HTTP_IO_THREADS", 2);
    options.verify_tls = platform::Environment::getBoolOr("CRAWLER_OUTBOUND_VERIFY_TLS", options.verify_tls);
    options.request_timeout = std::chrono::milliseconds(platform::Environment::getIntOr("CRAWLER_OUTBOUND_HTTP_TIMEOUT_MS", 30000));
    return options;
}

// 执行带 singer_id 参数的标量查询模板，用于验证入库后的数据规模。
boost::asio::awaitable<std::uint64_t> executeSingerScalar(
    const std::shared_ptr<mysql::MySqlConnectionPool> &pool,
    std::string sql_path,
    std::string singer_id
)
{
    auto connection = co_await pool->coAcquire();
    auto statement = co_await connection->async_prepare_statement(sql_loader::SqlFileLoader::loadOrThrow(sql_path), boost::asio::use_awaitable);
    boost::mysql::results result;
    co_await connection->async_execute(statement.bind(std::move(singer_id)), result, boost::asio::use_awaitable);
    if (result.rows().empty()) {
        co_return 0;
    }
    co_return mysql::MySqlValueReader::uint64At(result.rows().front(), 0);
}

// 执行 Migu 歌手歌曲同步入库测试。
void testMiguSingerSongIngestion()
{
    const mysql::MySqlConfig mysql_config = mysql_support::makeMySqlConfigFromEnv();

    const std::uint64_t singer_id = static_cast<std::uint64_t>(platform::Environment::getIntOr("CRAWLER_TEST_MIGU_SINGER_ID", 112));
    const int limit = platform::Environment::getIntOr("CRAWLER_TEST_MIGU_LIMIT", 20);
    if (singer_id == 0 || limit <= 0) {
        throw std::runtime_error("CRAWLER_TEST_MIGU_SINGER_ID and CRAWLER_TEST_MIGU_LIMIT must be positive");
    }

    mysql_support::assertDirectMySqlAvailable(mysql_config);

    auto mysql_pool = std::make_shared<mysql::MySqlConnectionPool>(mysql_config);
    mysql_support::assertMySqlAvailable(mysql_pool);

    const std::string singer_id_text = std::to_string(singer_id);
    const std::uint64_t before_refs = mysql_support::runOnExecutor(mysql_pool->executor(), executeSingerScalar(mysql_pool, "sql/mysql/tests/test_count_migu_singer_refs.sql", singer_id_text), "count existing Migu singer refs", mysql_support::mysqlOperationTimeout());

    auto http = std::make_shared<http_client::AsioHttpClient>(makeHttpOptionsFromEnv());
    auto provider = std::make_shared<migu_provider::MiguProvider>(http);
    auto parser = std::make_shared<migu_parser::MiguParser>();
    auto source_repository = std::make_shared<mysql::MySqlSongSourceRepository>(mysql_pool);

    auto parse_executor = std::make_shared<threading::ThreadPool>(1);
    auto batch_executor = std::make_shared<threading::ThreadPool>(1);
    auto page_executor = std::make_shared<threading::ThreadPool>(1);
    use_cases::CrawlerService service(
        provider,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        parser,
        parse_executor,
        batch_executor,
        page_executor,
        nullptr,
        1,
        1,
        0,
        std::chrono::minutes(5),
        app::model::CrawlerRuntimeSettings {},
        source_repository
    );

    std::cout << "[Check] Crawling Migu singer songs..." << std::endl;
    const bool saved = service.crawlSingerSongsAndSaveSync(singer_id, limit);
    if (!saved) {
        mysql_pool->cancel();
        throw std::runtime_error("crawlSingerSongsAndSaveSync returned false");
    }

    const std::uint64_t after_refs = mysql_support::runOnExecutor(mysql_pool->executor(), executeSingerScalar(mysql_pool, "sql/mysql/tests/test_count_migu_singer_refs.sql", singer_id_text), "count Migu singer refs after ingestion", mysql_support::mysqlOperationTimeout());
    const std::uint64_t audio_formats = mysql_support::runOnExecutor(mysql_pool->executor(), executeSingerScalar(mysql_pool, "sql/mysql/tests/test_count_migu_singer_audio_formats.sql", singer_id_text), "count Migu singer audio formats after ingestion", mysql_support::mysqlOperationTimeout());
    const std::uint64_t resources = mysql_support::runOnExecutor(mysql_pool->executor(), executeSingerScalar(mysql_pool, "sql/mysql/tests/test_count_migu_singer_resources.sql", singer_id_text), "count Migu singer resources after ingestion", mysql_support::mysqlOperationTimeout());

    std::cout << "[Result] singer_id=" << singer_id << ", limit=" << limit << std::endl;
    std::cout << "[Result] song_source_refs before=" << before_refs << ", after=" << after_refs << std::endl;
    std::cout << "[Result] audio_formats=" << audio_formats << ", resources=" << resources << std::endl;

    if (after_refs == 0 || audio_formats == 0 || resources == 0) {
        throw std::runtime_error("Migu singer song ingestion did not create expected source, format or resource records");
    }

    mysql_pool->cancel();
}

} // namespace

int main()
{
    try {
        platform::setupConsole();
        testMiguSingerSongIngestion();
        std::cout << "MiguSingerSongIngestionTest passed." << std::endl;
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "MiguSingerSongIngestionTest failed: " << e.what() << std::endl;
        return 1;
    }
}
