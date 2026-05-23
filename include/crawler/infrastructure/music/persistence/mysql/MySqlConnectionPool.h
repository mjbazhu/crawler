#pragma once

#include "crawler/infrastructure/music/persistence/mysql/MySqlConfig.h"

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/mysql/connection_pool.hpp>
#include <boost/mysql/results.hpp>

#include <memory>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {

// 基于 Boost.MySQL connection_pool 的 MySQL 连接池。
// 线程安全：当 MySqlConfig::thread_safe 为 true 时，可被多个业务线程并发 coAcquire/coExecute。
class MySqlConnectionPool {
public:
    // 使用配置创建连接池，并启动内部 Asio I/O 线程。
    explicit MySqlConnectionPool(MySqlConfig config);

    // 停止连接池并等待内部 I/O 线程退出。
    ~MySqlConnectionPool();

    // 连接池不允许拷贝，避免多个对象管理同一批连接。
    MySqlConnectionPool(const MySqlConnectionPool &) = delete;
    MySqlConnectionPool &operator=(const MySqlConnectionPool &) = delete;

    // 连接池不允许移动，避免协程运行期间底层执行器位置变化。
    MySqlConnectionPool(MySqlConnectionPool &&) = delete;
    MySqlConnectionPool &operator=(MySqlConnectionPool &&) = delete;

    // 返回内部执行器，供上层 co_spawn MySQL 协程任务。
    boost::asio::any_io_executor executor();

    // 异步获取独占连接，连接析构后自动归还连接池。
    boost::asio::awaitable<boost::mysql::pooled_connection> coAcquire();

    // 按配置执行外置 schema SQL；多条语句会拆分后顺序异步执行。
    boost::asio::awaitable<bool> coInitializeSchema();

    // 异步执行 SQL 请求，支持 std::string、string_view、boost::mysql::with_params 等 ExecutionRequest。
    template <typename ExecutionRequest>
    boost::asio::awaitable<boost::mysql::results> coExecute(ExecutionRequest request)
    {
        auto connection = co_await coAcquire();
        boost::mysql::results result;
        co_await connection->async_execute(std::move(request), result, boost::asio::use_awaitable);
        co_return result;
    }

    // 请求连接池停止，正在等待连接的协程会被取消。
    void cancel();

private:
    // 隐藏 Boost.MySQL 与 Asio 线程管理细节。
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
