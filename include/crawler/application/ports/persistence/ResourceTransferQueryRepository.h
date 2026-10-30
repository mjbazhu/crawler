#pragma once

#include "crawler/application/model/ResourceTransferQuery.h"
#include "crawler/application/model/ResourceStatus.h"
#include "crawler/application/result/ServiceResult.h"

namespace orchestrator::modules::crawler::application::ports {

// 线程安全的只读资源事实查询，与启用目标和维护开关独立。
class ResourceTransferQueryRepository {
public:
    // 释放查询接口。
    virtual ~ResourceTransferQueryRepository() = default;
    // 只检查结构兼容，不要求基线或并行开关已启用。
    virtual result::ServiceResult<bool> compatible() = 0;
    // 单次有界联表查询当前版本和回执，不发起存储探测。
    // 有界批量状态，同一快照读取四类资源；旧替身明确报告不可用。
    virtual result::ServiceResult<model::ResourceStatusBatch> status(model::ResourceStatusQuery)
    {
        return result::ServiceResult<model::ResourceStatusBatch>::failure(result::ServiceErrorType::ExternalApiError, "resource_status_unavailable");
    }
    virtual result::ServiceResult<model::ResourceCloudReceiptPage> receipts(model::ResourceCloudReceiptQuery query) = 0;
};

} // namespace orchestrator::modules::crawler::application::ports
