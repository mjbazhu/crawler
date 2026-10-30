#pragma once

#include "crawler/application/model/ResourceTransferExecution.h"
#include "crawler/application/result/ServiceResult.h"

#include <memory>

namespace orchestrator::modules::crawler::application::ports {

// 线程安全来源 port；每个调用独立获取，不等待正式 local 或另一目标。
class ResourceTransferSource {
public:
    // 释放来源适配器。
    virtual ~ResourceTransferSource() = default;
    // 历史 skip 在登记/认领时已处理；这里仅获取确实需要的完整快照。
    virtual result::ServiceResult<std::shared_ptr<const model::ResourceContentSnapshot>> acquire(const model::ResourceTransferJob &job, const model::ResourceTransferContext &context) = 0;
};

} // namespace orchestrator::modules::crawler::application::ports
