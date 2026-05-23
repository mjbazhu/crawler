#include "crawler/application/use_cases/AsyncSongResourceDownloadWorkflow.h"

#include <algorithm>
#include <array>
#include <atomic>
#include <chrono>
#include <ctime>
#include <exception>
#include <functional>
#include <memory>
#include <mutex>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace orchestrator::modules::crawler::application::use_cases {

namespace {

std::string trimMessage(std::string message)
{
    constexpr std::size_t kMaxMessageLength = 512;
    if (message.size() > kMaxMessageLength) {
        message.resize(kMaxMessageLength);
    }
    return message;
}

std::string escapeJson(std::string_view value)
{
    std::string escaped;
    escaped.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
        case '"': escaped += "\\\""; break;
        case '\\': escaped += "\\\\"; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped.push_back(ch); break;
        }
    }
    return escaped;
}

std::string stagePayload(const application::model::SongResourceQueueTarget &target, std::string_view stage, std::string_view extra = {})
{
    std::string payload = "{\"stage\":\"" + escapeJson(stage) + "\""
                          + ",\"resource_id\":" + std::to_string(target.resource_id)
                          + ",\"song_source_ref_id\":" + std::to_string(target.song_source_ref_id)
                          + ",\"content_id\":\"" + escapeJson(target.source_content_id) + "\""
                          + ",\"resource_kind\":\"" + escapeJson(target.resource_kind) + "\""
                          + ",\"format_type\":\"" + escapeJson(target.format_type) + "\"";
    if (!extra.empty()) {
        payload += ",";
        payload.append(extra);
    }
    payload += "}";
    return payload;
}

bool isAudioResource(const std::string &resource_kind)
{
    return resource_kind == "audio";
}

bool isRelatedTextResource(const std::string &resource_kind)
{
    return resource_kind == "lyric" || resource_kind == "mrc" || resource_kind == "trc";
}

bool isKnownDigitalAlbum(const application::model::SongResourceQueueTarget &target)
{
    return target.source_album_kind == 2;
}

bool isPermanentResourceSkip(const std::string &code)
{
    return code == "440000" || code == "440015" || code == "440016";
}

std::string permanentResourceSkipCategory(const std::string &code)
{
    if (code == "440000") {
        return "copyright";
    }
    return "resource";
}

bool isResourceRestrictionFailure(const std::string &code)
{
    return code == "440013";
}

application::model::SongResourceProcessItem makeItem(const application::model::SongResourceQueueTarget &target, bool ok, bool skipped, bool related, bool synthetic, std::string category, std::string code, std::string message)
{
    application::model::SongResourceProcessItem item;
    item.resource_id = target.resource_id;
    item.song_source_ref_id = target.song_source_ref_id;
    item.source = target.source;
    item.source_song_id = target.source_song_id;
    item.source_content_id = target.source_content_id;
    item.title = target.title;
    item.resource_kind = target.resource_kind;
    item.format_type = target.format_type;
    item.ok = ok;
    item.skipped = skipped;
    item.related = related;
    item.synthetic = synthetic;
    item.error_category = ok ? "" : std::move(category);
    item.result_code = std::move(code);
    item.message = trimMessage(std::move(message));
    return item;
}

application::model::SongResourceProcessItem makeStartedItem(const application::model::SongResourceQueueTarget &target, bool related)
{
    return makeItem(target, false, false, related, false, {}, {}, {});
}

application::model::SongResourceProcessItem makeRelatedSkippedItem(const application::model::SongResourceQueueTarget &audio_target, const std::string &kind)
{
    application::model::SongResourceQueueTarget target = audio_target;
    target.resource_id = 0;
    target.resource_kind = kind;
    target.format_type.clear();
    return makeItem(target, false, true, true, true, "resource", "related_not_claimed", "related resource task not found or already processed");
}

std::string relatedResourceKey(const application::model::SongResourceQueueTarget &audio_target, const std::string &kind)
{
    return std::to_string(audio_target.song_source_ref_id) + ":" + audio_target.source_song_id + ":" + kind;
}

application::model::SongResourceFetchRequest toFetchRequest(const application::model::SongResourceQueueTarget &target)
{
    application::model::SongResourceFetchRequest request;
    request.content_id = target.source_content_id;
    request.resource_type = target.audio_resource_type.empty() ? target.source_resource_type : target.audio_resource_type;
    request.copyright_id = target.copyright_id;
    request.tone_flag = target.format_type;
    return request;
}

