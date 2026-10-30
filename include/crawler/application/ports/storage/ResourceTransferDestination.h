#pragma once

#include "crawler/application/model/ResourceTransferExecution.h"
#include "crawler/application/result/ServiceResult.h"

#include <memory>

namespace orchestrator::modules::crawler::application::ports {

// 一次独占发布会话；写入前可查询实际路径，未知确认必须保留补偿。
class ResourceTransferWrite {
public:
    // 析构清理未发布 staging，已发布未确认对象由 settle 决定。
    virtual ~ResourceTransferWrite() = default;
    // 返回副作用前已经固定的实际对象身份。
    virtual model::ResourceTransferAttempt attempt() const = 0;
    // 完整发布并实测核验；未决副作用返回 CommitUncertain。
    virtual result::ServiceResult<model::ResourceTransferReceipt> publish(const model::ResourceTransferContext &context) = 0;
    // Accept 解除补偿，Defer 保留日志，Abort 仅清理确定属于本次的对象。
    virtual void settle(model::ResourceTransferSettlement settlement) noexcept = 0;
};

// 线程安全的单端传输策略，不能改写其他端或本地数据库。
class ResourceTransferDestination {
public:
    // 释放单端适配器。
    virtual ~ResourceTransferDestination() = default;
    // 核对持久旧尝试，不获取源站内容。
    virtual result::ServiceResult<model::ResourceAttemptInspection> inspect(const model::ResourceTransferJob &job, const model::ResourceTransferContext &context) = 0;
    // 准备独占 staging/对象身份，此时不得发布正式文件或上传。
    virtual result::ServiceResult<std::shared_ptr<ResourceTransferWrite>> prepare(const model::ResourceTransferJob &job, std::shared_ptr<const model::ResourceContentSnapshot> snapshot, const model::ResourceTransferContext &context) = 0;
};

} // namespace orchestrator::modules::crawler::application::ports
