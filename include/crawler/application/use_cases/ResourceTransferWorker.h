#pragma once

#include "crawler/application/ports/persistence/ResourceTransferJobRepository.h"
#include "crawler/application/ports/provider/ResourceTransferSource.h"
#include "crawler/application/ports/storage/ResourceTransferDestination.h"
#include "crawler/application/use_cases/ResourceTransferBudget.h"

#include <functional>
#include <memory>
#include <string>

namespace orchestrator::modules::crawler::application::use_cases {

// 无共享任务可变状态；每个目标可在独立有界 executor 中并发调用。
class ResourceTransferWorker {
public:
    // 同一组 source/transfer 配额由所有目标共享；各目标策略与指纹独立。
    ResourceTransferWorker(std::shared_ptr<ports::ResourceTransferJobRepository> jobs, std::shared_ptr<ports::ResourceTransferSource> source, std::shared_ptr<ports::ResourceTransferDestination> destination, model::ResourceTransferTargetProfile profile, std::shared_ptr<ResourceTransferBudget> source_budget, std::shared_ptr<ResourceTransferBudget> transfer_budget);
    // 执行至多一个持久任务；空队列为 success(false)，数据库未知保持可核对状态。
    result::ServiceResult<bool> runOne(std::function<bool()> stopping = {});

private:
    // 一次任务的来源、意图、发布和条件确认均使用独立上下文。
    result::ServiceResult<bool> execute(model::ResourceTransferJob job, std::function<bool()> stopping);
    std::shared_ptr<ports::ResourceTransferJobRepository> jobs_;      // 持久操作与取消
    std::shared_ptr<ports::ResourceTransferSource> source_;           // 独立不可变快照获取
    std::shared_ptr<ports::ResourceTransferDestination> destination_; // 本端物理传输
    model::ResourceTransferTargetProfile profile_;                    // 工厂冻结的本端身份
    std::shared_ptr<ResourceTransferBudget> source_budget_;           // 全局来源上限
    std::shared_ptr<ResourceTransferBudget> transfer_budget_;         // 全局传输上限
};

} // namespace orchestrator::modules::crawler::application::use_cases
