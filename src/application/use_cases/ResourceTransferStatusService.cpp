#include "crawler/application/use_cases/ResourceTransferService.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace orchestrator::modules::crawler::application::use_cases {
result::ServiceResult<model::ResourceStatusBatch> ResourceTransferService::status(model::ResourceStatusQuery query)
{
    using Result = result::ServiceResult<model::ResourceStatusBatch>;
    try {
        model::normalizeResourceStatusQuery(query);
    } catch (const std::invalid_argument &) {
        return Result::failure(result::ServiceErrorType::InvalidParam, "invalid_resource_status_query");
    }
    const auto schema = schemaCompatible();
    if (!schema.isSuccess() || !*schema.data()) return Result::failure(result::ServiceErrorType::ExternalApiError, "resource_schema_unavailable");
    const auto found = queries_->status(std::move(query));
    if (!found.isSuccess()) return found;
    auto value = *found.data();
    const auto profiles = runtime_->profiles();
    for (auto &item : value.items) {
        for (auto &cloud : item.cloud_receipts) {
            const auto profile = std::find_if(profiles.begin(), profiles.end(), [&](const auto &p) { return p.destination == cloud.provider; });
            if (profile != profiles.end() && profile->policy.strict_sha256 && cloud.verification != "sha256_verified") cloud.current = false;
        }
        item.all_requested_targets_current = item.found && !value.requested_targets.empty() && std::all_of(value.requested_targets.begin(), value.requested_targets.end(), [&](auto destination) {
                                                 if (destination == model::ResourceDestination::Local) return item.local.current;
                                                 const auto cloud = std::find_if(item.cloud_receipts.begin(), item.cloud_receipts.end(), [&](const auto &c) { return c.provider == destination; });
                                                 return cloud != item.cloud_receipts.end() && cloud->current;
                                             });
    }
    return Result::success(std::move(value));
}
} // namespace orchestrator::modules::crawler::application::use_cases
