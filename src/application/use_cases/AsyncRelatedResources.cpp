#include "crawler/application/use_cases/AsyncSongResourceDownloadWorkflow.h"

#include "ResourceDownloadSupport.h"
#include "ResourceDownloadState.h"

namespace orchestrator::modules::crawler::application::use_cases {
using namespace resource_detail; // 本用例内部的纯映射和事件格式规则

application::async::Task<void> AsyncSongResourceDownloadWorkflow::processRelatedResources(const application::model::SongResourceQueueTarget &audio_target, const application::model::SongResourceFetchResult &fetch_result, const std::string &batch_id, std::shared_ptr<ProcessRunState> state, SongResourceProcessListener *listener)
{
    const std::array<std::pair<std::string, std::string>, 4> related {{
        {"cover", fetch_result.cover_url},
        {"lyric", fetch_result.lyric_url},
        {"mrc", fetch_result.mrc_url},
        {"trc", fetch_result.trc_url},
    }};

    for (const auto &[kind, url] : related) {
        if (url.empty()) {
            co_await notifyBatchEvent(listener, "relatedResourceUrlMissing", stagePayload(audio_target, "related_resource_url_missing", "\"related_kind\":\"" + escapeJson(kind) + "\""));
            continue;
        }
        if (state) {
            const std::string key = relatedResourceKey(audio_target, kind);
            bool inserted = false;
            {
                std::lock_guard<std::mutex> lock(state->related_mutex);
                inserted = state->processed_related_keys.insert(key).second;
            }
            if (!inserted) {
                co_await notifyBatchEvent(listener, "relatedResourceDeduplicated", stagePayload(audio_target, "related_resource_deduplicated", "\"related_kind\":\"" + escapeJson(kind) + "\""));
                continue;
            }
        }

        application::model::SongResourceRelatedClaimRequest request;
        request.song_source_ref_id = audio_target.song_source_ref_id;
        request.resource_kind = kind;
        request.remote_url = url;
        request.batch_id = batch_id;

        co_await notifyBatchEvent(listener, "relatedResourceClaimStarted", stagePayload(audio_target, "related_resource_claim_started", "\"related_kind\":\"" + escapeJson(kind) + "\""));
        std::optional<application::model::SongResourceQueueTarget> related_target;
        application::model::SongResourceProcessItem item;
        bool failed = false;
        try {
            related_target = co_await resource_queue_->claimRelated(std::move(request));
            if (related_target) {
                co_await notifyItemStarted(listener, makeStartedItem(*related_target, true));
                co_await notifyItemEvent(listener, makeStartedItem(*related_target, true), "relatedResourceClaimed", stagePayload(*related_target, "related_resource_claimed", "\"related_kind\":\"" + escapeJson(kind) + "\""));
                item = co_await processDirectResource(*related_target, batch_id, state->options, listener);
            }
        } catch (...) {
            failed = true;
        }
        if (failed && related_target) {
            item = co_await fail(*related_target, "unknown", "related_worker_failed", "related resource processing interrupted", batch_id, listener);
        } else if (!related_target) {
            item = makeRelatedSkippedItem(audio_target, kind);
            if (failed) {
                item.skipped = false;
                item.error_category = "persistence";
                item.result_code = "related_claim_failed";
                item.message = "related resource claim failed";
            }
            co_await notifyItemStarted(listener, item);
            co_await notifyItemEvent(listener, item, failed ? "relatedResourceClaimFailed" : "relatedResourceNotClaimed", stagePayload(audio_target, "related_resource_not_claimed", "\"related_kind\":\"" + escapeJson(kind) + "\""));
            if (!failed) {
                std::lock_guard<std::mutex> lock(state->result_mutex);
                ++state->result.synthetic_related_skipped_count;
            }
        }
        item.related = true;
        {
            std::lock_guard<std::mutex> lock(state->result_mutex);
            if (item.ok) {
                ++state->result.related_succeeded_count;
            } else if (item.skipped) {
                ++state->result.related_skipped_count;
            } else {
                ++state->result.related_failed_count;
            }
            state->result.items.push_back(item);
        }
        co_await notifyItemFinished(listener, item);
    }
}

application::async::Task<bool> AsyncSongResourceDownloadWorkflow::refreshPacmtoken(std::shared_ptr<ProcessRunState> state, SongResourceProcessListener *listener, bool force)
{
    if (!state || !state->options.auto_refresh_pacmtoken || !pacmtoken_refresher_ || !pacmtoken_store_ || !runtime_) {
        co_return false;
    }
    if (!force && state->options.pacmtoken_refresh_interval_seconds <= 0) {
        co_return false;
    }

    const std::uint64_t now = nowSeconds();
    bool should_refresh = true;
    {
        std::lock_guard<std::mutex> lock(state->pacmtoken_mutex);
        if (state->pacmtoken_refreshing) {
            should_refresh = false;
        } else if (!force && state->last_pacmtoken_refresh_at != 0 && now >= state->last_pacmtoken_refresh_at && now - state->last_pacmtoken_refresh_at < static_cast<std::uint64_t>(state->options.pacmtoken_refresh_interval_seconds)) {
            should_refresh = false;
        } else {
            state->pacmtoken_refreshing = true;
        }
    }
    if (!should_refresh) {
        co_return false;
    }

    auto refreshed = co_await runtime_->postBlocking<application::model::MiguPacmtokenRefreshResult>([store = pacmtoken_store_, refresher = pacmtoken_refresher_]() {
        application::model::MiguPacmtokenRefreshResult result;
        auto current = store->currentPacmtoken();
        if (!current || current->empty()) {
            return result;
        }
        result = refresher->refreshPacmtoken(*current);
        if (!result.success) {
            return result;
        }
        if (result.pacmtoken.empty()) {
            result.pacmtoken = *current;
        }
        if (result.updated_at == 0) {
            result.updated_at = nowSeconds();
        }
        result.pacmtoken_length = result.pacmtoken.size();
        store->updatePacmtoken(result.pacmtoken, result.updated_at);
        return result;
    });

    {
        std::lock_guard<std::mutex> lock(state->pacmtoken_mutex);
        state->pacmtoken_refreshing = false;
        if (refreshed.success) {
            state->last_pacmtoken_refresh_at = refreshed.updated_at == 0 ? nowSeconds() : refreshed.updated_at;
        }
    }
    if (refreshed.success) {
        co_await notifyPacmtokenRefreshed(listener, refreshed.refreshed, refreshed.pacmtoken_length, refreshed.updated_at);
    }
    co_return refreshed.success;
}

} // namespace orchestrator::modules::crawler::application::use_cases
