#include "crawler/application/use_cases/AsyncSongResourceDownloadWorkflow.h"

#include "ResourceDownloadSupport.h"
#include "ResourceDownloadState.h"

namespace orchestrator::modules::crawler::application::use_cases {
using namespace resource_detail; // 本用例内部的纯映射和事件格式规则

application::async::Task<application::model::SongResourceProcessItem> AsyncSongResourceDownloadWorkflow::processAudio(application::model::SongResourceQueueTarget target, const std::string &batch_id, std::shared_ptr<ProcessRunState> state, SongResourceProcessListener *listener)
{
    if (isKnownDigitalAlbum(target)) {
        co_return co_await skip(std::move(target), "resource", "digital_album", "digital album requires purchase before download", batch_id, listener);
    }
    if (!resource_provider_ || !resource_parser_) {
        co_return co_await fail(std::move(target), "invalid_target", "missing_dependency", "song resource provider or parser is not configured", batch_id, listener);
    }
    if (!validAudioTarget(target)) {
        co_return co_await fail(std::move(target), "invalid_target", "invalid_target", "missing contentId, resourceType, copyrightId or toneFlag", batch_id, listener);
    }

    const auto options = state ? state->options : application::model::SongResourceProcessOptions {};
    int max_attempts = (std::max)(1, options.resource_download_retries + 1);
    bool refreshed_after_440013 = false;
    application::model::SongResourceProcessItem last_failure;
    for (int attempt = 1; attempt <= max_attempts; ++attempt) {
        co_await notifyItemEvent(listener, makeStartedItem(target, false), "resourceAttemptStarted", stagePayload(target, "resource_attempt_started", "\"attempt\":" + std::to_string(attempt) + ",\"max_attempts\":" + std::to_string(max_attempts)));
        if (state) {
            (void)co_await refreshPacmtoken(state, listener, false);
        }

        co_await notifyItemEvent(listener, makeStartedItem(target, false), "resourceUrlRequestStarted", stagePayload(target, "resource_url_request_started", "\"attempt\":" + std::to_string(attempt)));
        auto response = co_await resource_provider_->fetchSongResource(toFetchRequest(target));
        if (!response || response->status_code >= 500) {
            co_await notifyItemEvent(listener, makeStartedItem(target, false), "resourceUrlRequestFailed", stagePayload(target, "resource_url_request_failed", "\"attempt\":" + std::to_string(attempt) + ",\"result_code\":\"resource_url_fetch_failed\""));
            last_failure = makeItem(target, false, false, false, false, "network", "resource_url_fetch_failed", "song resource url request failed");
            if (attempt < max_attempts) {
                co_await notifyItemEvent(listener, makeStartedItem(target, false), "resourceRetryScheduled", stagePayload(target, "resource_retry_scheduled", "\"attempt\":" + std::to_string(attempt) + ",\"next_attempt\":" + std::to_string(attempt + 1) + ",\"max_attempts\":" + std::to_string(max_attempts) + ",\"failed_stage\":\"resource_url_request\",\"result_code\":\"resource_url_fetch_failed\",\"error_category\":\"network\""));
                continue;
            }
            co_await notifyItemEvent(listener, makeStartedItem(target, false), "resourceAttemptFinished", stagePayload(target, "resource_attempt_finished", "\"attempt\":" + std::to_string(attempt) + ",\"result_code\":\"" + escapeJson(last_failure.result_code) + "\",\"error_category\":\"" + escapeJson(last_failure.error_category) + "\""));
            co_return co_await fail(std::move(target), last_failure.error_category, last_failure.result_code, last_failure.message, batch_id, listener);
        }

        auto fetch_result = resource_parser_->parseSongResource(response->body);
        if (!fetch_result.ok) {
            std::string code = fetch_result.code.empty() ? "parse_failed" : fetch_result.code;
            std::string message = fetch_result.message.empty() ? "song resource response did not include a usable url" : fetch_result.message;
            if (fetch_result.code == "000000" && fetch_result.url.empty()) {
                code = "missing_resource_url";
                message = "song resource response did not include a usable url";
            }
            if (isPermanentResourceSkip(code)) {
                co_await notifyItemEvent(listener, makeStartedItem(target, false), "resourceUrlRejected", stagePayload(target, "resource_url_rejected", "\"attempt\":" + std::to_string(attempt) + ",\"result_code\":\"" + escapeJson(code) + "\""));
                const std::string category = permanentResourceSkipCategory(code);
                if (state && resource_queue_ && options.process_related_resources) {
                    co_await processRelatedResources(target, fetch_result, batch_id, state, listener);
                }
                co_await notifyItemEvent(listener, makeStartedItem(target, false), "resourceAttemptFinished", stagePayload(target, "resource_attempt_finished", "\"attempt\":" + std::to_string(attempt) + ",\"result_code\":\"" + escapeJson(code) + "\",\"error_category\":\"" + escapeJson(category) + "\""));
                co_return co_await skip(std::move(target), category, std::move(code), std::move(message), batch_id, listener);
            }
            if (isResourceRestrictionFailure(code) && options.refresh_pacmtoken_on_440013 && state && !refreshed_after_440013) {
                co_await notifyItemEvent(listener, makeStartedItem(target, false), "resourceUrlRestricted", stagePayload(target, "resource_url_restricted", "\"attempt\":" + std::to_string(attempt) + ",\"result_code\":\"" + escapeJson(code) + "\""));
                refreshed_after_440013 = true;
                if (co_await refreshPacmtoken(state, listener, true)) {
                    if (attempt == max_attempts) {
                        ++max_attempts;
                    }
                    continue;
                }
            }
            if (isResourceRestrictionFailure(code)) {
                co_await notifyItemEvent(listener, makeStartedItem(target, false), "resourceUrlRejected", stagePayload(target, "resource_url_rejected", "\"attempt\":" + std::to_string(attempt) + ",\"result_code\":\"" + escapeJson(code) + "\""));
                co_await notifyItemEvent(listener, makeStartedItem(target, false), "resourceAttemptFinished", stagePayload(target, "resource_attempt_finished", "\"attempt\":" + std::to_string(attempt) + ",\"result_code\":\"" + escapeJson(code) + "\",\"error_category\":\"resource\""));
                co_return co_await fail(std::move(target), "resource", std::move(code), std::move(message), batch_id, listener);
            }
            co_await notifyItemEvent(listener, makeStartedItem(target, false), "resourceUrlParseFailed", stagePayload(target, "resource_url_parse_failed", "\"attempt\":" + std::to_string(attempt) + ",\"result_code\":\"" + escapeJson(code) + "\""));
            co_await notifyItemEvent(listener, makeStartedItem(target, false), "resourceAttemptFinished", stagePayload(target, "resource_attempt_finished", "\"attempt\":" + std::to_string(attempt) + ",\"result_code\":\"" + escapeJson(code) + "\",\"error_category\":\"parse\""));
            co_return co_await fail(std::move(target), "parse", std::move(code), std::move(message), batch_id, listener);
        }

        co_await notifyItemEvent(listener, makeStartedItem(target, false), "resourceUrlFetched", stagePayload(target, "resource_url_fetched", "\"attempt\":" + std::to_string(attempt) + ",\"audio_format_type\":\"" + escapeJson(fetch_result.audio_format_type) + "\"" + ",\"version\":\"" + escapeJson(fetch_result.version) + "\"" + ",\"has_lyric\":" + (fetch_result.lyric_url.empty() ? "false" : "true") + ",\"has_mrc\":" + (fetch_result.mrc_url.empty() ? "false" : "true") + ",\"has_trc\":" + (fetch_result.trc_url.empty() ? "false" : "true")));

        auto item = co_await downloadAndStoreAttempt(target, fetch_result.url, batch_id, options, attempt == max_attempts, listener);
        if (item.ok && resource_queue_ && options.process_related_resources) {
            co_await processRelatedResources(target, fetch_result, batch_id, state, listener);
            co_await notifyItemEvent(listener, makeStartedItem(target, false), "resourceAttemptFinished", stagePayload(target, "resource_attempt_finished", "\"attempt\":" + std::to_string(attempt) + ",\"result_code\":\"ok\""));
            co_return item;
        }
        if (item.ok) {
            co_await notifyItemEvent(listener, makeStartedItem(target, false), "resourceAttemptFinished", stagePayload(target, "resource_attempt_finished", "\"attempt\":" + std::to_string(attempt) + ",\"result_code\":\"ok\""));
            co_return item;
        }
        last_failure = item;
        if (attempt < max_attempts && retryableResourceFailure(item)) {
            co_await notifyItemEvent(listener, makeStartedItem(target, false), "resourceRetryScheduled", stagePayload(target, "resource_retry_scheduled", "\"attempt\":" + std::to_string(attempt) + ",\"next_attempt\":" + std::to_string(attempt + 1) + ",\"max_attempts\":" + std::to_string(max_attempts) + ",\"failed_stage\":\"resource_content_download\",\"result_code\":\"" + escapeJson(item.result_code) + "\",\"error_category\":\"" + escapeJson(item.error_category) + "\""));
            continue;
        }
        if (!item.ok && !item.skipped && attempt < max_attempts) {
            co_await notifyItemEvent(listener, makeStartedItem(target, false), "resourceAttemptFinished", stagePayload(target, "resource_attempt_finished", "\"attempt\":" + std::to_string(attempt) + ",\"result_code\":\"" + escapeJson(item.result_code) + "\",\"error_category\":\"" + escapeJson(item.error_category) + "\""));
            co_return co_await fail(std::move(target), item.error_category, item.result_code, item.message, batch_id, listener);
        }
        co_await notifyItemEvent(listener, makeStartedItem(target, false), "resourceAttemptFinished", stagePayload(target, "resource_attempt_finished", "\"attempt\":" + std::to_string(attempt) + ",\"result_code\":\"" + escapeJson(item.result_code) + "\",\"error_category\":\"" + escapeJson(item.error_category) + "\""));
        co_return item;
    }

    co_return co_await fail(std::move(target), last_failure.error_category.empty() ? "unknown" : last_failure.error_category, last_failure.result_code.empty() ? "failed" : last_failure.result_code, last_failure.message.empty() ? "resource processing failed" : last_failure.message, batch_id, listener);
}

application::async::Task<application::model::SongResourceProcessItem> AsyncSongResourceDownloadWorkflow::processDirectResource(application::model::SongResourceQueueTarget target, const std::string &batch_id, const application::model::SongResourceProcessOptions &options, SongResourceProcessListener *listener)
{
    if (target.remote_url.empty()) {
        if (!isRelatedResource(target.resource_kind) || !resource_provider_ || !resource_parser_ || !validAudioTarget(target)) {
            co_return co_await fail(std::move(target), "invalid_target", "missing_remote_url", "resource remote_url is empty", batch_id, listener);
        }
        co_await notifyItemEvent(listener, makeStartedItem(target, true), "resourceUrlRequestStarted", stagePayload(target, "resource_url_request_started", "\"reason\":\"missing_remote_url\""));
        auto response = co_await resource_provider_->fetchSongResource(toFetchRequest(target));
        if (!response) {
            co_await notifyItemEvent(listener, makeStartedItem(target, true), "resourceUrlRequestFailed", stagePayload(target, "resource_url_request_failed", "\"result_code\":\"resource_url_fetch_failed\""));
            co_return co_await fail(std::move(target), "network", "resource_url_fetch_failed", "song resource url request failed", batch_id, listener);
        }
        auto fetch_result = resource_parser_->parseSongResource(response->body);
        const std::string resolved_url = relatedResourceUrl(fetch_result, target.resource_kind);
        if (resolved_url.empty()) {
            std::string code = fetch_result.code.empty() ? "missing_related_resource_url" : fetch_result.code;
            std::string message = fetch_result.message.empty() ? "song resource response did not include related resource url" : fetch_result.message;
            if (code == "000000") {
                code = "missing_related_resource_url";
                message = "song resource response did not include related resource url";
            }
            co_await notifyItemEvent(listener, makeStartedItem(target, true), "resourceUrlParseFailed", stagePayload(target, "resource_url_parse_failed", "\"result_code\":\"" + escapeJson(code) + "\""));
            if (isPermanentResourceSkip(code)) {
                const std::string category = permanentResourceSkipCategory(code);
                co_return co_await skip(std::move(target), category, std::move(code), std::move(message), batch_id, listener);
            }
            co_return co_await fail(std::move(target), "resource", std::move(code), std::move(message), batch_id, listener);
        }
        target.remote_url = resolved_url;
        co_await notifyItemEvent(listener, makeStartedItem(target, true), "resourceUrlFetched", stagePayload(target, "resource_url_fetched", "\"reason\":\"missing_remote_url\",\"has_cover\":" + std::string(fetch_result.cover_url.empty() ? "false" : "true") + ",\"has_lyric\":" + (fetch_result.lyric_url.empty() ? "false" : "true") + ",\"has_mrc\":" + (fetch_result.mrc_url.empty() ? "false" : "true") + ",\"has_trc\":" + (fetch_result.trc_url.empty() ? "false" : "true")));
    }
    std::string remote_url = target.remote_url;
    const int max_attempts = (std::max)(1, options.resource_download_retries + 1);
    application::model::SongResourceProcessItem last_failure;
    for (int attempt = 1; attempt <= max_attempts; ++attempt) {
        co_await notifyItemEvent(listener, makeStartedItem(target, isRelatedResource(target.resource_kind)), "resourceAttemptStarted", stagePayload(target, "resource_attempt_started", "\"attempt\":" + std::to_string(attempt) + ",\"max_attempts\":" + std::to_string(max_attempts)));
        auto item = co_await downloadAndStoreAttempt(target, remote_url, batch_id, options, attempt == max_attempts, listener);
        if (item.ok || item.skipped) {
            if (item.ok) co_await postProcessCompleted(item, listener);
            co_await notifyItemEvent(listener, makeStartedItem(target, isRelatedResource(target.resource_kind)), "resourceAttemptFinished", stagePayload(target, "resource_attempt_finished", "\"attempt\":" + std::to_string(attempt) + ",\"result_code\":\"" + escapeJson(item.result_code) + "\",\"error_category\":\"" + escapeJson(item.error_category) + "\""));
            co_return item;
        }
        last_failure = item;
        if (attempt < max_attempts && retryableResourceFailure(item)) {
            co_await notifyItemEvent(listener, makeStartedItem(target, isRelatedResource(target.resource_kind)), "resourceRetryScheduled", stagePayload(target, "resource_retry_scheduled", "\"attempt\":" + std::to_string(attempt) + ",\"next_attempt\":" + std::to_string(attempt + 1) + ",\"max_attempts\":" + std::to_string(max_attempts) + ",\"failed_stage\":\"resource_content_download\",\"result_code\":\"" + escapeJson(item.result_code) + "\",\"error_category\":\"" + escapeJson(item.error_category) + "\""));
            continue;
        }
        if (attempt < max_attempts) {
            co_await notifyItemEvent(listener, makeStartedItem(target, isRelatedResource(target.resource_kind)), "resourceAttemptFinished", stagePayload(target, "resource_attempt_finished", "\"attempt\":" + std::to_string(attempt) + ",\"result_code\":\"" + escapeJson(item.result_code) + "\",\"error_category\":\"" + escapeJson(item.error_category) + "\""));
            co_return co_await fail(std::move(target), item.error_category, item.result_code, item.message, batch_id, listener);
        }
        co_await notifyItemEvent(listener, makeStartedItem(target, isRelatedResource(target.resource_kind)), "resourceAttemptFinished", stagePayload(target, "resource_attempt_finished", "\"attempt\":" + std::to_string(attempt) + ",\"result_code\":\"" + escapeJson(item.result_code) + "\",\"error_category\":\"" + escapeJson(item.error_category) + "\""));
        co_return item;
    }
    co_return co_await fail(std::move(target), last_failure.error_category.empty() ? "unknown" : last_failure.error_category, last_failure.result_code.empty() ? "failed" : last_failure.result_code, last_failure.message.empty() ? "resource processing failed" : last_failure.message, batch_id, listener);
}

} // namespace orchestrator::modules::crawler::application::use_cases
