#include "crawler/infrastructure/execution/threading/ThreadPool.h"
#include <iostream>
#include <vector>
#include <chrono>
#include <cassert>
#include <set>
#include <mutex>
#include <atomic>

namespace crawler = orchestrator::modules::crawler;
using crawler::infrastructure::execution::threading::ThreadPool;

// 测试 1: 基础任务调度与返回值获取
void test_basic_scheduling()
{
    // 创建包含 2 个线程的池
    ThreadPool pool(2);

    // 提交一个简单的计算任务
    auto future = pool.enqueue([](int a, int b) {
        return a * b;
    },
                               6,
                               7);

    // 阻塞等待结果
    int result = future.get();

    // 验证结果正确性
    assert(result == 42);

    std::cout << "[Pass] Basic Scheduling: Result is " << result << std::endl;
}

// 测试 2: 并发执行能力验证
void test_concurrency()
{
    const int num_threads = 4;
    const int num_tasks = 20;
    ThreadPool pool(num_threads);

    std::mutex mtx;
    std::set<std::thread::id> thread_ids;
    std::atomic<int> completed_tasks {0};

    std::vector<std::future<void>> results;

    // 提交多个任务，每个任务睡眠一小段时间以强制并发
    for (int i = 0; i < num_tasks; ++i) {
        results.emplace_back(pool.enqueue([&] {
            // 模拟耗时操作 (10ms)
            std::this_thread::sleep_for(std::chrono::milliseconds(10));

            // 记录执行该任务的线程 ID
            {
                std::lock_guard<std::mutex> lock(mtx);
                thread_ids.insert(std::this_thread::get_id());
            }
            completed_tasks++;
        }));
    }

    // 等待所有任务完成
    for (auto &res : results) {
        res.get();
    }

    // 验证所有任务都已执行
    assert(completed_tasks == num_tasks);

    // 验证使用了多个线程 (线程池大小为 4，应该不止 1 个线程在工作)
    // 注意：在极端调度情况下可能少于 4，但对于 20 个任务，通常应大于 1
    assert(thread_ids.size() > 1);
    assert(thread_ids.size() <= num_threads);

    std::cout << "[Pass] Concurrency: Processed " << num_tasks
              << " tasks using " << thread_ids.size() << " threads." << std::endl;
}

// 测试 3: 异常传播机制
void test_exception_propagation()
{
    ThreadPool pool(2);

    auto future = pool.enqueue([] {
        throw std::runtime_error("Intentional Error");
    });

    try {
        future.get();
        assert(false && "Should have thrown exception");
    } catch (const std::runtime_error &e) {
        assert(std::string(e.what()) == "Intentional Error");
        std::cout << "[Pass] Exception Propagation: Caught expected exception." << std::endl;
    }
}

int main()
{
    std::cout << "Starting ThreadPool Unit Tests..." << std::endl;

    test_basic_scheduling();
    test_concurrency();
    test_exception_propagation();

    std::cout << "All tests passed successfully." << std::endl;
    return 0;
}
