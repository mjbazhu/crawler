#include "crawler/application/use_cases/ResourceTransferService.h"

#include <algorithm>
#include <set>
#include <stdexcept>
#include <utility>

namespace orchestrator::modules::crawler::application::use_cases {

ResourceTransferService::ResourceTransferService(std::shared_ptr<ports::ResourceTransferJobRepository> jobs, std::shared_ptr<ports::ResourceTransferAccess> access, std::shared_ptr<ports::ResourceTransferQueryRepository> queries, std::shared_ptr<ports::ResourceTransferRuntimeControl> runtime, std::shared_ptr<ports::ResourceProducerTaskRepository> producers) : jobs_(std::move(jobs)), access_(std::move(access)), queries_(std::move(queries)), runtime_(std::move(runtime)), producers_(std::move(producers))
{
    if (!jobs_ || !access_ || !queries_ || !runtime_) throw std::invalid_argument("resource transfer service dependencies required");
}

result::ServiceResult<model::ResourceTransferOperation> ResourceTransferService::process(model::ResourceTransferRequest request)
{
    using Result = result::ServiceResult<model::ResourceTransferOperation>;
    request.require_write_admission = true;
    if (!runtime_->parallelEnabled()) return Result::failure(result::ServiceErrorType::ExternalApiError, "resource_parallel_required");
    const auto schema = schemaCompatible();
    if (!schema.isSuccess() || !*schema.data()) return Result::failure(result::ServiceErrorType::ExternalApiError, "resource_schema_unavailable");
    const auto allowed = access_->writesAllowed();
    if (!allowed.isSuccess() || !*allowed.data()) {
        // 空目标配置仅可命中已持久的幂等操作；新请求由仓储原子回滚，不扩大维护期写入。
        if (!request.idempotency_key.empty()) {
            const auto replay = jobs_->enqueue(std::move(request), {});
            if (replay.isSuccess() || replay.error() == result::ServiceErrorType::Conflict) return replay;
        }
        return Result::failure(result::ServiceErrorType::ExternalApiError, "resource_maintenance");
    }
    return jobs_->enqueue(std::move(request), runtime_->profiles());
}

result::ServiceResult<model::ResourceTransferOperation> ResourceTransferService::find(std::string id)
{
    const auto schema = schemaCompatible();
    if (!schema.isSuccess() || !*schema.data()) return result::ServiceResult<model::ResourceTransferOperation>::failure(result::ServiceErrorType::ExternalApiError, "resource_schema_unavailable");
    return jobs_->find(std::move(id));
}

result::ServiceResult<model::ResourceTransferOperation> ResourceTransferService::stop(std::string id, std::vector<model::ResourceDestination> destinations)
{
    using Result = result::ServiceResult<model::ResourceTransferOperation>;
    if (destinations.size() > 3 || std::set(destinations.begin(), destinations.end()).size() != destinations.size()) return Result::failure(result::ServiceErrorType::InvalidParam, "invalid_cancel_targets");
    const auto before = find(id);
    if (!before.isSuccess()) return before;
    for (const auto destination : destinations)
        if (std::none_of(before.data()->tasks.begin(), before.data()->tasks.end(), [destination](const auto &task) { return task.destination == destination; })) return Result::failure(result::ServiceErrorType::InvalidParam, "destination_not_in_operation");
    const auto cancelled = jobs_->cancel(id, std::move(destinations));
    if (!cancelled.isSuccess()) return Result::failure(cancelled.error(), cancelled.message());
    return jobs_->find(std::move(id));
}

result::ServiceResult<model::ResourceCloudReceiptPage> ResourceTransferService::receipts(model::ResourceCloudReceiptQuery query)
{
    const auto schema = schemaCompatible();
    if (!schema.isSuccess() || !*schema.data()) return result::ServiceResult<model::ResourceCloudReceiptPage>::failure(result::ServiceErrorType::ExternalApiError, "resource_schema_unavailable");
    const auto found = queries_->receipts(std::move(query));
    if (!found.isSuccess()) return found;
    auto page = *found.data();
    const auto profiles = runtime_->profiles();
    for (auto &item : page.items) {
        const auto configured = std::find_if(profiles.begin(), profiles.end(), [&](const auto &profile) { return profile.destination == item.provider; });
        if (configured != profiles.end() && configured->policy.strict_sha256 && item.verification != "sha256_verified") item.current = false;
    }
    return result::ServiceResult<model::ResourceCloudReceiptPage>::success(std::move(page));
}

std::vector<model::ResourceTransferTargetProfile> ResourceTransferService::profiles() const
{
    return runtime_->profiles();
}

result::ServiceResult<bool> ResourceTransferService::schemaCompatible()
{
    return queries_->compatible();
}

bool ResourceTransferService::parallelEnabled() const
{
    return runtime_->parallelEnabled();
}

result::ServiceResult<bool> ResourceTransferService::updatePolicy(model::ResourceDestination destination, model::ResourceTransferJobPolicy policy)
{
    if (!runtime_->parallelEnabled()) return result::ServiceResult<bool>::failure(result::ServiceErrorType::ExternalApiError, "resource_parallel_required");
    const auto profiles = runtime_->profiles();
    const auto configured = std::find_if(profiles.begin(), profiles.end(), [destination](const auto &profile) { return profile.destination == destination; });
    if (configured == profiles.end()) return result::ServiceResult<bool>::failure(result::ServiceErrorType::ExternalApiError, "resource_destination_disabled");
    policy.strict_sha256 = configured->policy.strict_sha256;
    return runtime_->updatePolicy(destination, policy);
}

} // namespace orchestrator::modules::crawler::application::use_cases
