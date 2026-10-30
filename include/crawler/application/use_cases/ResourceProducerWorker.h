#pragma once

#include "crawler/application/ports/persistence/ResourceProducerTaskRepository.h"
#include "crawler/application/ports/parsing/ResourceArtifactGenerator.h"
#include "crawler/application/ports/provider/ResourceTransferSource.h"
#include "crawler/application/use_cases/ResourceTransferBudget.h"

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>

namespace orchestrator::modules::crawler::application::use_cases {

// 独立生成 worker；取得来源、CPU 解码与数据库预算均不占用 MRC 下载调用。
class ResourceProducerWorker {
public:
    // 注入生成队列、受控来源与纯生成器，独立于文件传输 worker 的生命周期。
    ResourceProducerWorker(std::shared_ptr<ports::ResourceProducerTaskRepository> tasks, std::shared_ptr<ports::ResourceTransferSource> source, std::shared_ptr<ports::ResourceArtifactGenerator> generator, model::ResourceTransferTargetProfile profile, std::shared_ptr<ResourceTransferBudget> source_budget);
    // 每次最多处理一项；崩溃租约定期有界恢复，不将缓存当成持久输入。
    result::ServiceResult<bool> runOne(std::function<bool()> stopping = {});

private:
    std::shared_ptr<ports::ResourceProducerTaskRepository> tasks_; // 任务及原子子操作
    std::shared_ptr<ports::ResourceTransferSource> source_;        // 独立预算的受控 MRC 取得
    std::shared_ptr<ports::ResourceArtifactGenerator> generator_;  // 纯生成与有界缓存
    model::ResourceTransferTargetProfile profile_;                 // 本端身份和冻结版本能力
    std::shared_ptr<ResourceTransferBudget> source_budget_;        // 与文件传输共用全局来源上限
    std::atomic<std::int64_t> last_recovery_ {0};                  // 每 30 秒至多一次有限恢复
};

} // namespace orchestrator::modules::crawler::application::use_cases
