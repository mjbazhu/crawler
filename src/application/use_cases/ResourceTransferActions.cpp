#include "crawler/application/model/ResourceTransferJobs.h"

namespace orchestrator::modules::crawler::application::model {
std::string_view resourceTransferActionName(ResourceTransferAction action)
{
    switch (action) {
    case ResourceTransferAction::Process: return "process";
    case ResourceTransferAction::Retry: return "retry";
    case ResourceTransferAction::Repair: return "repair_destination";
    case ResourceTransferAction::RefreshContent: return "refresh_content";
    case ResourceTransferAction::ProduceArtifacts: return "produce_artifacts";
    }
    return {};
}
std::optional<ResourceTransferAction> parseResourceTransferAction(std::string_view name)
{
    for (auto action : {ResourceTransferAction::Process, ResourceTransferAction::Retry, ResourceTransferAction::Repair, ResourceTransferAction::RefreshContent, ResourceTransferAction::ProduceArtifacts})
        if (name == resourceTransferActionName(action)) return action;
    return std::nullopt;
}
} // namespace orchestrator::modules::crawler::application::model
