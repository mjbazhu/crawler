#pragma once

#include <condition_variable>
#include <coroutine>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <optional>
#include <utility>

namespace orchestrator::modules::crawler::application::async {

namespace detail {

// detached Task 的清理续体，在原协程 final_suspend 完成后销毁原协程帧。
template <typename Handle>
class DetachedDestroyTask {
public:
    struct promise_type {
        DetachedDestroyTask get_return_object()
        {
            return DetachedDestroyTask {std::coroutine_handle<promise_type>::from_promise(*this)};
        }

        std::suspend_always initial_suspend() noexcept
        {
            return {};
        }

        std::suspend_never final_suspend() noexcept
        {
            return {};
        }

        void return_void() noexcept
        {}

        void unhandled_exception() noexcept
        {}
    };

    explicit DetachedDestroyTask(std::coroutine_handle<promise_type> handle) : handle_(handle)
    {}

    DetachedDestroyTask(const DetachedDestroyTask &) = delete;
    DetachedDestroyTask &operator=(const DetachedDestroyTask &) = delete;

    DetachedDestroyTask(DetachedDestroyTask &&other) noexcept : handle_(std::exchange(other.handle_, {}))
    {}

    ~DetachedDestroyTask()
    {
        if (handle_) {
            handle_.destroy();
        }
    }

    std::coroutine_handle<> release() noexcept
    {
        return std::exchange(handle_, {});
    }

private:
    std::coroutine_handle<promise_type> handle_; // cleanup 协程帧
};

// 创建一个 cleanup 协程句柄，先销毁已完成协程帧，再执行 detached 完成回调。
template <typename Handle>
DetachedDestroyTask<Handle> destroyAfterFinalSuspend(Handle handle, std::function<void(std::exception_ptr)> completion, std::exception_ptr exception)
{
    handle.destroy();
    try {
        if (completion) {
            completion(exception);
        }
    } catch (...) {
    }
    co_return;
}

// 返回 cleanup 续体；调用方必须立即返回该句柄，不能恢复当前协程。
template <typename Handle>
std::coroutine_handle<> detachedDestroyContinuation(Handle handle, std::function<void(std::exception_ptr)> completion, std::exception_ptr exception) noexcept
{
    return destroyAfterFinalSuspend(handle, std::move(completion), exception).release();
}

} // namespace detail

// application 层轻量协程任务；不绑定任何具体 I/O runtime。
template <typename T>
class Task {
public:
    struct promise_type;
    using Handle = std::coroutine_handle<promise_type>;

    // 保存协程句柄；Task 为 move-only，避免多个所有者重复销毁协程帧。
    explicit Task(Handle handle = {}) : handle_(handle)
    {}

    Task(const Task &) = delete;
    Task &operator=(const Task &) = delete;

    Task(Task &&other) noexcept : handle_(std::exchange(other.handle_, {}))
    {}

    Task &operator=(Task &&other) noexcept
    {
        if (this != &other) {
            destroy();
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }

    ~Task()
    {
        destroy();
    }

    // 当前线程启动任务；通常由 AsyncRuntime::spawn 调用。
    void start()
    {
        if (handle_ && !handle_.done()) {
            handle_.resume();
        }
    }

    // 后台启动任务并在完成时回调；异常通过回调参数传递。
    void startDetached(std::function<void(std::exception_ptr)> on_complete = {})
    {
        Handle handle = std::exchange(handle_, {});
        if (!handle) {
            return;
        }
        handle.promise().completion = [on_complete = std::move(on_complete)](std::exception_ptr error) mutable {
            try {
                if (on_complete) {
                    on_complete(error);
                }
            } catch (...) {
            }
        };
        if (!handle.done()) {
            handle.resume();
        }
    }

    // co_await 支持嵌套 application Task。
    auto operator co_await() noexcept
    {
        struct Awaiter {
            Handle handle; // 被等待任务的协程句柄，由 Awaiter 接管销毁

            bool await_ready() const noexcept
            {
                return !handle || handle.done();
            }

            std::coroutine_handle<> await_suspend(std::coroutine_handle<> continuation) noexcept
            {
                handle.promise().continuation = continuation;
                return handle;
            }

            T await_resume()
            {
                if (handle.promise().exception) {
                    std::rethrow_exception(handle.promise().exception);
                }
                return std::move(*handle.promise().value);
            }

            ~Awaiter()
            {
                if (handle) {
                    handle.destroy();
                }
            }
        };
        return Awaiter {std::exchange(handle_, {})};
    }

    struct promise_type {
        std::optional<T> value;                                  // 协程返回值
        std::exception_ptr exception;                            // 未处理异常
        std::coroutine_handle<> continuation;                    // 等待当前任务的父协程
        std::function<void(std::exception_ptr)> completion;      // detached 任务完成回调

        Task get_return_object()
        {
            return Task {Handle::from_promise(*this)};
        }

        std::suspend_always initial_suspend() noexcept
        {
            return {};
        }

        auto final_suspend() noexcept
        {
            struct FinalAwaiter {
                bool await_ready() noexcept
                {
                    return false;
                }

                std::coroutine_handle<> await_suspend(Handle handle) noexcept
                {
                    auto &promise = handle.promise();
                    if (promise.completion) {
                        auto completion = std::move(promise.completion);
                        auto exception = promise.exception;
                        return detail::detachedDestroyContinuation(handle, std::move(completion), exception);
                    }
                    if (promise.continuation) {
                        return promise.continuation;
                    }
                    return std::noop_coroutine();
                }

                void await_resume() noexcept
                {}
            };
            return FinalAwaiter {};
        }

        template <typename U>
        void return_value(U &&result)
        {
            value.emplace(std::forward<U>(result));
        }

        void unhandled_exception()
        {
            exception = std::current_exception();
        }
    };

private:
    void destroy()
    {
        if (handle_) {
            handle_.destroy();
            handle_ = {};
        }
    }

    Handle handle_; // 当前 Task 拥有的协程句柄
};

// void 特化，保留异常传播和嵌套 co_await 语义。
template <>
class Task<void> {
public:
    struct promise_type;
    using Handle = std::coroutine_handle<promise_type>;

