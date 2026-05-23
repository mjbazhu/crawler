#pragma once

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>

#include <cstddef>
#include <memory>
#include <thread>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::execution::asio {

// 进程内协程执行上下文，负责运行 application 异步用例和 HTTP handler。
// 线程安全：executor() 可被多线程调用，返回的 executor 可用于 co_spawn。
class AsioExecutionContext {
public:
    explicit AsioExecutionContext(std::size_t threads);
    ~AsioExecutionContext();

    AsioExecutionContext(const AsioExecutionContext &) = delete;
    AsioExecutionContext &operator=(const AsioExecutionContext &) = delete;

    // 返回可调度协程的共享 executor；任务可在多个工作线程上并发恢复。
    boost::asio::any_io_executor executor();

private:
    boost::asio::io_context io_context_; // 协程调度上下文
    std::unique_ptr<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>> work_guard_;
    std::vector<std::thread> workers_; // 运行 io_context 的工作线程
};

} // namespace orchestrator::modules::crawler::infrastructure::execution::asio
