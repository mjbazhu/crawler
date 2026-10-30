#include "crawler/application/use_cases/ResourceTransferService.h"

namespace orchestrator::modules::crawler::application::use_cases {
result::ServiceResult<model::LegacyResourceTransferBatch> ResourceTransferService::legacyBatch(std::string id)
{
    using Result = result::ServiceResult<model::LegacyResourceTransferBatch>;
    const auto schema = schemaCompatible();
    if (!schema.isSuccess() || !*schema.data()) return Result::failure(result::ServiceErrorType::ExternalApiError, "resource_schema_unavailable");
    if (!legacy_) return Result::failure(result::ServiceErrorType::NotFound, "legacy_batch_missing");
    const auto found = legacy_->find(std::move(id));
    if (!found.isSuccess()) return Result::failure(found.error(), found.message());
    if (!*found.data()) return Result::failure(result::ServiceErrorType::NotFound, "legacy_batch_missing");
    return Result::success(**found.data());
}
result::ServiceResult<std::vector<model::ResourceProducerTask>> ResourceTransferService::producers(std::string id)
{
    using Result = result::ServiceResult<std::vector<model::ResourceProducerTask>>;
    const auto parent = find(id);
    if (!parent.isSuccess()) return Result::failure(parent.error(), parent.message());
    if (!producers_) return Result::failure(result::ServiceErrorType::ExternalApiError, "resource_producer_unavailable");
    return producers_->list(std::move(id));
}
result::ServiceResult<bool> ResourceTransferService::retryProducer(std::uint64_t id)
{
    using Result = result::ServiceResult<bool>;
    if (!id) return Result::failure(result::ServiceErrorType::InvalidParam, "invalid_producer_id");
    const auto schema = schemaCompatible();
    if (!schema.isSuccess() || !*schema.data()) return Result::failure(result::ServiceErrorType::ExternalApiError, "resource_schema_unavailable");
    if (!producers_ || !runtime_->parallelEnabled()) return Result::failure(result::ServiceErrorType::ExternalApiError, "resource_producer_unavailable");
    const auto allowed = access_->writesAllowed();
    if (!allowed.isSuccess() || !*allowed.data()) return Result::failure(result::ServiceErrorType::ExternalApiError, "resource_maintenance");
    return producers_->retry(id);
}
} // namespace orchestrator::modules::crawler::application::use_cases
