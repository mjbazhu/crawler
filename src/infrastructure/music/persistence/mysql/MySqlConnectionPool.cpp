#include "crawler/infrastructure/music/persistence/mysql/MySqlConnectionPool.h"

#include "crawler/infrastructure/music/persistence/mysql/MySqlMigrationRunner.h"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/detached.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/mysql/any_address.hpp>
#include <boost/mysql/error_code.hpp>
#include <boost/mysql/pool_params.hpp>

#include <algorithm>
#include <exception>
#include <iostream>
#include <optional>
#include <thread>
#include <utility>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {

namespace {

// 将模块配置转换为 Boost.MySQL 的连接池参数。
boost::mysql::pool_params makePoolParams(const MySqlConfig &config)
{
    boost::mysql::pool_params params;
    params.server_address = boost::mysql::host_and_port {config.host, config.port};
    params.username = config.username;
    params.password = config.password;
    params.database = config.database;
    params.ssl = config.ssl_mode;
    params.initial_size = config.initial_pool_size;
    params.max_size = (std::max)(config.initial_pool_size, config.max_pool_size);
    params.connect_timeout = config.connect_timeout;
    params.retry_interval = config.retry_interval;
    params.ping_interval = config.ping_interval;
    params.ping_timeout = config.ping_timeout;
    params.thread_safe = config.thread_safe;
    return params;
}

} // namespace

class MySqlConnectionPool::Impl {
public:
    // 初始化 Boost.MySQL 连接池，并启动池维护协程和 I/O 线程。
    explicit Impl(MySqlConfig config) : config_(std::move(config)),
                                        io_context_((std::max)(1, config_.io_threads)),
                                        work_guard_(boost::asio::make_work_guard(io_context_)),
                                        pool_(std::make_unique<boost::mysql::connection_pool>(io_context_.get_executor(), makePoolParams(config_)))
    {
        pool_->async_run([](boost::mysql::error_code ec) {
            if (ec) {
                std::cerr << "[MySqlConnectionPool] pool stopped with error: " << ec.message() << std::endl;
            }
        });

        const int thread_count = (std::max)(1, config_.io_threads);
        threads_.reserve(static_cast<std::size_t>(thread_count));
        for (int i = 0; i < thread_count; ++i) {
            threads_.emplace_back([this] {
                try {
                    io_context_.run();
                } catch (const std::exception &e) {
                    std::cerr << "[MySqlConnectionPool] io thread exception: " << e.what() << std::endl;
                }
            });
        }
    }

    // 停止连接池和 I/O 线程。
    ~Impl()
    {
        stop();
    }

    // 返回连接池执行器。
    boost::asio::any_io_executor executor()
    {
        return io_context_.get_executor();
    }

    // 异步获取一个独占连接。
    boost::asio::awaitable<boost::mysql::pooled_connection> coAcquire()
    {
        auto connection = co_await pool_->async_get_connection(boost::asio::use_awaitable);
        co_return std::move(connection);
    }

    // 执行 MySQL migration，避免建表和索引 SQL 散落在 C++ 内部。
    boost::asio::awaitable<bool> coInitializeSchema()
    {
        if (!config_.initialize_schema) {
            co_return true;
        }

        try {
            auto connection = co_await coAcquire();
            const bool ok = co_await MySqlMigrationRunner::coRun(connection, config_.migrations_path);
            co_return ok;
        } catch (const std::exception &e) {
            std::cerr << "[MySqlConnectionPool] initialize schema failed: " << e.what() << std::endl;
            co_return false;
        }
    }

    // 取消连接池，等待线程退出。
    void stop()
    {
        if (stopped_) {
            return;
        }
        stopped_ = true;

        if (pool_) {
            pool_->cancel();
        }
        work_guard_.reset();
        io_context_.stop();

        for (std::thread &thread : threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
        threads_.clear();
    }

private:
    MySqlConfig config_;                                                                  // 原始连接配置
    boost::asio::io_context io_context_;                                                  // MySQL 异步 I/O 上下文
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_guard_; // 防止 io_context 过早退出
    std::unique_ptr<boost::mysql::connection_pool> pool_;                                 // Boost.MySQL 原生连接池
    std::vector<std::thread> threads_;                                                    // 内部 I/O 线程
    bool stopped_ = false;                                                                // 停止标记
};

MySqlConnectionPool::MySqlConnectionPool(MySqlConfig config) : impl_(std::make_unique<Impl>(std::move(config)))
{}

MySqlConnectionPool::~MySqlConnectionPool() = default;

boost::asio::any_io_executor MySqlConnectionPool::executor()
{
    return impl_->executor();
}

boost::asio::awaitable<boost::mysql::pooled_connection> MySqlConnectionPool::coAcquire()
{
    auto connection = co_await impl_->coAcquire();
    co_return std::move(connection);
}

boost::asio::awaitable<bool> MySqlConnectionPool::coInitializeSchema()
{
    co_return co_await impl_->coInitializeSchema();
}

void MySqlConnectionPool::cancel()
{
    impl_->stop();
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
