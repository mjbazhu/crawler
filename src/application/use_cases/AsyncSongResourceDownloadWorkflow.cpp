#include "crawler/application/use_cases/AsyncSongResourceDownloadWorkflow.h"

#include "ResourceDownloadSupport.h"
#include "ResourceDownloadState.h"

namespace orchestrator::modules::crawler::application::use_cases {
using namespace resource_detail; // 本用例内部的纯映射和事件格式规则

AsyncSongResourceDownloadWorkflow::AsyncSongResourceDownloadWorkflow(
    std::shared_ptr<application::ports::AsyncRuntime> runtime,
    std::shared_ptr<application::ports::AsyncSongResourceQueue> resource_queue,
    std::shared_ptr<application::ports::AsyncSongResourceProvider> resource_provider,
    std::shared_ptr<application::ports::SongResourceParser> resource_parser,
    std::shared_ptr<application::ports::AsyncSongResourceContentFetcher> content_fetcher,
    std::shared_ptr<application::ports::AsyncSongResourceBlobStore> blob_store,
    SongResourcePathPlanner path_planner,
    std::shared_ptr<application::ports::MiguPacmtokenRefresher> pacmtoken_refresher,
    std::shared_ptr<application::ports::MiguPacmtokenStore> pacmtoken_store,
    std::shared_ptr<application::ports::AsyncSongResourceRemoteCopier> remote_copier,
    std::shared_ptr<application::ports::AsyncSongResourceCloudUploader> cloud_uploader,
    std::shared_ptr<application::ports::AsyncSongResourceBatchCloudUploader> batch_cloud_uploader,
    application::ports::ResourceProcessingExtensions extensions
) : extensions_(std::move(extensions)), runtime_(std::move(runtime)),
    resource_queue_(std::move(resource_queue)),
    resource_provider_(std::move(resource_provider)),
    resource_parser_(std::move(resource_parser)),
    content_fetcher_(std::move(content_fetcher)),
    blob_store_(std::move(blob_store)),
    path_planner_(std::move(path_planner)),
    pacmtoken_refresher_(std::move(pacmtoken_refresher)),
    pacmtoken_store_(std::move(pacmtoken_store)),
    remote_copier_(std::move(remote_copier)),
    cloud_uploader_(std::move(cloud_uploader)),
    batch_cloud_uploader_(std::move(batch_cloud_uploader))
{}

application::async::Task<application::model::SongResourceProcessResult> AsyncSongResourceDownloadWorkflow::processPending(application::model::SongResourceClaimQuery query, SongResourceProcessListener *listener)
{
    auto state = std::make_shared<ProcessRunState>();
    state->result.batch_id = query.batch_id;
    state->options = query.options;
    if (!runtime_ || !resource_queue_ || ((!content_fetcher_ || !blob_store_) && !remote_copier_)) {
        co_return state->result;
    }
    if (query.source.empty()) {
        query.source = "migu";
    }
    if (query.limit == 0) {
        query.limit = 20;
    }
    state->result.batch_id = query.batch_id;

    co_await requireWritesAllowed();

    co_await notifyBatchEvent(listener, "resourceClaimStarted", claimPayload(query, "resource_claim_started"));
    std::vector<application::model::SongResourceQueueTarget> targets;
    std::exception_ptr claim_exception;
    std::string claim_error;
    try {
        targets = co_await resource_queue_->claimPending(query);
    } catch (const std::exception &e) {
        claim_exception = std::current_exception();
        claim_error = trimMessage(e.what());
    } catch (...) {
        claim_exception = std::current_exception();
        claim_error = "resource claim failed with unknown error";
    }
    if (claim_exception) {
        co_await notifyBatchEvent(listener, "resourceClaimFailed", claimPayload(query, "resource_claim_failed", "\"error\":\"" + escapeJson(claim_error) + "\""));
        std::rethrow_exception(claim_exception);
    }
    {
        std::lock_guard<std::mutex> lock(state->result_mutex);
        state->result.claimed_count = static_cast<int>(targets.size());
    }
    co_await notifyBatchEvent(listener, "resourceClaimCompleted", claimPayload(query, "resource_claim_completed", "\"claimed_count\":" + std::to_string(targets.size()) + ",\"concurrency\":" + std::to_string(normalizedResourceConcurrency(state->options)) + ",\"retries\":" + std::to_string(state->options.resource_download_retries) + ",\"timeout_ms\":" + std::to_string(state->options.resource_download_timeout_ms)));
    if (targets.empty()) {
        co_await notifyBatchEvent(listener, "resourceClaimEmpty", claimPayload(query, "resource_claim_empty"));
        co_return state->result;
    }

    // 空队列或全部历史跳过不能为 token 刷新而产生额外网络请求。
    targets = co_await filterAdmission(std::move(targets), state, listener);
    if (targets.empty()) co_return state->result;
    if (state->options.auto_refresh_pacmtoken) (void)co_await refreshPacmtoken(state, listener, true);

    auto shared_targets = std::make_shared<std::vector<application::model::SongResourceQueueTarget>>(std::move(targets));
    auto next_index = std::make_shared<std::atomic<std::size_t>>(0);
    auto remaining = std::make_shared<std::atomic<int>>(static_cast<int>(shared_targets->size()));
    application::async::TaskCompletionSource<void> done;
    auto wait_done = done.task();
    auto launcher = std::make_shared<std::function<void()>>();
    std::weak_ptr<std::function<void()>> weak_launcher = launcher;

    *launcher = [this, state, shared_targets, next_index, remaining, done, weak_launcher, batch_id = query.batch_id, listener]() mutable {
        const std::size_t index = next_index->fetch_add(1);
        if (index >= shared_targets->size()) {
            return;
        }
        auto target = std::move((*shared_targets)[index]);
        auto worker_task = processWorker(state, std::move(target), batch_id, listener);
        worker_task.startDetached([remaining, done, weak_launcher](std::exception_ptr) mutable {
            if (remaining->fetch_sub(1) == 1) {
                done.set_value();
                return;
            }
            if (auto launch = weak_launcher.lock()) {
                (*launch)();
            }
        });
    };

    const std::size_t initial_workers = (std::min)(normalizedResourceConcurrency(state->options), shared_targets->size());
    for (std::size_t index = 0; index < initial_workers; ++index) {
        (*launcher)();
    }

    co_await wait_done;
    co_await uploadBatchCloudObjects(state, query.batch_id, listener);
    application::model::SongResourceProcessResult result;
    {
        std::lock_guard<std::mutex> lock(state->result_mutex);
        result = state->result;
    }
    co_return result;
}

application::async::Task<application::model::SongResourceProcessResult> AsyncSongResourceDownloadWorkflow::processAvailableRelated(application::model::SongResourceQueueTarget song, application::model::SongResourceFetchResult resources)
{
    auto state = std::make_shared<ProcessRunState>();
    state->options.auto_refresh_pacmtoken = false;
    if (runtime_ && resource_queue_ && content_fetcher_ && blob_store_) {
        co_await requireWritesAllowed();
        if (!co_await checkAdmission(song, nullptr)) co_await processRelatedResources(song, resources, {}, state, nullptr);
    }
    co_return state->result;
}

application::async::Task<application::model::SongResourceProcessItem> AsyncSongResourceDownloadWorkflow::processClaimed(application::model::SongResourceQueueTarget target, std::string batch_id)
{
    co_return co_await processClaimed(std::move(target), std::move(batch_id), application::model::SongResourceProcessOptions {});
}

application::async::Task<application::model::SongResourceProcessItem> AsyncSongResourceDownloadWorkflow::processClaimed(application::model::SongResourceQueueTarget target, std::string batch_id, application::model::SongResourceProcessOptions options)
{
    auto state = std::make_shared<ProcessRunState>();
    state->options = std::move(options);
    state->options.process_related_resources = false;
    if (const auto blocked = co_await checkAdmission(target, nullptr)) co_return *blocked;
    co_return isAudioResource(target.resource_kind) ? co_await processAudio(std::move(target), batch_id, state, nullptr) : co_await processDirectResource(std::move(target), batch_id, state->options, nullptr);
}

application::async::Task<void> AsyncSongResourceDownloadWorkflow::processWorker(std::shared_ptr<ProcessRunState> state, application::model::SongResourceQueueTarget target, std::string batch_id, SongResourceProcessListener *listener)
{
    application::model::SongResourceProcessItem item;
    try {
        co_await notifyItemStarted(listener, makeStartedItem(target, false));
        const auto blocked = co_await checkAdmission(target, listener);
        if (blocked && blocked->result_code == "cancelled")
            item = co_await fail(target, "cancelled", "cancelled", "resource batch cancelled", batch_id, listener);
        else if (blocked)
            item = *blocked;
        else
            item = isAudioResource(target.resource_kind) ? co_await processAudio(target, batch_id, state, listener) : co_await processDirectResource(target, batch_id, state->options, listener);
    } catch (const std::exception &e) {
        item = makeItem(target, false, false, false, false, "unknown", "worker_failed", e.what());
    } catch (...) {
        item = makeItem(target, false, false, false, false, "unknown", "worker_failed", "resource worker failed with unknown error");
    }
    if (item.result_code == "worker_failed") {
        item = co_await fail(target, "unknown", "worker_failed", "resource worker failed", batch_id, listener);
    }
    co_await notifyItemFinished(listener, item);
    {
        std::lock_guard<std::mutex> lock(state->result_mutex);
        if (item.ok) {
            ++state->result.succeeded_count;
        } else if (item.skipped) {
            ++state->result.skipped_count;
        } else {
            ++state->result.failed_count;
        }
        state->result.items.push_back(std::move(item));
    }
}

} // namespace orchestrator::modules::crawler::application::use_cases
