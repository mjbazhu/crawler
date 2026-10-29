#pragma once

#include "crawler/application/model/BatchProgress.h"
#include "crawler/application/model/CrawlBatch.h"
#include "crawler/application/ports/diagnostics/DiagnosticsSink.h"
#include "crawler/application/ports/execution/TaskExecutor.h"
#include "crawler/application/ports/persistence/BatchTaskRepository.h"
#include "crawler/application/use_cases/TaskRunner.h"

#include <chrono>
#include <atomic>
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
    // 带批次身份与协作取消检查的目标处理器，供独立资源任务复用执行设施。
    using ContextualTargetHandler = std::function<TargetResult(std::string, int, const std::string &, const std::function<bool()> &)>;
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

    // 标记停止并请求协作取消，后台任务持有独立上下文，不引用管理器本身。
    ~BatchTaskManager();

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

    // 启动带批次身份与取消检查的目标处理，并采用本批次的有界并发。
    std::string startWithContext(
        const std::vector<application::model::CrawlBatchItemRecord> &items,
        int normalized_limit,
        std::string task_type,
        std::string request_params,
        std::string settings_snapshot,
        ContextualTargetHandler handler,
        int concurrency,
        bool allow_empty = true
    );

    // 请求取消批量任务；已开始的单个处理会在当前步骤结束后协作退出。
    bool cancel(const std::string &batch_id);

    // 获取任务进度快照。
    std::optional<application::model::BatchProgressSnapshot> progress(const std::string &batch_id);

private:
    // 两种处理器共享同一生命周期和事件实现。
    std::string startImpl(const std::vector<application::model::CrawlBatchItemRecord> &items, int normalized_limit, std::string task_type, std::string request_params, std::string settings_snapshot, std::string parent_batch_id, ContextualTargetHandler handler, bool allow_empty, int concurrency, int max_retries);
    // 清理已结束超过保留期的任务。
    void cleanupFinishedTasks();

    TaskRunner batch_runner_;                                                                  // 批量任务执行器
    std::shared_ptr<application::ports::BatchTaskRepository> batch_repository_;                // 批次持久化 port
    BatchConcurrencyReader max_batch_concurrency_;                                             // 批量并发读取器
    TimeoutReader task_timeout_;                                                               // 任务超时读取器
    std::shared_ptr<std::atomic<bool>> stopping_ = std::make_shared<std::atomic<bool>>(false); // 脱离管理器生命周期的停止标记
    std::mutex tasks_mutex_;                                                                   // 任务表锁
    std::map<std::string, std::shared_ptr<application::model::BatchProgress>> batch_tasks_;    // 任务表
};

} // namespace orchestrator::modules::crawler::application::use_cases
