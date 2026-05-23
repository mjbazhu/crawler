#include "crawler/infrastructure/execution/asio/AsioExecutionContext.h"

#include <algorithm>

namespace orchestrator::modules::crawler::infrastructure::execution::asio {

AsioExecutionContext::AsioExecutionContext(std::size_t threads)
{
    const std::size_t worker_count = (std::max<std::size_t>)(1, threads);
    work_guard_ = std::make_unique<boost::asio::executor_work_guard<boost::asio::io_context::executor_type>>(boost::asio::make_work_guard(io_context_));
    workers_.reserve(worker_count);
    for (std::size_t i = 0; i < worker_count; ++i) {
        workers_.emplace_back([this] {
            io_context_.run();
        });
    }
}

AsioExecutionContext::~AsioExecutionContext()
{
    work_guard_.reset();
    io_context_.stop();
    for (auto &worker : workers_) {
        if (worker.joinable()) {
            worker.join();
        }
    }
}

boost::asio::any_io_executor AsioExecutionContext::executor()
{
    return io_context_.get_executor();
}

} // namespace orchestrator::modules::crawler::infrastructure::execution::asio
