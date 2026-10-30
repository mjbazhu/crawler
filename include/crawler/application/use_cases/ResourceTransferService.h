#pragma once

#include "crawler/application/ports/execution/ResourceTransferRuntimeControl.h"
#include "crawler/application/ports/persistence/ResourceTransferAccess.h"
#include "crawler/application/ports/persistence/ResourceTransferJobRepository.h"
#include "crawler/application/ports/persistence/ResourceTransferQueryRepository.h"
#include "crawler/application/ports/persistence/ResourceProducerTaskRepository.h"
#include "crawler/application/ports/persistence/LegacyResourceTransferRepository.h"

#include <memory>
#include <string>
#include <vector>

namespace orchestrator::modules::crawler::application::use_cases {

// 线程安全的版本化操作服务；受理不等于文件处理完成，停止不依赖写入闸门。
class ResourceTransferService {
public:
    // 注入独立的持久任务、准入、只读事实和运行策略端口。
    ResourceTransferService(std::shared_ptr<ports::ResourceTransferJobRepository> jobs, std::shared_ptr<ports::ResourceTransferAccess> access, std::shared_ptr<ports::ResourceTransferQueryRepository> queries, std::shared_ptr<ports::ResourceTransferRuntimeControl> runtime, std::shared_ptr<ports::ResourceProducerTaskRepository> producers = {});
    // 维护与legacy模式可读，核验强度按当前已配置目标收紧。
    result::ServiceResult<model::ResourceStatusBatch> status(model::ResourceStatusQuery query);
    // 持久受理显式文件资源；具体来源和副作用由 worker 执行。
    result::ServiceResult<model::ResourceTransferOperation> process(model::ResourceTransferRequest request);
    // 查询真实操作；维护和 legacy 模式仍可读取已有历史。
    result::ServiceResult<model::ResourceTransferOperation> find(std::string id);
    // 定向取消本操作拥有的目标，并返回实际持久状态。
    result::ServiceResult<model::ResourceTransferOperation> stop(std::string id, std::vector<model::ResourceDestination> destinations);
    // 查询四类资源的逐平台事实，不探测远端。
    result::ServiceResult<model::ResourceCloudReceiptPage> receipts(model::ResourceCloudReceiptQuery query);
    // 仅检查结构兼容性；不要求目标在线、基线激活或 parallel 模式。
    result::ServiceResult<bool> schemaCompatible();
    // 返回当前启用目标的配置快照；API 应只选取安全字段。
    std::vector<model::ResourceTransferTargetProfile> profiles() const;
    // 是否启用并行受理。
    bool parallelEnabled() const;
    // 一次更新一端新受理的策略，已有任务的冻结配置保持。
    result::ServiceResult<bool> updatePolicy(model::ResourceDestination destination, model::ResourceTransferJobPolicy policy);
    // 生成计划读取与定向补排；不自动恢复仍可能运行的认领。
    result::ServiceResult<std::vector<model::ResourceProducerTask>> producers(std::string parent_operation_id);
    result::ServiceResult<bool> retryProducer(std::uint64_t producer_id);
    // 启动期装配持久兼容ID读取；查询无需启用parallel写入。
    void setLegacyRepository(std::shared_ptr<ports::LegacyResourceTransferRepository> repository, bool writes_ready)
    {
        legacy_ = std::move(repository);
        legacy_ready_ = writes_ready;
    }
    result::ServiceResult<model::LegacyResourceTransferBatch> legacyBatch(std::string id);
    bool legacyProjectionReady() const
    {
        return legacy_ready_;
    }

private:
    std::shared_ptr<ports::ResourceTransferJobRepository> jobs_;      // 持久任务
    std::shared_ptr<ports::ResourceTransferAccess> access_;           // 实时维护闸门
    std::shared_ptr<ports::ResourceTransferQueryRepository> queries_; // 只读事实
    std::shared_ptr<ports::ResourceTransferRuntimeControl> runtime_;  // 不含协议的策略控制
    std::shared_ptr<ports::ResourceProducerTaskRepository> producers_;
    std::shared_ptr<ports::LegacyResourceTransferRepository> legacy_;
    bool legacy_ready_ = false;
};

} // namespace orchestrator::modules::crawler::application::use_cases
