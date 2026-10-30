#pragma once

#include "crawler/application/model/ResourceTransferJobs.h"
#include "crawler/application/result/ServiceResult.h"

#include <vector>

namespace orchestrator::modules::crawler::application::ports {

// 线程安全的传输运行配置；不向业务/API 暴露认证、路径或执行器。
class ResourceTransferRuntimeControl {
public:
    // 释放运行配置接口。
    virtual ~ResourceTransferRuntimeControl() = default;
    // 是否显式启用正式并行模式。
    virtual bool parallelEnabled() const = 0;
    // 当前启用目标的已核对身份和受理策略；返回独立快照。
    virtual std::vector<model::ResourceTransferTargetProfile> profiles() const = 0;
    // 原子替换一端策略，只影响之后的新受理，不修改已有任务或私有配置。
    virtual result::ServiceResult<bool> updatePolicy(model::ResourceDestination destination, model::ResourceTransferJobPolicy policy) = 0;
};

} // namespace orchestrator::modules::crawler::application::ports
