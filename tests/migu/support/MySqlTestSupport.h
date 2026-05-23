#pragma once

#include "crawler/infrastructure/music/persistence/mysql/MySqlConfig.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlConnectionPool.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlValueReader.h"
#include "crawler/infrastructure/music/persistence/sql/SqlFileLoader.h"
#include "crawler/infrastructure/platform/Environment.h"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/mysql/any_connection.hpp>
#include <boost/mysql/connect_params.hpp>
#include <boost/mysql/diagnostics.hpp>
#include <boost/mysql/results.hpp>

#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>

namespace orchestrator::modules::crawler::tests::migu::support {

namespace mysql = orchestrator::modules::crawler::infrastructure::music::persistence::mysql;
namespace platform = orchestrator::modules::crawler::infrastructure::platform;
namespace sql_loader = orchestrator::modules::crawler::infrastructure::music::persistence::sql;

// 生成异步测试超时错误，避免测试无输出地挂起。
inline std::runtime_error makeTimeoutError(std::string_view operation, std::chrono::seconds timeout)
{
    return std::runtime_error(std::string(operation) + " timed out after " + std::to_string(timeout.count()) + " seconds");
}

// 读取 MySQL 集成测试超时时间。
inline std::chrono::seconds mysqlOperationTimeout()
{
    return std::chrono::seconds(platform::Environment::getIntOr("CRAWLER_TEST_MYSQL_TIMEOUT_SECONDS", 30));
}

// 在连接池 executor 上同步等待协程完成。
template <typename T>
T runOnExecutor(boost::asio::any_io_executor executor, boost::asio::awaitable<T> task, std::string_view operation, std::chrono::seconds timeout = mysqlOperationTimeout())
{
    std::future<T> result = boost::asio::co_spawn(std::move(executor), std::move(task), boost::asio::use_future);
    if (result.wait_for(timeout) != std::future_status::ready) {
        throw makeTimeoutError(operation, timeout);
    }
    return result.get();
}

// 在临时 io_context 中同步运行协程，主要用于连接池创建前的直连探测。
template <typename T>
T runInIoContext(boost::asio::io_context &io_context, boost::asio::awaitable<T> task, std::string_view operation, std::chrono::seconds timeout = mysqlOperationTimeout())
{
    std::future<T> result = boost::asio::co_spawn(io_context, std::move(task), boost::asio::use_future);
    std::thread runner([&io_context] {
        io_context.run();
    });

    auto stopAndJoin = [&io_context, &runner] {
        io_context.stop();
        if (runner.joinable()) {
            runner.join();
        }
    };

    if (result.wait_for(timeout) != std::future_status::ready) {
        stopAndJoin();
        throw makeTimeoutError(operation, timeout);
    }

    try {
        T value = result.get();
        stopAndJoin();
        return value;
    } catch (...) {
        stopAndJoin();
        throw;
    }
}

// 从 .env.test 或 .env 读取 MySQL 测试配置。
inline mysql::MySqlConfig makeMySqlConfigFromEnv()
{
    platform::Environment::loadDotEnvFile(".env.test");
    platform::Environment::loadDotEnvFile(".env");

    mysql::MySqlConfig config;
    config.host = platform::Environment::getStringOr("CRAWLER_MYSQL_HOST", config.host);
    config.port = platform::Environment::getPortOr("CRAWLER_MYSQL_PORT", config.port);
    config.username = platform::Environment::getStringOr("CRAWLER_MYSQL_USER", config.username);
    config.password = platform::Environment::getStringOr("CRAWLER_MYSQL_PASSWORD", config.password);
    config.database = platform::Environment::getStringOr("CRAWLER_MYSQL_DATABASE", config.database);
    config.migrations_path = platform::Environment::getStringOr("CRAWLER_MYSQL_MIGRATIONS_PATH", config.migrations_path);
    config.initialize_schema = platform::Environment::getBoolOr("CRAWLER_MYSQL_INIT_SCHEMA", true);
    config.io_threads = platform::Environment::getIntOr("CRAWLER_MYSQL_IO_THREADS", 2);
    config.connect_timeout = std::chrono::seconds(platform::Environment::getIntOr("CRAWLER_MYSQL_CONNECT_TIMEOUT_SECONDS", 5));
    config.retry_interval = std::chrono::seconds(platform::Environment::getIntOr("CRAWLER_MYSQL_RETRY_INTERVAL_SECONDS", 2));
    config.ping_timeout = std::chrono::seconds(platform::Environment::getIntOr("CRAWLER_MYSQL_PING_TIMEOUT_SECONDS", 5));
    config.initial_pool_size = 1;
    config.max_pool_size = static_cast<std::size_t>(platform::Environment::getIntOr("CRAWLER_MYSQL_MAX_POOL_SIZE", 4));
    return config;
}

// 拼接 Boost.MySQL 诊断信息，避免丢失服务端返回的认证或数据库错误。
inline std::string diagnosticsText(const boost::mysql::diagnostics &diagnostics)
{
    if (!diagnostics.server_message().empty()) {
        return std::string(diagnostics.server_message());
    }
    if (!diagnostics.client_message().empty()) {
        return std::string(diagnostics.client_message());
    }
    return {};
}

// 使用普通连接先探测 MySQL 凭据和数据库是否可用，避免连接池在错误凭据下持续重试。
inline boost::asio::awaitable<bool> probeDirectMySql(mysql::MySqlConfig config)
{
    auto executor = co_await boost::asio::this_coro::executor;
    boost::mysql::any_connection connection(executor);

    boost::mysql::connect_params params;
    params.server_address = boost::mysql::host_and_port {config.host, config.port};
    params.username = config.username;
    params.password = config.password;
    params.database = config.database;
    params.ssl = config.ssl_mode;

    boost::mysql::diagnostics diagnostics;
    try {
        co_await connection.async_connect(params, diagnostics, boost::asio::use_awaitable);

        boost::mysql::results result;
        co_await connection.async_execute("SELECT 1 AS probe", result, boost::asio::use_awaitable);
        co_await connection.async_close(boost::asio::use_awaitable);

        if (result.rows().empty() || mysql::MySqlValueReader::uint64At(result.rows().front(), 0) != 1) {
            throw std::runtime_error("direct MySQL SELECT 1 probe returned unexpected result");
        }
    } catch (const std::exception &e) {
        const std::string details = diagnosticsText(diagnostics);
        if (!details.empty()) {
            throw std::runtime_error(std::string("direct MySQL probe failed: ") + e.what() + " (" + details + ")");
        }
        throw std::runtime_error(std::string("direct MySQL probe failed: ") + e.what());
    }

    co_return true;
}

// 验证 MySQL 凭据、数据库和基础查询可用。
inline void assertDirectMySqlAvailable(const mysql::MySqlConfig &config)
{
    std::cout << "[Check] Probing MySQL " << config.host << ":" << config.port << "/" << config.database << " as " << config.username << std::endl;
    boost::asio::io_context io_context;
    const bool ok = runInIoContext(io_context, probeDirectMySql(config), "direct MySQL probe", mysqlOperationTimeout());
    if (!ok) {
        throw std::runtime_error("direct MySQL probe returned false");
    }
    std::cout << "[Check] Direct MySQL probe OK." << std::endl;
}

// 执行无参数标量查询模板，用于连接探测。
inline boost::asio::awaitable<std::uint64_t> executeScalar(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, std::string sql_path)
{
    boost::mysql::results result = co_await pool->coExecute(sql_loader::SqlFileLoader::loadOrThrow(sql_path));
    if (result.rows().empty()) {
        co_return 0;
    }
    co_return mysql::MySqlValueReader::uint64At(result.rows().front(), 0);
}

// 验证 MySQL 可以连接、schema 可以初始化，并能执行基础查询。
inline void assertMySqlAvailable(const std::shared_ptr<mysql::MySqlConnectionPool> &pool)
{
    const bool schema_ok = runOnExecutor(pool->executor(), pool->coInitializeSchema(), "MySQL schema initialization", mysqlOperationTimeout());
    if (!schema_ok) {
        pool->cancel();
        throw std::runtime_error("MySQL schema initialization failed");
    }

    const std::uint64_t probe = runOnExecutor(pool->executor(), executeScalar(pool, "sql/mysql/tests/test_probe_mysql.sql"), "MySQL pooled SELECT 1 probe", mysqlOperationTimeout());
    if (probe != 1) {
        pool->cancel();
        throw std::runtime_error("MySQL SELECT 1 probe failed");
    }
    std::cout << "[Check] MySQL connection OK, schema initialized." << std::endl;
}

// 创建并初始化 MySQL 连接池。
inline std::shared_ptr<mysql::MySqlConnectionPool> makeInitializedPool()
{
    auto pool = std::make_shared<mysql::MySqlConnectionPool>(makeMySqlConfigFromEnv());
    const bool ok = runOnExecutor(pool->executor(), pool->coInitializeSchema(), "MySQL schema initialization");
    if (!ok) {
        pool->cancel();
        throw std::runtime_error("MySQL schema initialization failed");
    }
    return pool;
}

} // namespace orchestrator::modules::crawler::tests::migu::support
