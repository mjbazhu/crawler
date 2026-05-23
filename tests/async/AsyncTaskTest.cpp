#include "crawler/application/async/Task.h"
#include "crawler/application/ports/execution/AsyncRuntime.h"
#include "crawler/application/ports/execution/TaskExecutor.h"
#include "crawler/infrastructure/execution/asio/AsioAsyncRuntime.h"
#include "crawler/infrastructure/execution/threading/ThreadPool.h"

#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/io_context.hpp>

#include <condition_variable>
#include <exception>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace crawler = orchestrator::modules::crawler;
namespace app_async = crawler::application::async;
namespace ports = crawler::application::ports;
namespace asio_runtime = crawler::infrastructure::execution::asio;
namespace threading = crawler::infrastructure::execution::threading;

namespace {

// 测试断言失败时抛出明确错误，避免依赖 assert 宏。
void require(bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// 内联 runtime，用于验证 application Task 语义。
class InlineAsyncRuntime : public ports::AsyncRuntime {
public:
    bool spawn(app_async::Task<void> task) override
    {
        task.startDetached();
        return true;
    }

protected:
    void postBlockingCallback(std::function<void()> task) override
    {
        task();
    }
};

// 每个阻塞任务使用独立线程，便于验证 postBlocking 不占用 Asio I/O 线程。
class ThreadedTaskExecutor : public ports::TaskExecutor {
public:
    ~ThreadedTaskExecutor() override
    {
        for (auto &thread : threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }

    void submit(std::function<void()> task) override
    {
        threads_.emplace_back(std::move(task));
    }

private:
    std::vector<std::thread> threads_; // 已提交的阻塞任务线程
};

app_async::Task<int> returnNumber()
{
    co_return 7;
}

app_async::Task<int> nestedNumber()
{
    const int value = co_await returnNumber();
    co_return value + 5;
}

app_async::Task<void> noopTask()
{
    co_return;
}

app_async::Task<int> throwingTask()
{
    throw std::runtime_error("expected task failure");
    co_return 0;
}

app_async::Task<int> waitForExternalValue(app_async::TaskCompletionSource<int> source)
{
    co_return co_await source.task();
}

app_async::Task<void> waitForExternalVoid(app_async::TaskCompletionSource<void> source)
{
    co_await source.task();
}

app_async::Task<void> detachedNoopTask()
{
    co_return;
}

void testTaskReturnsAndNests()
{
    InlineAsyncRuntime runtime;
    require(runtime.runForTest(returnNumber()) == 7, "Task<int> should return value");
    require(runtime.runForTest(nestedNumber()) == 12, "nested co_await should propagate value");
    runtime.runForTest(noopTask());
}

void testTaskPropagatesException()
{
    InlineAsyncRuntime runtime;
    bool threw = false;
    try {
        (void)runtime.runForTest(throwingTask());
    } catch (const std::runtime_error &e) {
        threw = std::string(e.what()).find("expected task failure") != std::string::npos;
    }
    require(threw, "Task exception should propagate through runForTest");
}

void testTaskCompletionSourceResumesLater()
{
    InlineAsyncRuntime runtime;
    app_async::TaskCompletionSource<int> int_source;
    std::thread int_setter([int_source]() mutable {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        int_source.set_value(42);
    });
    require(runtime.runForTest(waitForExternalValue(int_source)) == 42, "TaskCompletionSource<int> should resume waiter");
    int_setter.join();

    app_async::TaskCompletionSource<void> void_source;
    std::thread void_setter([void_source]() mutable {
        std::this_thread::sleep_for(std::chrono::milliseconds(20));
        void_source.set_value();
    });
    runtime.runForTest(waitForExternalVoid(void_source));
    void_setter.join();
}

void testDetachedCompletionRuns()
{
    std::mutex mutex;
    std::condition_variable cv;
    bool completed = false;

    auto task = detachedNoopTask();
    task.startDetached([&mutex, &cv, &completed](std::exception_ptr error) {
        if (error) {
            return;
        }
        {
            std::lock_guard<std::mutex> lock(mutex);
            completed = true;
        }
        cv.notify_one();
    });

    std::unique_lock<std::mutex> lock(mutex);
    require(cv.wait_for(lock, std::chrono::seconds(2), [&completed] {
        return completed;
    }), "detached task completion callback should run");
}

void testPostBlockingUsesBlockingExecutor()
{
    boost::asio::io_context io_context;
    auto guard = boost::asio::make_work_guard(io_context);
    auto blocking_executor = std::make_shared<ThreadedTaskExecutor>();
    auto runtime = std::make_shared<asio_runtime::AsioAsyncRuntime>(io_context.get_executor(), blocking_executor);

    std::thread io_thread([&io_context]() {
        io_context.run();
    });

    auto task = [runtime]() -> app_async::Task<bool> {
        const std::thread::id io_thread_id = std::this_thread::get_id();
        const std::thread::id blocking_thread_id = co_await runtime->postBlocking<std::thread::id>([]() {
            return std::this_thread::get_id();
        });
        co_return blocking_thread_id != io_thread_id;
    };

    const bool separated = runtime->runForTest(task());
    guard.reset();
    io_context.stop();
    if (io_thread.joinable()) {
        io_thread.join();
    }
    require(separated, "postBlocking should execute outside the Asio I/O thread");
}

void testRepeatedPostBlockingVoid()
{
    boost::asio::io_context io_context;
    auto guard = boost::asio::make_work_guard(io_context);
    auto blocking_executor = std::make_shared<ThreadedTaskExecutor>();
    auto runtime = std::make_shared<asio_runtime::AsioAsyncRuntime>(io_context.get_executor(), blocking_executor);

    std::thread io_thread([&io_context]() {
        io_context.run();
    });

    auto task = [runtime]() -> app_async::Task<int> {
        int count = 0;
        for (int i = 0; i < 20; ++i) {
            co_await runtime->postBlocking<void>([&count]() {
                ++count;
            });
        }
        co_return count;
    };

    const int count = runtime->runForTest(task());
    guard.reset();
    io_context.stop();
    if (io_thread.joinable()) {
        io_thread.join();
    }
    require(count == 20, "repeated postBlocking<void> should resume reliably");
}

void testRepeatedPostBlockingVoidWithThreadPool()
{
    boost::asio::io_context io_context;
    auto guard = boost::asio::make_work_guard(io_context);
    auto blocking_executor = std::make_shared<threading::ThreadPool>(4);
    auto runtime = std::make_shared<asio_runtime::AsioAsyncRuntime>(io_context.get_executor(), blocking_executor);

    std::thread io_thread([&io_context]() {
        io_context.run();
    });

    auto task = [runtime]() -> app_async::Task<int> {
        int count = 0;
        for (int i = 0; i < 20; ++i) {
            co_await runtime->postBlocking<void>([&count]() {
                ++count;
            });
        }
        co_return count;
    };

    const int count = runtime->runForTest(task());
    guard.reset();
    io_context.stop();
    if (io_thread.joinable()) {
        io_thread.join();
    }
    require(count == 20, "ThreadPool postBlocking<void> should resume reliably");
}

} // namespace

int main()
{
    try {
        std::cout << "[AsyncTaskTest] returns_and_nests" << std::endl;
        testTaskReturnsAndNests();
        std::cout << "[AsyncTaskTest] exception" << std::endl;
        testTaskPropagatesException();
        std::cout << "[AsyncTaskTest] completion_source" << std::endl;
        testTaskCompletionSourceResumesLater();
        std::cout << "[AsyncTaskTest] detached" << std::endl;
        testDetachedCompletionRuns();
        std::cout << "[AsyncTaskTest] post_blocking" << std::endl;
        testPostBlockingUsesBlockingExecutor();
        std::cout << "[AsyncTaskTest] repeated_post_blocking" << std::endl;
        testRepeatedPostBlockingVoid();
        std::cout << "[AsyncTaskTest] repeated_post_blocking_thread_pool" << std::endl;
        testRepeatedPostBlockingVoidWithThreadPool();
        std::cout << "AsyncTaskTest passed." << std::endl;
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "AsyncTaskTest failed: " << e.what() << std::endl;
        return 1;
    }
}
