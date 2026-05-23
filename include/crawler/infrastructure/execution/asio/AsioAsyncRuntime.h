#pragma once

#include "crawler/application/ports/execution/AsyncRuntime.h"
#include "crawler/application/ports/execution/TaskExecutor.h"

#include <boost/asio/any_io_executor.hpp>

#include <functional>
#include <memory>

namespace orchestrator::modules::crawler::infrastructure::execution::asio {

// 基于 Boost.Asio executor 的 application AsyncRuntime 实现。
class AsioAsyncRuntime : public application::ports::AsyncRuntime {
public:
    // executor 用于启动非阻塞协程，blocking_executor 用于隔离文件/同步仓储等阻塞调用。
    AsioAsyncRuntime(boost::asio::any_io_executor executor, std::shared_ptr<application::ports::TaskExecutor> blocking_executor);

    // 在 Asio executor 上启动 application Task。
    bool spawn(application::async::Task<void> task) override;

protected:
    // 将阻塞任务投递到独立 blocking executor。
    void postBlockingCallback(std::function<void()> task) override;

    // 将阻塞任务完成通知投回 Asio executor，避免业务协程在 blocking 线程继续运行。
    void postCompletionCallback(std::function<void()> task) override;

private:
    boost::asio::any_io_executor executor_;                                      // Asio 非阻塞执行器
    std::shared_ptr<application::ports::TaskExecutor> blocking_executor_;        // 阻塞任务执行器
};

} // namespace orchestrator::modules::crawler::infrastructure::execution::asio
