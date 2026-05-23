#pragma once

#include <string>
#include <atomic>
#include <vector>
#include <mutex>
#include <optional>
#include <chrono>

namespace orchestrator::modules::crawler::application::model {

/**
 * @brief 批量任务状态枚举
 */
enum class BatchState {
    Pending,   // 等待中
    Running,   // 进行中
    Completed, // 已完成
    Failed,    // 任务级失败
    Cancelled  // 已取消
};

/**
 * @brief 批量爬取任务进度追踪对象
 * @note 该对象设计为线程安全，可由 Worker 线程写入，HTTP 线程读取
 */
struct BatchProgress {
    // 任务元数据
    std::string batch_id;
    std::chrono::system_clock::time_point start_time;

    // 核心状态指标 (Atomic for lock-free monitoring)
    std::atomic<BatchState> state {BatchState::Pending};
    std::atomic<int> total_items {0};          // 预计总条目数
    std::atomic<int> processed_count {0};      // 已处理总数
    std::atomic<int> success_count {0};        // 成功数
    std::atomic<int> failure_count {0};        // 失败数
    std::atomic_bool cancel_requested {false}; // 是否请求取消

    // 详细信息 (需锁保护)
    mutable std::mutex mutex_;
    std::string current_item_name;       // 当前正在处理的项目名称
    std::vector<std::string> error_logs; // 最近的错误日志

    BatchProgress() : start_time(std::chrono::system_clock::now())
    {}

    // 禁止拷贝
    BatchProgress(const BatchProgress &) = delete;
    BatchProgress &operator=(const BatchProgress &) = delete;
};

/**
 * @brief 用于 API 返回的只读快照
 */
struct BatchProgressSnapshot {
    std::string batch_id;
    std::string state_str;
    int total;
    int processed;
    int success;
    int failed;
    std::string current_item;
    std::vector<std::string> recent_errors;
    long long duration_seconds;
};

} // namespace orchestrator::modules::crawler::application::model
