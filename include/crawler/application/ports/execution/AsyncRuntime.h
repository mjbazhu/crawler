#pragma once

#include "crawler/application/async/Task.h"

#include <condition_variable>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <type_traits>
#include <utility>

namespace orchestrator::modules::crawler::application::ports {

namespace detail {

// runForTest 非 void 返回值暂存。
template <typename T>
struct AsyncRunForTestStorage {
    std::optional<T> value; // Task 成功返回值

    void set(T result)
    {
        value.emplace(std::move(result));
    }

    T take()
    {
        return std::move(*value);
    }
};

// runForTest void 返回值暂存，避免实例化 std::optional<void>。
template <>
struct AsyncRunForTestStorage<void> {
    void set() {}
    void take() {}
};

} // namespace detail

// application 异步 runtime port，只表达任务启动和阻塞任务隔离，不暴露 Asio/Drogon。
class AsyncRuntime {
public:
    virtual ~AsyncRuntime() = default;

    // 启动一个后台 Task；返回 false 表示无法提交。
    virtual bool spawn(application::async::Task<void> task) = 0;

    // 把阻塞函数投递到 blocking executor，完成后恢复等待它的 Task。
    template <typename T, typename F>
    application::async::Task<T> postBlocking(F fn)
    {
        application::async::TaskCompletionSource<T> source;
        auto task = source.task();
        auto storage = std::make_shared<detail::AsyncRunForTestStorage<T>>();
        postBlockingCallback([this, source, fn = std::move(fn), storage]() mutable {
            std::exception_ptr error;
            try {
                if constexpr (std::is_void_v<T>) {
                    fn();
                    storage->set();
                } else {
                    storage->set(fn());
                }
            } catch (...) {
                error = std::current_exception();
            }
            postCompletionCallback([source, storage, error]() mutable {
                if (error) {
                    source.set_exception(error);
                    return;
                }
                if constexpr (std::is_void_v<T>) {
                    source.set_value();
                } else {
                    source.set_value(storage->take());
                }
            });
        });
        return task;
    }

    // 测试辅助：同步运行一个 Task 并返回结果。
    template <typename T>
    T runForTest(application::async::Task<T> task)
    {
        std::mutex mutex;
        std::condition_variable cv;
        bool done = false;
        detail::AsyncRunForTestStorage<T> storage;
        std::exception_ptr error;
        auto wrapper = [task = std::move(task), &mutex, &cv, &done, &storage, &error]() mutable -> application::async::Task<void> {
            try {
                if constexpr (std::is_void_v<T>) {
                    co_await task;
                    storage.set();
                } else {
                    storage.set(co_await task);
                }
            } catch (...) {
                error = std::current_exception();
            }
            {
                std::lock_guard<std::mutex> lock(mutex);
                done = true;
            }
            cv.notify_one();
        };

        if (!spawn(wrapper())) {
            throw std::runtime_error("async runtime spawn failed");
        }

        std::unique_lock<std::mutex> lock(mutex);
        cv.wait(lock, [&done] {
            return done;
        });
        if (error) {
            std::rethrow_exception(error);
        }
        if constexpr (!std::is_void_v<T>) {
            return storage.take();
        }
    }

protected:
    // 具体 runtime 实现阻塞任务提交策略。
    virtual void postBlockingCallback(std::function<void()> task) = 0;

    // 阻塞任务完成后恢复协程的提交策略；默认内联执行，具体 I/O runtime 应投回自己的 executor。
    virtual void postCompletionCallback(std::function<void()> task)
    {
        task();
    }
};

} // namespace orchestrator::modules::crawler::application::ports
