#include "crawler/application/use_cases/AsyncSongResourceDownloadWorkflow.h"

#include "ResourceDownloadSupport.h"
#include "ResourceDownloadState.h"
#include "crawler/application/use_cases/ResourceTransferPreflight.h"

#include <stdexcept>

namespace orchestrator::modules::crawler::application::use_cases {

application::async::Task<void> AsyncSongResourceDownloadWorkflow::requireWritesAllowed()
{
    if (!extensions_.transfer_access) co_return;
    const auto allowed = co_await runtime_->postBlocking<application::result::ServiceResult<bool>>([access = extensions_.transfer_access] {
        const auto global = access->writesAllowed();
        return !global.isSuccess() || !*global.data() ? global : access->destinationReady(application::model::ResourceDestination::Local);
    });
    if (!allowed.isSuccess() || !*allowed.data()) throw std::runtime_error("resource writes are paused");
}

application::async::Task<std::optional<application::model::SongResourceProcessItem>> AsyncSongResourceDownloadWorkflow::checkAdmission(const application::model::SongResourceQueueTarget &target, SongResourceProcessListener *listener)
{
    const auto checked = co_await runtime_->postBlocking<application::result::ServiceResult<ResourceTransferPreflight>>([access = extensions_.transfer_access, identity = application::model::ResourceIdentity {application::model::ResourceOwner::Song, target.resource_id}, listener] {
        return preflightResourceTransfer(access.get(), identity, application::model::ResourceDestination::Local, listener && listener->isCancellationRequested());
    });
    if (checked.isSuccess() && checked.data()->decision == ResourceWorkDecision::Transfer) co_return std::nullopt;
    const bool skipped = checked.isSuccess() && (checked.data()->decision == ResourceWorkDecision::SkipPolicy || checked.data()->decision == ResourceWorkDecision::Restricted);
    const auto code = checked.isSuccess() ? checked.data()->result_code : "resource_admission_failed";
    co_return resource_detail::makeItem(target, false, skipped, false, false, skipped ? "" : "resource", code, skipped ? "" : "resource transfer not admitted");
}

application::async::Task<std::vector<application::model::SongResourceQueueTarget>> AsyncSongResourceDownloadWorkflow::filterAdmission(std::vector<application::model::SongResourceQueueTarget> targets, std::shared_ptr<ProcessRunState> state, SongResourceProcessListener *listener)
{
    if (!extensions_.transfer_access) co_return targets;
    std::vector<application::model::SongResourceQueueTarget> ready;
    ready.reserve(targets.size());
    for (auto &target : targets) {
        auto blocked = co_await checkAdmission(target, listener);
        if (!blocked) {
            ready.push_back(std::move(target));
            continue;
        }
        co_await notifyItemStarted(listener, resource_detail::makeStartedItem(target, false));
        if (blocked->result_code == "cancelled") *blocked = co_await fail(target, "cancelled", "cancelled", "resource batch cancelled", state->result.batch_id, listener);
        co_await notifyItemFinished(listener, *blocked);
        std::lock_guard lock(state->result_mutex);
        if (blocked->skipped)
            ++state->result.skipped_count;
        else
            ++state->result.failed_count;
        state->result.items.push_back(std::move(*blocked));
    }
    co_return ready;
}
} // namespace orchestrator::modules::crawler::application::use_cases
