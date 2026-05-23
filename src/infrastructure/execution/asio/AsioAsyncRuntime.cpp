#include "crawler/infrastructure/execution/asio/AsioAsyncRuntime.h"

#include <boost/asio/post.hpp>

#include <utility>

namespace orchestrator::modules::crawler::infrastructure::execution::asio {

AsioAsyncRuntime::AsioAsyncRuntime(boost::asio::any_io_executor executor, std::shared_ptr<application::ports::TaskExecutor> blocking_executor) : executor_(std::move(executor)),
                                                                                                                                               blocking_executor_(std::move(blocking_executor))
{}

bool AsioAsyncRuntime::spawn(application::async::Task<void> task)
{
    try {
        boost::asio::post(executor_, [task = std::move(task)]() mutable {
            task.startDetached();
        });
        return true;
    } catch (...) {
        return false;
    }
}

void AsioAsyncRuntime::postBlockingCallback(std::function<void()> task)
{
    if (blocking_executor_) {
        blocking_executor_->submit(std::move(task));
        return;
    }
    boost::asio::post(executor_, std::move(task));
}

void AsioAsyncRuntime::postCompletionCallback(std::function<void()> task)
{
    boost::asio::post(executor_, std::move(task));
}

} // namespace orchestrator::modules::crawler::infrastructure::execution::asio
