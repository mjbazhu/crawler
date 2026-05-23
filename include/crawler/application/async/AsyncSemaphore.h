#pragma once

#include "crawler/application/async/Task.h"

#include <algorithm>
#include <cstddef>
#include <mutex>
#include <queue>

namespace orchestrator::modules::crawler::application::async {

// 协程信号量，用于限制同一批次内资源下载并发。
class AsyncSemaphore {
public:
    explicit AsyncSemaphore(std::size_t permits) : permits_((std::max<std::size_t>)(1, permits))
    {}

    // 获取一个许可；没有可用许可时挂起当前 Task。
    Task<void> acquire()
    {
        TaskCompletionSource<void> waiter;
        bool acquired = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (permits_ > 0) {
                --permits_;
                acquired = true;
            } else {
                waiters_.push(waiter);
            }
        }
        if (!acquired) {
            co_await waiter.task();
        }
    }

    // 释放一个许可，并恢复一个等待者。
    void release()
    {
        TaskCompletionSource<void> waiter;
        bool has_waiter = false;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            if (!waiters_.empty()) {
                waiter = waiters_.front();
                waiters_.pop();
                has_waiter = true;
            } else {
                ++permits_;
            }
        }
        if (has_waiter) {
            waiter.set_value();
        }
    }

private:
    std::mutex mutex_;                                // 保护许可和等待队列
    std::size_t permits_ = 0;                         // 当前可用许可数
    std::queue<TaskCompletionSource<void>> waiters_;  // 等待许可的协程
};

} // namespace orchestrator::modules::crawler::application::async
