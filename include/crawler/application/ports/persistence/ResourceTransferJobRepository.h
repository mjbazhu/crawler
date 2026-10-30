#pragma once

#include "crawler/application/model/ResourceTransferJobs.h"
#include "crawler/application/result/ServiceResult.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace orchestrator::modules::crawler::application::ports {

// 线程安全的操作和逐目标任务持久化；所有网络/文件动作均在事务外。
class ResourceTransferJobRepository {
public:
    // 释放任务仓储。
    virtual ~ResourceTransferJobRepository() = default;
    // 一次短事务冻结全部任务/策略与占用；同幂等键不重新选择资源或配置，空 profiles 仅允许重放。
    virtual result::ServiceResult<model::ResourceTransferOperation> enqueue(model::ResourceTransferRequest request, std::vector<model::ResourceTransferTargetProfile> profiles) = 0;
    // 只读真实操作，包括终态和被其他操作占用的目标引用。
    virtual result::ServiceResult<model::ResourceTransferOperation> find(std::string operation_id) = 0;
    // 只认领某一目标，不等待 local 或其他 provider；无可执行项返回空。
    virtual result::ServiceResult<std::optional<model::ResourceTransferJob>> claim(model::ResourceDestination destination, std::string target_fingerprint, std::string adapter) = 0;
    // 取得内容和副作用边界调用；取消、维护或版本变化均返回 true。
    virtual result::ServiceResult<bool> shouldStop(const model::ResourceTransferJob &job) = 0;
    // 上传/发布前持久化精确意图，防止响应未知时遗失对象身份。
    virtual result::ServiceResult<bool> beginAttempt(const model::ResourceTransferJob &job, model::ResourceTransferAttempt attempt) = 0;
    // 同事务核对认领、来源、内容/目标版本和参考摘要，再确认回执与终态。
    virtual result::ServiceResult<bool> complete(const model::ResourceTransferJob &job, model::ResourceTransferReceipt receipt) = 0;
    // 独立结束或有限重试某目标；结果未知不伪装为已回滚。
    virtual result::ServiceResult<bool> fail(const model::ResourceTransferJob &job, model::ResourceTransferJobFailure failure) = 0;
    // 空目标集合取消本操作及其一层 producer 子操作全部目标，不涉及无关联操作。
    virtual result::ServiceResult<bool> cancel(std::string operation_id, std::vector<model::ResourceDestination> destinations) = 0;
    // 显式恢复超时租约，旧 worker 失效并保留未决对象意图，不盲目重传。
    virtual result::ServiceResult<int> recover(model::ResourceDestination destination, int stale_seconds) = 0;
};
} // namespace orchestrator::modules::crawler::application::ports
