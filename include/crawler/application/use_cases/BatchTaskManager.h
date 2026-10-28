#pragma once

#include "crawler/application/model/BatchProgress.h"
#include "crawler/application/model/CrawlBatch.h"
#include "crawler/application/ports/diagnostics/DiagnosticsSink.h"
#include "crawler/application/ports/execution/TaskExecutor.h"
#include "crawler/application/ports/persistence/BatchTaskRepository.h"
#include "crawler/application/use_cases/TaskRunner.h"

#include <chrono>
#include <functional>
#include <map>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <vector>

namespace orchestrator::modules::crawler::application::use_cases {

// 批量任务管理器，只负责任务状态、取消、超时和进度快照。
class BatchTaskManager {
public:
    // 单个批次目标处理结果，供持久化 item 写入准确数量和错误。
    struct TargetResult {
        bool success = false;                         // 是否成功
        int written_count = 0;                        // 写入或解析数量
        std::string error;                            // 失败原因
        std::string error_category;                   // 失败分类，空表示无失败
        std::string result_code;                      // 结果编码，成功时默认为 ok
        bool retryable = false;                       // 仅明确允许重取的外部响应失败可重试
        std::chrono::milliseconds retry_delay {1000}; // 首次重试等待时间，限流至少等待 20 秒
    };

    using TargetHandler = std::function<TargetResult(std::string, int)>;
    using BatchConcurrencyReader = std::function<int()>;
    using TimeoutReader = std::function<std::chrono::milliseconds()>;

    // 保存批量执行器和运行设置读取回调，便于热更新设置对运行任务生效。
    BatchTaskManager(
        std::shared_ptr<application::ports::TaskExecutor> batch_executor,
        std::shared_ptr<application::ports::DiagnosticsSink> diagnostics,
        std::shared_ptr<application::ports::BatchTaskRepository> batch_repository,
        BatchConcurrencyReader max_batch_concurrency,
        TimeoutReader task_timeout
    );

    // 启动批量任务，并用 handler 处理单个目标。
    std::string start(
        const std::vector<application::model::CrawlBatchItemRecord> &items,
        int normalized_limit,
        std::string task_type,
        std::string request_params,
        std::string settings_snapshot,
        std::string parent_batch_id,
        TargetHandler handler,
        bool allow_empty = false, // 已校验的队列查询可正常完成零目标任务
        int max_retries = 0       // 额外尝试次数，默认保持其他批次原有执行策略
    );

    // 请求取消批量任务；已开始的单个处理会在当前步骤结束后协作退出。
    bool cancel(const std::string &batch_id);

    // 获取任务进度快照。
    std::optional<application::model::BatchProgressSnapshot> progress(const std::string &batch_id);

private:
    // 判断任务是否超过当前配置的超时时间。
    bool isTaskTimedOut(const std::shared_ptr<application::model::BatchProgress> &progress) const;

    // 追加任务错误日志，最多保留固定数量。
    void appendError(const std::shared_ptr<application::model::BatchProgress> &progress, const std::string &message) const;

    // 清理已结束超过保留期的任务。
    void cleanupFinishedTasks();

    TaskRunner batch_runner_;                                                               // 批量任务执行器
    std::shared_ptr<application::ports::BatchTaskRepository> batch_repository_;             // 批次持久化 port
    BatchConcurrencyReader max_batch_concurrency_;                                          // 批量并发读取器
    TimeoutReader task_timeout_;                                                            // 任务超时读取器
    std::mutex tasks_mutex_;                                                                // 任务表锁
    std::map<std::string, std::shared_ptr<application::model::BatchProgress>> batch_tasks_; // 任务表
};

} // namespace orchestrator::modules::crawler::application::use_cases