    explicit Task(Handle handle = {}) : handle_(handle)
    {}

    Task(const Task &) = delete;
    Task &operator=(const Task &) = delete;

    Task(Task &&other) noexcept : handle_(std::exchange(other.handle_, {}))
    {}

    Task &operator=(Task &&other) noexcept
    {
        if (this != &other) {
            destroy();
            handle_ = std::exchange(other.handle_, {});
        }
        return *this;
    }

    ~Task()
    {
        destroy();
    }

    void start()
    {
        if (handle_ && !handle_.done()) {
            handle_.resume();
        }
    }

    void startDetached(std::function<void(std::exception_ptr)> on_complete = {})
    {
        Handle handle = std::exchange(handle_, {});
        if (!handle) {
            return;
        }
        handle.promise().completion = [on_complete = std::move(on_complete)](std::exception_ptr error) mutable {
            try {
                if (on_complete) {
                    on_complete(error);
                }
            } catch (...) {
            }
        };
        if (!handle.done()) {
            handle.resume();
        }
    }

    auto operator co_await() noexcept
    {
        struct Awaiter {
            Handle handle; // 被等待任务的协程句柄，由 Awaiter 接管销毁

            bool await_ready() const noexcept
            {
                return !handle || handle.done();
            }

            std::coroutine_handle<> await_suspend(std::coroutine_handle<> continuation) noexcept
            {
                handle.promise().continuation = continuation;
                return handle;
            }

            void await_resume()
            {
                if (handle.promise().exception) {
                    std::rethrow_exception(handle.promise().exception);
                }
            }

            ~Awaiter()
            {
                if (handle) {
                    handle.destroy();
                }
            }
        };
        return Awaiter {std::exchange(handle_, {})};
    }

    struct promise_type {
        std::exception_ptr exception;                       // 未处理异常
        std::coroutine_handle<> continuation;               // 等待当前任务的父协程
        std::function<void(std::exception_ptr)> completion; // detached 任务完成回调

        Task get_return_object()
        {
            return Task {Handle::from_promise(*this)};
        }

        std::suspend_always initial_suspend() noexcept
        {
            return {};
        }

        auto final_suspend() noexcept
        {
            struct FinalAwaiter {
                bool await_ready() noexcept
                {
                    return false;
                }

                std::coroutine_handle<> await_suspend(Handle handle) noexcept
                {
                    auto &promise = handle.promise();
                    if (promise.completion) {
                        auto completion = std::move(promise.completion);
                        auto exception = promise.exception;
                        return detail::detachedDestroyContinuation(handle, std::move(completion), exception);
                    }
                    if (promise.continuation) {
                        return promise.continuation;
                    }
                    return std::noop_coroutine();
                }

                void await_resume() noexcept
                {}
            };
            return FinalAwaiter {};
        }

        void return_void() noexcept
        {}

        void unhandled_exception()
        {
            exception = std::current_exception();
        }
    };

private:
    void destroy()
    {
        if (handle_) {
            handle_.destroy();
            handle_ = {};
        }
    }

