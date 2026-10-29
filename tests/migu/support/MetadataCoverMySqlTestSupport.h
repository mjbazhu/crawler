#pragma once

#include "migu/support/MySqlTestSupport.h"

#include <algorithm>
#include <cctype>

#include <memory>
#include <stdexcept>
#include <string>

namespace orchestrator::modules::crawler::tests::migu::support {

// 数据库名仅允许安全标识符，供测试专属 CREATE/跨库只读查询使用。
inline bool safeCoverDatabaseName(const std::string &name)
{
    return !name.empty() && name.size() <= 64 && std::all_of(name.begin(), name.end(), [](unsigned char ch) { return std::isalnum(ch) || ch == '_'; });
}

// 只创建已验证的独立测试库；凭据不进入日志和命令行。
inline boost::asio::awaitable<bool> createCoverTestDatabase(mysql::MySqlConfig config)
{
    if (!config.database.starts_with("crawler_ownership_test_") || !safeCoverDatabaseName(config.database)) throw std::invalid_argument("refusing non-isolated cover database");
    const auto executor = co_await boost::asio::this_coro::executor;
    boost::mysql::any_connection connection(executor);
    boost::mysql::connect_params params;
    params.server_address = boost::mysql::host_and_port {config.host, config.port};
    params.username = config.username;
    params.password = config.password;
    params.ssl = config.ssl_mode;
    co_await connection.async_connect(params, boost::asio::use_awaitable);
    boost::mysql::results result;
    co_await connection.async_execute("CREATE DATABASE IF NOT EXISTS `" + config.database + "` CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci", result, boost::asio::use_awaitable);
    co_await connection.async_close(boost::asio::use_awaitable);
    co_return true;
}

// 从有效配置创建新测试库，再通过正式 migration 初始化。
inline std::shared_ptr<mysql::MySqlConnectionPool> makeCoverTestPool()
{
    const auto config = makeMySqlConfigFromEnv();
    boost::asio::io_context context;
    if (!runInIoContext(context, createCoverTestDatabase(config), "create isolated cover database")) throw std::runtime_error("cover test database creation failed");
    return makeInitializedPool();
}

} // namespace orchestrator::modules::crawler::tests::migu::support
