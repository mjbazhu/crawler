#pragma once
#include "crawler/application/model/ResourceTransferJobs.h"
#include "crawler/application/model/ResourceTransferSource.h"
#include "crawler/application/result/ServiceResult.h"
namespace orchestrator::modules::crawler::application::ports {
// 线程安全来源读取，不套用 local 状态筛选云端候选。
class ResourceTransferSources {
public:
    // 释放来源描述仓储。
    virtual ~ResourceTransferSources() = default;
    // 在短事务核对冻结身份、内容与来源版本，不领取本地队列。
    virtual result::ServiceResult<model::ResourceTransferSourceInput> describe(const model::ResourceTransferJob &job) = 0;
};
} // namespace orchestrator::modules::crawler::application::ports