bool validAudioTarget(const application::model::SongResourceQueueTarget &target)
{
    const auto request = toFetchRequest(target);
    return !request.content_id.empty() && !request.resource_type.empty() && !request.copyright_id.empty() && !request.tone_flag.empty();
}

application::model::SongResourcePathRequest toPathRequest(const application::model::SongResourceQueueTarget &target, const std::string &remote_url, const std::string &content_type)
{
    application::model::SongResourcePathRequest request;
    request.resource_id = target.resource_id;
    request.song_source_ref_id = target.song_source_ref_id;
    request.source = target.source;
    request.source_content_id = target.source_content_id;
    request.source_song_id = target.source_song_id;
    request.resource_kind = target.resource_kind;
    request.format_type = target.format_type;
    request.remote_url = remote_url;
    request.content_type = content_type;
    return request;
}

std::uint64_t nowSeconds()
{
    return static_cast<std::uint64_t>(std::time(nullptr));
}

bool retryableResourceFailure(const application::model::SongResourceProcessItem &item)
{
    if (item.ok || item.skipped) {
        return false;
    }
    if (item.error_category == "network" || item.error_category == "timeout") {
        return true;
    }
    if (item.result_code == "network_failed" || item.result_code == "timeout") {
        return true;
    }
    return item.result_code.rfind("http_status_5", 0) == 0;
}

std::size_t normalizedResourceConcurrency(const application::model::SongResourceProcessOptions &options)
{
    return (std::min<std::size_t>)((std::max<std::size_t>)(1, options.resource_download_concurrency), 32);
}

} // namespace

struct AsyncSongResourceDownloadWorkflow::ProcessRunState {
    application::model::SongResourceProcessOptions options;          // 本次批处理有效选项
    application::model::SongResourceProcessResult result;            // 批次聚合结果
    std::mutex result_mutex;                                         // 保护 result
    std::mutex related_mutex;                                        // 保护关联资源去重
    std::mutex pacmtoken_mutex;                                      // 保护 pacmtoken 刷新状态
    std::unordered_set<std::string> processed_related_keys;          // 同一批次内关联资源去重键
    std::uint64_t last_pacmtoken_refresh_at = 0;                     // 最近刷新时间
    bool pacmtoken_refreshing = false;                               // 是否已有协程在刷新 token
};

AsyncSongResourceDownloadWorkflow::AsyncSongResourceDownloadWorkflow(
    std::shared_ptr<application::ports::AsyncRuntime> runtime,
    std::shared_ptr<application::ports::AsyncSongResourceQueue> resource_queue,
    std::shared_ptr<application::ports::AsyncSongResourceProvider> resource_provider,
    std::shared_ptr<application::ports::SongResourceParser> resource_parser,
    std::shared_ptr<application::ports::AsyncSongResourceContentFetcher> content_fetcher,
    std::shared_ptr<application::ports::AsyncSongResourceBlobStore> blob_store,
    SongResourcePathPlanner path_planner,
    std::shared_ptr<application::ports::MiguPacmtokenRefresher> pacmtoken_refresher,
    std::shared_ptr<application::ports::MiguPacmtokenStore> pacmtoken_store
) : runtime_(std::move(runtime)),
    resource_queue_(std::move(resource_queue)),
    resource_provider_(std::move(resource_provider)),
    resource_parser_(std::move(resource_parser)),
    content_fetcher_(std::move(content_fetcher)),
    blob_store_(std::move(blob_store)),
    path_planner_(std::move(path_planner)),
    pacmtoken_refresher_(std::move(pacmtoken_refresher)),
    pacmtoken_store_(std::move(pacmtoken_store))
{}

application::async::Task<application::model::SongResourceProcessResult> AsyncSongResourceDownloadWorkflow::processPending(application::model::SongResourceClaimQuery query, SongResourceProcessListener *listener)
{
    auto state = std::make_shared<ProcessRunState>();
    state->result.batch_id = query.batch_id;
    state->options = query.options;
    if (!runtime_ || !resource_queue_ || !content_fetcher_ || !blob_store_) {
        co_return state->result;
    }
    if (query.source.empty()) {
        query.source = "migu";
    }
    if (query.limit == 0) {
        query.limit = 20;
    }
    state->result.batch_id = query.batch_id;

    if (state->options.auto_refresh_pacmtoken) {
        (void)co_await refreshPacmtoken(state, listener, true);
    }

    auto targets = co_await resource_queue_->claimPending(query);
    {
        std::lock_guard<std::mutex> lock(state->result_mutex);
        state->result.claimed_count = static_cast<int>(targets.size());
    }
    if (targets.empty()) {
        co_return state->result;
    }

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
    application::model::SongResourceProcessResult result;
    {
        std::lock_guard<std::mutex> lock(state->result_mutex);
        result = state->result;
    }
    co_return result;
}

