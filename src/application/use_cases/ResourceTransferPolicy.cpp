#include "crawler/application/use_cases/ResourceTransferPolicy.h"

namespace orchestrator::modules::crawler::application::use_cases {

ResourceWorkDecision evaluateResourceTransfer(const ResourceTransferPolicyInput &input)
{
    if (!input.enabled) return ResourceWorkDecision::Disabled;
    if (input.cancelled) return ResourceWorkDecision::Cancelled;
    if (!input.version.content_generation || !input.version.target_revision) return ResourceWorkDecision::Blocked;
    if (input.skip.enabled && !input.skip.baseline_id.empty() && !input.skip.reason.empty() && input.skip.version == input.version)
        return ResourceWorkDecision::SkipPolicy;
    if (!input.dispatch_ready) return ResourceWorkDecision::Blocked;
    if (input.already_current) return ResourceWorkDecision::AlreadyCurrent;
    if (input.active) return ResourceWorkDecision::Busy;
    if (input.waiting_input) return ResourceWorkDecision::WaitingInput;
    if (!input.available) return ResourceWorkDecision::Unavailable;
    return ResourceWorkDecision::Transfer;
}

} // namespace orchestrator::modules::crawler::application::use_cases
