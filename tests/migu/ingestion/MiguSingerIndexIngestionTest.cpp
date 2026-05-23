#include "crawler/application/use_cases/CrawlerService.h"
#include "crawler/domain/music/model/Singer.h"
#include "migu/support/MySqlTestSupport.h"
#include "crawler/infrastructure/execution/threading/ThreadPool.h"
#include "crawler/infrastructure/http/client/AsioHttpClient.h"
#include "crawler/infrastructure/music/migu/parser/MiguParser.h"
#include "crawler/infrastructure/music/migu/provider/MiguProvider.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlConnectionPool.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlSingerSourceRepository.h"
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

// 执行带地区和性别参数的标量查询模板，用于验证歌手分类入库规模。
boost::asio::awaitable<std::uint64_t> executeSingerClassScalar(
    const std::shared_ptr<mysql::MySqlConnectionPool> &pool,
    std::string sql_path,
    std::string source_country,
    int gender
)
{
    auto connection = co_await pool->coAcquire();
    auto statement = co_await connection->async_prepare_statement(sql_loader::SqlFileLoader::loadOrThrow(sql_path), boost::asio::use_awaitable);
    boost::mysql::results result;
    co_await connection->async_execute(statement.bind(std::move(source_country), gender), result, boost::asio::use_awaitable);
    if (result.rows().empty()) {
        co_return 0;
    }
    co_return mysql::MySqlValueReader::uint64At(result.rows().front(), 0);
}

// 根据 tab 获取测试期望分类。
crawler::domain::CountryGender singerClassFromTab(const std::string &tab)
{
    const auto it = crawler::domain::kTabMap.find(tab);
    if (it == crawler::domain::kTabMap.end()) {
        throw std::runtime_error("unsupported CRAWLER_TEST_MIGU_SINGER_TAB: " + tab);
    }
    return it->second;
}

// 执行 Migu 歌手分类同步入库测试。
void testMiguSingerIndexIngestion()
{
    const mysql::MySqlConfig mysql_config = mysql_support::makeMySqlConfigFromEnv();

    const std::string tab = platform::Environment::getStringOr("CRAWLER_TEST_MIGU_SINGER_TAB", "huayu-nan");
    const int limit = platform::Environment::getIntOr("CRAWLER_TEST_MIGU_SINGER_INDEX_LIMIT", 50);
    if (limit <= 0) {
        throw std::runtime_error("CRAWLER_TEST_MIGU_SINGER_INDEX_LIMIT must be positive");
    }

    const auto singer_class = singerClassFromTab(tab);
    const int gender = static_cast<int>(singer_class.gender);

    mysql_support::assertDirectMySqlAvailable(mysql_config);

    auto mysql_pool = std::make_shared<mysql::MySqlConnectionPool>(mysql_config);
    mysql_support::assertMySqlAvailable(mysql_pool);

    const std::uint64_t before_refs = mysql_support::runOnExecutor(
        mysql_pool->executor(),
        executeSingerClassScalar(mysql_pool, "sql/mysql/tests/test_count_migu_singer_index_refs_by_class.sql", singer_class.country, gender),
        "count existing Migu singer index refs",
        mysql_support::mysqlOperationTimeout()
    );

    auto http = std::make_shared<http_client::AsioHttpClient>(makeHttpOptionsFromEnv());
    auto provider = std::make_shared<migu_provider::MiguProvider>(http);
    auto parser = std::make_shared<migu_parser::MiguParser>();
    auto source_repository = std::make_shared<mysql::MySqlSingerSourceRepository>(mysql_pool);

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
        nullptr,
        source_repository
    );

    std::cout << "[Check] Crawling Migu singer index tab=" << tab << "..." << std::endl;
    const bool saved = service.crawlSingerIndexAndSaveSync(tab, limit);
    if (!saved) {
        mysql_pool->cancel();
        throw std::runtime_error("crawlSingerIndexAndSaveSync returned false");
    }

    const std::uint64_t after_refs = mysql_support::runOnExecutor(
        mysql_pool->executor(),
        executeSingerClassScalar(mysql_pool, "sql/mysql/tests/test_count_migu_singer_index_refs_by_class.sql", singer_class.country, gender),
        "count Migu singer index refs after ingestion",
        mysql_support::mysqlOperationTimeout()
    );

    std::cout << "[Result] tab=" << tab << ", country=" << singer_class.country << ", gender=" << gender << ", limit=" << limit << std::endl;
    std::cout << "[Result] singer_source_refs before=" << before_refs << ", after=" << after_refs << std::endl;

    if (after_refs == 0) {
        throw std::runtime_error("Migu singer index ingestion did not create expected singer_source_refs records");
    }

    mysql_pool->cancel();
}

} // namespace

int main()
{
    try {
        platform::setupConsole();
        testMiguSingerIndexIngestion();
        std::cout << "MiguSingerIndexIngestionTest passed." << std::endl;
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "MiguSingerIndexIngestionTest failed: " << e.what() << std::endl;
        return 1;
    }
}