application::async::Task<void> AsyncSongResourceDownloadWorkflow::processWorker(std::shared_ptr<ProcessRunState> state, application::model::SongResourceQueueTarget target, std::string batch_id, SongResourceProcessListener *listener)
{
    application::model::SongResourceProcessItem item;
    try {
        co_await notifyItemStarted(listener, makeStartedItem(target, false));
        item = isAudioResource(target.resource_kind) ? co_await processAudio(target, batch_id, state, listener) : co_await processDirectResource(target, batch_id, state->options, listener);
    } catch (const std::exception &e) {
        item = makeItem(target, false, false, false, false, "unknown", "worker_failed", e.what());
    } catch (...) {
        item = makeItem(target, false, false, false, false, "unknown", "worker_failed", "resource worker failed with unknown error");
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

application::async::Task<application::model::SongResourceProcessItem> AsyncSongResourceDownloadWorkflow::processAudio(application::model::SongResourceQueueTarget target, const std::string &batch_id, std::shared_ptr<ProcessRunState> state, SongResourceProcessListener *listener)
{
    if (isKnownDigitalAlbum(target)) {
        co_return co_await skip(std::move(target), "resource", "digital_album", "digital album requires purchase before download", batch_id);
    }
    if (!resource_provider_ || !resource_parser_) {
        co_return co_await fail(std::move(target), "invalid_target", "missing_dependency", "song resource provider or parser is not configured", batch_id);
    }
    if (!validAudioTarget(target)) {
        co_return co_await fail(std::move(target), "invalid_target", "invalid_target", "missing contentId, resourceType, copyrightId or toneFlag", batch_id);
    }

    const auto options = state ? state->options : application::model::SongResourceProcessOptions {};
    int max_attempts = (std::max)(1, options.resource_download_retries + 1);
    bool refreshed_after_440013 = false;
    application::model::SongResourceProcessItem last_failure;
    for (int attempt = 1; attempt <= max_attempts; ++attempt) {
        if (state) {
            (void)co_await refreshPacmtoken(state, listener, false);
        }

        co_await notifyItemEvent(listener, makeStartedItem(target, false), "resourceUrlRequestStarted", stagePayload(target, "resource_url_request_started", "\"attempt\":" + std::to_string(attempt)));
        auto response = co_await resource_provider_->fetchSongResource(toFetchRequest(target));
        if (!response) {
            co_await notifyItemEvent(listener, makeStartedItem(target, false), "resourceUrlRequestFailed", stagePayload(target, "resource_url_request_failed", "\"attempt\":" + std::to_string(attempt) + ",\"result_code\":\"resource_url_fetch_failed\""));
            last_failure = makeItem(target, false, false, false, false, "network", "resource_url_fetch_failed", "song resource url request failed");
            if (attempt < max_attempts) {
                continue;
            }
            co_return co_await fail(std::move(target), last_failure.error_category, last_failure.result_code, last_failure.message, batch_id);
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
                co_return co_await skip(std::move(target), category, std::move(code), std::move(message), batch_id);
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
                co_return co_await fail(std::move(target), "resource", std::move(code), std::move(message), batch_id);
            }
            co_await notifyItemEvent(listener, makeStartedItem(target, false), "resourceUrlParseFailed", stagePayload(target, "resource_url_parse_failed", "\"attempt\":" + std::to_string(attempt) + ",\"result_code\":\"" + escapeJson(code) + "\""));
            co_return co_await fail(std::move(target), "parse", std::move(code), std::move(message), batch_id);
        }

        co_await notifyItemEvent(listener, makeStartedItem(target, false), "resourceUrlFetched", stagePayload(target, "resource_url_fetched", "\"attempt\":" + std::to_string(attempt) + ",\"audio_format_type\":\"" + escapeJson(fetch_result.audio_format_type) + "\"" + ",\"version\":\"" + escapeJson(fetch_result.version) + "\"" + ",\"has_lyric\":" + (fetch_result.lyric_url.empty() ? "false" : "true") + ",\"has_mrc\":" + (fetch_result.mrc_url.empty() ? "false" : "true") + ",\"has_trc\":" + (fetch_result.trc_url.empty() ? "false" : "true")));

        auto item = co_await downloadAndStoreAttempt(target, fetch_result.url, batch_id, options, attempt == max_attempts, listener);
        if (item.ok && resource_queue_) {
            co_await processRelatedTextResources(target, fetch_result, batch_id, state, listener);
            co_return item;
        }
        last_failure = item;
        if (attempt < max_attempts && retryableResourceFailure(item)) {
            continue;
        }
        if (!item.ok && !item.skipped && attempt < max_attempts) {
            co_return co_await fail(std::move(target), item.error_category, item.result_code, item.message, batch_id);
        }
        co_return item;
    }

    co_return co_await fail(std::move(target), last_failure.error_category.empty() ? "unknown" : last_failure.error_category, last_failure.result_code.empty() ? "failed" : last_failure.result_code, last_failure.message.empty() ? "resource processing failed" : last_failure.message, batch_id);
}

application::async::Task<application::model::SongResourceProcessItem> AsyncSongResourceDownloadWorkflow::processDirectResource(application::model::SongResourceQueueTarget target, const std::string &batch_id, const application::model::SongResourceProcessOptions &options, SongResourceProcessListener *listener)
{
    if (target.remote_url.empty()) {
        co_return co_await fail(std::move(target), "invalid_target", "missing_remote_url", "resource remote_url is empty", batch_id);
    }
    std::string remote_url = target.remote_url;
    const int max_attempts = (std::max)(1, options.resource_download_retries + 1);
    application::model::SongResourceProcessItem last_failure;
    for (int attempt = 1; attempt <= max_attempts; ++attempt) {
        auto item = co_await downloadAndStoreAttempt(target, remote_url, batch_id, options, attempt == max_attempts, listener);
        if (item.ok || item.skipped) {
            co_return item;
        }
        last_failure = item;
        if (attempt < max_attempts && retryableResourceFailure(item)) {
            continue;
        }
        if (attempt < max_attempts) {
            co_return co_await fail(std::move(target), item.error_category, item.result_code, item.message, batch_id);
        }
        co_return item;
    }
    co_return co_await fail(std::move(target), last_failure.error_category.empty() ? "unknown" : last_failure.error_category, last_failure.result_code.empty() ? "failed" : last_failure.result_code, last_failure.message.empty() ? "resource processing failed" : last_failure.message, batch_id);
}

application::async::Task<application::model::SongResourceProcessItem> AsyncSongResourceDownloadWorkflow::downloadAndStoreAttempt(application::model::SongResourceQueueTarget target, std::string remote_url, const std::string &batch_id, const application::model::SongResourceProcessOptions &options, bool write_failure, SongResourceProcessListener *listener)
{
    if (!content_fetcher_ || !blob_store_) {
        co_return write_failure ? co_await fail(std::move(target), "invalid_target", "missing_dependency", "content fetcher or blob store is not configured", batch_id) : makeItem(target, false, false, false, false, "invalid_target", "missing_dependency", "content fetcher or blob store is not configured");
    }

    application::model::SongResourceContentFetchRequest fetch_request;
    fetch_request.url = remote_url;
    fetch_request.timeout_ms = options.resource_download_timeout_ms;
    co_await notifyItemEvent(listener, makeStartedItem(target, isRelatedTextResource(target.resource_kind)), "resourceContentDownloadStarted", stagePayload(target, "resource_content_download_started"));

    struct WriterState {
        std::unique_ptr<application::ports::AsyncBlobWriteSession> writer; // 异步写入会话
        application::model::SongResourcePathResult path;                   // 规划后的路径
        std::string content_type;                                          // 响应 Content-Type
        std::string failure_category;                                      // 下游失败分类
        std::string failure_code;                                          // 下游失败编码
        std::string failure_message;                                       // 下游失败说明
    };
    auto writer_state = std::make_shared<WriterState>();

    application::ports::AsyncSongResourceContentStreamCallbacks callbacks;
    callbacks.on_response = [this, writer_state, target, remote_url, listener](const application::model::SongResourceContentStreamInfo &info) mutable -> application::async::Task<bool> {
        writer_state->content_type = info.content_type;
        writer_state->path = path_planner_.plan(toPathRequest(target, remote_url, writer_state->content_type));
        if (!writer_state->path.ok) {
            writer_state->failure_category = "persistence";
            writer_state->failure_code = "path_plan_failed";
            writer_state->failure_message = writer_state->path.error.empty() ? "resource local path planning failed" : writer_state->path.error;
            co_await notifyItemEvent(listener, makeStartedItem(target, isRelatedTextResource(target.resource_kind)), "resourcePathPlanFailed", stagePayload(target, "resource_path_plan_failed", "\"result_code\":\"path_plan_failed\",\"message\":\"" + escapeJson(writer_state->failure_message) + "\""));
            co_return false;
        }
        co_await notifyItemEvent(listener, makeStartedItem(target, isRelatedTextResource(target.resource_kind)), "resourcePathPlanned", stagePayload(target, "resource_path_planned", "\"local_path\":\"" + escapeJson(writer_state->path.relative_path) + "\",\"extension\":\"" + escapeJson(writer_state->path.extension) + "\""));

        application::model::SongResourceBlobStreamSaveRequest save_request;
        save_request.relative_path_hint = writer_state->path.relative_path;
        save_request.content_type = writer_state->content_type;
        writer_state->writer = co_await blob_store_->open(std::move(save_request));
        if (!writer_state->writer) {
            writer_state->failure_category = "persistence";
            writer_state->failure_code = "persist_failed";
            writer_state->failure_message = "resource blob stream open failed";
            co_await notifyItemEvent(listener, makeStartedItem(target, isRelatedTextResource(target.resource_kind)), "resourceBlobSaveFailed", stagePayload(target, "resource_blob_save_failed", "\"result_code\":\"persist_failed\",\"message\":\"" + escapeJson(writer_state->failure_message) + "\""));
            co_return false;
        }
        co_await notifyItemEvent(listener, makeStartedItem(target, isRelatedTextResource(target.resource_kind)), "resourceBlobSaveStarted", stagePayload(target, "resource_blob_save_started", "\"local_path\":\"" + escapeJson(writer_state->path.relative_path) + "\""));
        co_return true;
    };
    callbacks.on_chunk = [writer_state](std::span<const char> chunk) mutable -> application::async::Task<bool> {
        if (!writer_state->writer) {
            writer_state->failure_category = "persistence";
            writer_state->failure_code = "persist_failed";
            writer_state->failure_message = "resource blob stream was not opened";
            co_return false;
        }
        auto written = co_await writer_state->writer->write(chunk);
        if (!written.ok) {
            writer_state->failure_category = "persistence";
            writer_state->failure_code = "persist_failed";
            writer_state->failure_message = written.error.empty() ? "resource blob stream write failed" : written.error;
            co_return false;
        }
        co_return true;
    };

    auto content = co_await content_fetcher_->streamSongResourceContent(std::move(fetch_request), std::move(callbacks));
    if (!content.ok) {
        if (writer_state->writer) {
            co_await writer_state->writer->abort();
        }
        const std::string category = !writer_state->failure_category.empty() ? writer_state->failure_category : (content.error_category.empty() ? "network" : content.error_category);
        const std::string code = !writer_state->failure_code.empty() ? writer_state->failure_code : (content.result_code.empty() ? "download_failed" : content.result_code);
        const std::string message = !writer_state->failure_message.empty() ? writer_state->failure_message : (content.message.empty() ? "resource content download failed" : content.message);
        co_await notifyItemEvent(listener, makeStartedItem(target, isRelatedTextResource(target.resource_kind)), "resourceContentDownloadFailed", stagePayload(target, "resource_content_download_failed", "\"result_code\":\"" + escapeJson(code) + "\",\"error_category\":\"" + escapeJson(category) + "\""));
        co_return write_failure ? co_await fail(std::move(target), category, code, message, batch_id) : makeItem(target, false, false, false, false, category, code, message);
    }
    co_await notifyItemEvent(listener, makeStartedItem(target, isRelatedTextResource(target.resource_kind)), "resourceContentDownloaded", stagePayload(target, "resource_content_downloaded", "\"status_code\":" + std::to_string(content.status_code) + ",\"content_type\":\"" + escapeJson(content.content_type) + "\"" + ",\"body_size\":" + std::to_string(content.bytes_received)));

    if (!writer_state->writer) {
        const std::string message = "resource blob stream was not opened";
        co_await notifyItemEvent(listener, makeStartedItem(target, isRelatedTextResource(target.resource_kind)), "resourceBlobSaveFailed", stagePayload(target, "resource_blob_save_failed", "\"result_code\":\"persist_failed\",\"message\":\"" + escapeJson(message) + "\""));
        co_return write_failure ? co_await fail(std::move(target), "persistence", "persist_failed", message, batch_id) : makeItem(target, false, false, false, false, "persistence", "persist_failed", message);
    }

    auto saved = co_await writer_state->writer->commit();
    if (!saved.ok) {
        const std::string message = saved.error.empty() ? "resource blob save failed" : saved.error;
        co_await notifyItemEvent(listener, makeStartedItem(target, isRelatedTextResource(target.resource_kind)), "resourceBlobSaveFailed", stagePayload(target, "resource_blob_save_failed", "\"result_code\":\"persist_failed\",\"message\":\"" + escapeJson(message) + "\""));
        co_return write_failure ? co_await fail(std::move(target), "persistence", "persist_failed", message, batch_id) : makeItem(target, false, false, false, false, "persistence", "persist_failed", message);
    }
    if (saved.local_path.empty() || saved.size_bytes == 0 || saved.size_bytes != content.bytes_received || saved.checksum.empty()) {
        co_await notifyItemEvent(listener, makeStartedItem(target, isRelatedTextResource(target.resource_kind)), "resourceBlobValidationFailed", stagePayload(target, "resource_blob_validation_failed", "\"result_code\":\"persist_validation_failed\"" + std::string(",\"local_path\":\"") + escapeJson(saved.local_path) + "\"" + ",\"downloaded_size\":" + std::to_string(content.bytes_received) + ",\"saved_size\":" + std::to_string(saved.size_bytes)));
        co_return write_failure ? co_await fail(std::move(target), "persistence", "persist_validation_failed", "resource blob save result did not match downloaded content", batch_id) : makeItem(target, false, false, false, false, "persistence", "persist_validation_failed", "resource blob save result did not match downloaded content");
    }
    co_await notifyItemEvent(listener, makeStartedItem(target, isRelatedTextResource(target.resource_kind)), "resourceBlobSaved", stagePayload(target, "resource_blob_saved", "\"local_path\":\"" + escapeJson(saved.local_path) + "\"" + ",\"size_bytes\":" + std::to_string(saved.size_bytes) + ",\"checksum\":\"" + escapeJson(saved.checksum) + "\""));

    application::model::SongResourceSuccessUpdate update;
    update.resource_id = target.resource_id;
    update.remote_url = std::move(remote_url);
    update.local_path = saved.local_path;
    update.size_bytes = saved.size_bytes;
    update.checksum = saved.checksum;
    update.result_code = "ok";
    update.batch_id = batch_id;
    if (!resource_queue_ || !(co_await resource_queue_->markSucceeded(std::move(update)))) {
        co_await notifyItemEvent(listener, makeStartedItem(target, isRelatedTextResource(target.resource_kind)), "resourceDbWritebackFailed", stagePayload(target, "resource_db_writeback_failed", "\"result_code\":\"writeback_failed\",\"local_path\":\"" + escapeJson(saved.local_path) + "\""));
        co_return write_failure ? co_await fail(std::move(target), "persistence", "writeback_failed", "resource success writeback failed", batch_id) : makeItem(target, false, false, false, false, "persistence", "writeback_failed", "resource success writeback failed");
    }
    co_await notifyItemEvent(listener, makeStartedItem(target, isRelatedTextResource(target.resource_kind)), "resourceDbWritebackCompleted", stagePayload(target, "resource_db_writeback_completed", "\"local_path\":\"" + escapeJson(saved.local_path) + "\",\"size_bytes\":" + std::to_string(saved.size_bytes)));

    auto item = makeItem(target, true, false, false, false, {}, "ok", {});
    item.local_path = std::move(saved.local_path);
    item.size_bytes = saved.size_bytes;
    item.checksum = std::move(saved.checksum);
    co_return item;
}

application::async::Task<void> AsyncSongResourceDownloadWorkflow::processRelatedTextResources(const application::model::SongResourceQueueTarget &audio_target, const application::model::SongResourceFetchResult &fetch_result, const std::string &batch_id, std::shared_ptr<ProcessRunState> state, SongResourceProcessListener *listener)
{
    const std::array<std::pair<std::string, std::string>, 3> related {{
        {"lyric", fetch_result.lyric_url},
        {"mrc", fetch_result.mrc_url},
        {"trc", fetch_result.trc_url},
    }};

    for (const auto &[kind, url] : related) {
        if (url.empty()) {
            continue;
        }
        if (state) {
            const std::string key = relatedResourceKey(audio_target, kind);
            {
                std::lock_guard<std::mutex> lock(state->related_mutex);
                if (!state->processed_related_keys.insert(key).second) {
                    continue;
                }
            }
        }

        application::model::SongResourceRelatedClaimRequest request;
        request.song_source_ref_id = audio_target.song_source_ref_id;
        request.resource_kind = kind;
        request.remote_url = url;
        request.batch_id = batch_id;

        auto related_target = co_await resource_queue_->claimRelated(std::move(request));
        if (!related_target) {
            auto item = makeRelatedSkippedItem(audio_target, kind);
            co_await notifyItemStarted(listener, item);
            {
                std::lock_guard<std::mutex> lock(state->result_mutex);
                ++state->result.related_skipped_count;
                ++state->result.synthetic_related_skipped_count;
                state->result.items.push_back(item);
            }
            co_await notifyItemFinished(listener, item);
            continue;
        }

        co_await notifyItemStarted(listener, makeStartedItem(*related_target, true));
        auto item = co_await processDirectResource(std::move(*related_target), batch_id, state ? state->options : application::model::SongResourceProcessOptions {}, listener);
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

application::async::Task<application::model::SongResourceProcessItem> AsyncSongResourceDownloadWorkflow::fail(application::model::SongResourceQueueTarget target, std::string category, std::string code, std::string message, const std::string &batch_id)
{
    application::model::SongResourceFailureUpdate update;
    update.resource_id = target.resource_id;
    update.error_category = std::move(category);
    update.result_code = std::move(code);
    update.message = trimMessage(std::move(message));
    update.batch_id = batch_id;
    if (resource_queue_) {
        (void)co_await resource_queue_->markFailed(update);
    }
    co_return makeItem(target, false, false, false, false, update.error_category, update.result_code, update.message);
}

application::async::Task<application::model::SongResourceProcessItem> AsyncSongResourceDownloadWorkflow::skip(application::model::SongResourceQueueTarget target, std::string category, std::string code, std::string message, const std::string &batch_id)
{
    application::model::SongResourceSkipUpdate update;
    update.resource_id = target.resource_id;
    update.song_source_ref_id = target.song_source_ref_id;
    update.resource_kind.clear();
    update.format_type.clear();
    update.error_category = std::move(category);
    update.result_code = std::move(code);
    update.message = trimMessage(std::move(message));
    update.batch_id = batch_id;
    if (resource_queue_) {
        (void)co_await resource_queue_->markSkipped(update);
    }
    co_return makeItem(target, false, true, false, false, update.error_category, update.result_code, update.message);
}

application::async::Task<void> AsyncSongResourceDownloadWorkflow::notifyItemStarted(SongResourceProcessListener *listener, application::model::SongResourceProcessItem item)
{
    if (!listener) {
        co_return;
    }
    try {
        listener->onItemStarted(item);
    } catch (...) {
    }
}

application::async::Task<void> AsyncSongResourceDownloadWorkflow::notifyItemFinished(SongResourceProcessListener *listener, application::model::SongResourceProcessItem item)
{
    if (!listener) {
        co_return;
    }
    try {
        listener->onItemFinished(item);
    } catch (...) {
    }
}

application::async::Task<void> AsyncSongResourceDownloadWorkflow::notifyItemEvent(SongResourceProcessListener *listener, application::model::SongResourceProcessItem item, std::string event_type, std::string payload)
{
    if (!listener) {
        co_return;
    }
    try {
        listener->onItemEvent(item, std::move(event_type), std::move(payload));
    } catch (...) {
    }
}

application::async::Task<void> AsyncSongResourceDownloadWorkflow::notifyPacmtokenRefreshed(SongResourceProcessListener *listener, bool refreshed, std::size_t pacmtoken_length, std::uint64_t updated_at)
{
    if (!listener) {
        co_return;
    }
    try {
        listener->onPacmtokenRefreshed(refreshed, pacmtoken_length, updated_at);
    } catch (...) {
    }
}

} // namespace orchestrator::modules::crawler::application::use_cases