    Handle handle_; // 当前 Task 拥有的协程句柄
};

namespace detail {

// TaskCompletionSource 共享状态，允许外部回调恢复等待它的协程。
template <typename T>
struct CompletionState {
    std::mutex mutex;                       // 保护完成状态
    bool ready = false;                     // 是否已有结果
    std::optional<T> value;                 // 成功结果
    std::exception_ptr exception;           // 失败异常
    std::coroutine_handle<> continuation;   // 等待完成的协程
};

template <>
struct CompletionState<void> {
    std::mutex mutex;                     // 保护完成状态
    bool ready = false;                   // 是否已完成
    std::exception_ptr exception;         // 失败异常
    std::coroutine_handle<> continuation; // 等待完成的协程
};

// 根据共享状态创建可等待 Task；state 作为参数进入协程帧，避免成员协程悬垂 this。
template <typename T>
Task<T> completionTask(std::shared_ptr<CompletionState<T>> state)
{
    struct Awaitable {
        std::shared_ptr<CompletionState<T>> state; // 等待的共享状态

        bool await_ready() const
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            return state->ready;
        }

        void await_suspend(std::coroutine_handle<> continuation)
        {
            bool resume_now = false;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (state->ready) {
                    resume_now = true;
                } else {
                    state->continuation = continuation;
                }
            }
            if (resume_now) {
                continuation.resume();
            }
        }

        T await_resume()
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (state->exception) {
                std::rethrow_exception(state->exception);
            }
            return std::move(*state->value);
        }
    };
    co_return co_await Awaitable {std::move(state)};
}

// void 版本的共享状态等待 Task。
inline Task<void> completionTask(std::shared_ptr<CompletionState<void>> state)
{
    struct Awaitable {
        std::shared_ptr<CompletionState<void>> state; // 等待的共享状态

        bool await_ready() const
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            return state->ready;
        }

        void await_suspend(std::coroutine_handle<> continuation)
        {
            bool resume_now = false;
            {
                std::lock_guard<std::mutex> lock(state->mutex);
                if (state->ready) {
                    resume_now = true;
                } else {
                    state->continuation = continuation;
                }
            }
            if (resume_now) {
                continuation.resume();
            }
        }

        void await_resume()
        {
            std::lock_guard<std::mutex> lock(state->mutex);
            if (state->exception) {
                std::rethrow_exception(state->exception);
            }
        }
    };
    co_await Awaitable {std::move(state)};
}

} // namespace detail

// 回调转 Task 的桥接对象；用于 infrastructure adapter 把异步完成事件暴露给 application。
template <typename T>
class TaskCompletionSource {
public:
    TaskCompletionSource() : state_(std::make_shared<detail::CompletionState<T>>())
    {}

    // 返回可被 co_await 的任务。
    Task<T> task() const
    {
        return detail::completionTask<T>(state_);
    }

    // 设置成功结果；重复设置会被忽略。
    void set_value(T value) const
    {
        std::coroutine_handle<> continuation;
        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            if (state_->ready) {
                return;
            }
            state_->value.emplace(std::move(value));
            state_->ready = true;
            continuation = state_->continuation;
        }
        if (continuation) {
            continuation.resume();
        }
    }

    // 设置异常结果；重复设置会被忽略。
    void set_exception(std::exception_ptr exception) const
    {
        std::coroutine_handle<> continuation;
        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            if (state_->ready) {
                return;
            }
            state_->exception = exception;
            state_->ready = true;
            continuation = state_->continuation;
        }
        if (continuation) {
            continuation.resume();
        }
    }

private:
    std::shared_ptr<detail::CompletionState<T>> state_; // 共享完成状态
};

template <>
class TaskCompletionSource<void> {
public:
    TaskCompletionSource() : state_(std::make_shared<detail::CompletionState<void>>())
    {}

    Task<void> task() const
    {
        return detail::completionTask(state_);
    }

    void set_value() const
    {
        std::coroutine_handle<> continuation;
        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            if (state_->ready) {
                return;
            }
            state_->ready = true;
            continuation = state_->continuation;
        }
        if (continuation) {
            continuation.resume();
        }
    }

    void set_exception(std::exception_ptr exception) const
    {
        std::coroutine_handle<> continuation;
        {
            std::lock_guard<std::mutex> lock(state_->mutex);
            if (state_->ready) {
                return;
            }
            state_->exception = exception;
            state_->ready = true;
            continuation = state_->continuation;
        }
        if (continuation) {
            continuation.resume();
        }
    }

private:
    std::shared_ptr<detail::CompletionState<void>> state_; // 共享完成状态
};

} // namespace orchestrator::modules::crawler::application::async
