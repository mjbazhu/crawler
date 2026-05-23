#pragma once

#include "crawler/application/ports/execution/TaskExecutor.h"

#include <condition_variable>
#include <functional>
#include <future>
#include <memory>
#include <mutex>
#include <queue>
#include <stdexcept>
#include <thread>
#include <type_traits>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::execution::threading {

class ThreadPool : public application::ports::TaskExecutor {
public:
    explicit ThreadPool(size_t threads);
    ~ThreadPool() override;

    // 禁止拷贝
    ThreadPool(const ThreadPool &) = delete;
    ThreadPool &operator=(const ThreadPool &) = delete;

    // 提交任务到线程池
    // 返回 std::future 以获取结果
    template <class F, class... Args>
    auto enqueue(F &&f, Args &&...args)
        -> std::future<typename std::invoke_result<F, Args...>::type>;

    void submit(std::function<void()> task) override;

private:
    // 工作线程列表
    std::vector<std::thread> workers_;
    // 任务队列
    std::queue<std::function<void()>> tasks_;

    // 同步原语
    std::mutex queue_mutex_;
    std::condition_variable condition_;
    bool stop_;
};

// 模板实现需放在头文件
template <class F, class... Args>
auto ThreadPool::enqueue(F &&f, Args &&...args)
    -> std::future<typename std::invoke_result<F, Args...>::type>
{
    using return_type = typename std::invoke_result<F, Args...>::type;

    auto task = std::make_shared<std::packaged_task<return_type()>>(
        std::bind(std::forward<F>(f), std::forward<Args>(args)...)
    );

    std::future<return_type> res = task->get_future();
    {
        std::unique_lock<std::mutex> lock(queue_mutex_);
        if (stop_)
            throw std::runtime_error("enqueue on stopped ThreadPool");

        tasks_.emplace([task]() { (*task)(); });
    }
    condition_.notify_one();
    return res;
}

} // namespace orchestrator::modules::crawler::infrastructure::execution::threading
