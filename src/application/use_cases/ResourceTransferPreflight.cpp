#include "crawler/application/use_cases/ResourceTransferPreflight.h"

#include <utility>

namespace orchestrator::modules::crawler::application::use_cases {

result::ServiceResult<ResourceTransferPreflight> preflightResourceTransfer(ports::ResourceTransferAccess *access, model::ResourceIdentity identity, model::ResourceDestination destination, bool cancelled)
{
    using Result = result::ServiceResult<ResourceTransferPreflight>;
    if (cancelled) return Result::success({ResourceWorkDecision::Cancelled, {}, "cancelled"});
    if (!access) return Result::success({ResourceWorkDecision::Transfer, {}, "ok"});
    auto checked = access->inspect(identity, destination);
    if (!checked.isSuccess()) return Result::failure(checked.error(), "resource admission is unavailable");
    const auto &value = *checked.data();
    ResourceTransferPolicyInput input;
    input.version = value.version;
    input.skip = value.skip;
    input.dispatch_ready = value.dispatch_ready;
    const auto decision = evaluateResourceTransfer(input);
    if (decision == ResourceWorkDecision::SkipPolicy) return Result::success({decision, value.version, "skip_existing"});
    if (value.restricted) return Result::success({ResourceWorkDecision::Restricted, value.version, "permanent_resource_restriction"});
    return Result::success({decision, value.version, decision == ResourceWorkDecision::Transfer ? "ok" : value.blocked_code});
}
} // namespace orchestrator::modules::crawler::application::use_cases
