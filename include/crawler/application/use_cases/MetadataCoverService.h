#pragma once

#include "crawler/application/model/BatchProgress.h"
#include "crawler/application/model/MetadataCover.h"
#include "crawler/application/ports/diagnostics/DiagnosticsSink.h"
#include "crawler/application/ports/execution/TaskExecutor.h"
#include "crawler/application/ports/persistence/BatchTaskRepository.h"
#include "crawler/application/ports/persistence/MetadataCoverQueue.h"
#include "crawler/application/ports/storage/MetadataCoverReader.h"
#include "crawler/application/result/ServiceResult.h"
#include "crawler/application/use_cases/BatchTaskManager.h"
#include "crawler/application/use_cases/MetadataCoverDownloadWorkflow.h"
#include "crawler/application/use_cases/MetadataCoverCloudWorkflow.h"

#include <chrono>
#include <memory>

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace orchestrator::modules::crawler::application::use_cases {

// 封面查询与下载任务门面；线程安全，运行状态委托既有批次管理器。
class MetadataCoverService {
public:
    // 注入窄 port、下载用例及有界执行器；不引入基础设施类型。
    MetadataCoverService(std::shared_ptr<ports::MetadataCoverQueue> queue, std::shared_ptr<ports::MetadataCoverReader> reader, std::shared_ptr<MetadataCoverDownloadWorkflow> workflow, std::shared_ptr<ports::TaskExecutor> executor, std::shared_ptr<ports::DiagnosticsSink> diagnostics, std::shared_ptr<ports::BatchTaskRepository> batches, std::chrono::milliseconds timeout = std::chrono::minutes(30), std::shared_ptr<MetadataCoverCloudWorkflow> cloud = {});
    // 查询队列并补本地文件可用性。
    result::ServiceResult<model::MetadataCoverPage> list(const model::MetadataCoverQuery &query);
    // 同步小批下载，复用相同的单资源用例。
    result::ServiceResult<std::vector<model::MetadataCoverItem>> process(const model::MetadataCoverOptions &options);
    // 创建持久化批次，下载在有界执行器执行。
    result::ServiceResult<std::string> start(const model::MetadataCoverOptions &options);
    // 返回已校验的图片内容。
    result::ServiceResult<model::MetadataCoverContent> content(model::CoverOwner owner, std::uint64_t id);
    // 恢复指定范围的超时认领。
    result::ServiceResult<int> recover(const model::MetadataCoverQuery &query, int stale_seconds);
    // 查询本服务的内存任务进度。
    std::optional<model::BatchProgressSnapshot> progress(const std::string &batch_id);
    // 协作取消本服务的任务。
    bool cancel(const std::string &batch_id);

private:
    // 将下载筛选收敛为有界候选页，不提前认领未开始的任务。
    result::ServiceResult<model::MetadataCoverPage> select(const model::MetadataCoverOptions &options);
    std::shared_ptr<ports::MetadataCoverQueue> queue_;        // 数据库队列
    std::shared_ptr<ports::MetadataCoverReader> reader_;      // 文件读取
    std::shared_ptr<MetadataCoverDownloadWorkflow> workflow_; // 单资源编排
    std::shared_ptr<MetadataCoverCloudWorkflow> cloud_;       // 可选的共用云端阶段
    BatchTaskManager tasks_;                                  // 批次状态、事件与取消
};

} // namespace orchestrator::modules::crawler::application::use_cases
