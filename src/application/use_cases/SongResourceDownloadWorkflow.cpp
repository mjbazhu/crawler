#include "crawler/application/use_cases/SongResourceDownloadWorkflow.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <ctime>
#include <exception>
#include <future>
#include <memory>
#include <mutex>
#include <sstream>
#include <string_view>
#include <unordered_set>
#include <utility>
#include <vector>

namespace orchestrator::modules::crawler::application::use_cases {

namespace {

// 错误文本限制长度，避免把过大的外部响应写入数据库。
std::string trimMessage(std::string message)
{
    constexpr std::size_t kMaxMessageLength = 512;
    if (message.size() > kMaxMessageLength) {
        message.resize(kMaxMessageLength);
    }
    return message;
}

// 转义短 JSON 字符串，用于批次事件 payload，避免引入 JSON 依赖到 application workflow。
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

// 构造不会泄露临时 URL 的阶段事件 payload。
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

// 判断资源类型是否为音频。
bool isAudioResource(const std::string &resource_kind)
{
    return resource_kind == "audio";
}

// 判断资源类型是否为 listen 响应带出的关联文本资源。
bool isRelatedTextResource(const std::string &resource_kind)
{
    return resource_kind == "lyric" || resource_kind == "mrc" || resource_kind == "trc";
}

// 判断来源专辑是否已知为数字专辑。
bool isKnownDigitalAlbum(const application::model::SongResourceQueueTarget &target)
{
    return target.source_album_kind == 2;
}

// 判断是否为无需继续重试的平台永久限制。
bool isPermanentResourceSkip(const std::string &code)
{
    return code == "440000" || code == "440015" || code == "440016";
}

// 归类无需继续重试的平台永久限制，便于前端按业务原因展示。
std::string permanentResourceSkipCategory(const std::string &code)
{
    if (code == "440000") {
        return "copyright";
    }
    return "resource";
}

// 判断是否为可通过登录态或账号状态变化重试的平台资源限制。
bool isResourceRestrictionFailure(const std::string &code)
{
    return code == "440013";
}

// 构造可回写的处理明细。
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

// 构造开始处理事件所需的最小资源明细。
application::model::SongResourceProcessItem makeStartedItem(const application::model::SongResourceQueueTarget &target, bool related)
{
    return makeItem(target, false, false, related, false, {}, {}, {});
}

// 记录单条资源处理阶段事件。
void emitItemEvent(SongResourceProcessListener *listener, const application::model::SongResourceQueueTarget &target, const std::string &event_type, const std::string &payload)
{
    if (!listener) {
        return;
    }
    listener->onItemEvent(makeStartedItem(target, isRelatedTextResource(target.resource_kind)), event_type, payload);
}

// 记录批次阶段事件。
void emitBatchEvent(SongResourceProcessListener *listener, const std::string &event_type, const std::string &payload)
{
    if (!listener) {
        return;
    }
    listener->onBatchEvent(event_type, payload);
}

// 把状态过滤转换为短 JSON 数组。
std::string statusesJson(const std::vector<int> &statuses)
{
    std::ostringstream out;
    out << '[';
    for (std::size_t index = 0; index < statuses.size(); ++index) {
        if (index > 0) {
            out << ',';
        }
        out << statuses[index];
    }
    out << ']';
    return out.str();
}

// 构造资源认领事件 payload。
std::string claimPayload(const application::model::SongResourceClaimQuery &query, std::string_view stage, std::string_view extra = {})
{
    std::string payload = "{\"stage\":\"" + escapeJson(stage) + "\""
                          + ",\"source\":\"" + escapeJson(query.source) + "\""
                          + ",\"resource_kind\":\"" + escapeJson(query.resource_kind) + "\""
                          + ",\"format_type\":\"" + escapeJson(query.format_type) + "\""
                          + ",\"content_id\":\"" + escapeJson(query.source_content_id) + "\""
                          + ",\"statuses\":" + statusesJson(query.statuses)
                          + ",\"limit\":" + std::to_string(query.limit)
                          + ",\"offset\":" + std::to_string(query.offset);
    if (!extra.empty()) {
        payload += ",";
        payload.append(extra);
    }
    payload += "}";
    return payload;
}

// 构造关联资源无法认领时的跳过明细。
application::model::SongResourceProcessItem makeRelatedSkippedItem(const application::model::SongResourceQueueTarget &audio_target, const std::string &kind)
{
    application::model::SongResourceQueueTarget target = audio_target;
    target.resource_id = 0;
    target.resource_kind = kind;
    target.format_type.clear();
    return makeItem(target, false, true, true, true, "resource", "related_not_claimed", "related resource task not found or already processed");
}

// 构造同一批次内关联文本资源的去重键。
std::string relatedResourceKey(const application::model::SongResourceQueueTarget &audio_target, const std::string &kind)
{
    return std::to_string(audio_target.song_source_ref_id) + ":" + audio_target.source_song_id + ":" + kind;
}

// 为 Migu listen 接口构造稳定请求参数。
application::model::SongResourceFetchRequest toFetchRequest(const application::model::SongResourceQueueTarget &target)
{
    application::model::SongResourceFetchRequest request;
    request.content_id = target.source_content_id;
    request.resource_type = target.audio_resource_type.empty() ? target.source_resource_type : target.audio_resource_type;
    request.copyright_id = target.copyright_id;
    request.tone_flag = target.format_type;
    return request;
}

// 判断音频资源 URL 获取参数是否完整。
bool validAudioTarget(const application::model::SongResourceQueueTarget &target)
{
    const auto request = toFetchRequest(target);
    return !request.content_id.empty() && !request.resource_type.empty() && !request.copyright_id.empty() && !request.tone_flag.empty();
}

// 把队列目标转换为路径规划请求。
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

// 当前 Unix 秒时间戳。
std::uint64_t nowSeconds()
{
    return static_cast<std::uint64_t>(std::time(nullptr));
}

// 判断失败是否适合立即重试。
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

// 将前端传入的并发约束到合理范围。
std::size_t normalizedResourceConcurrency(const application::model::SongResourceProcessOptions &options)
{
    return (std::min<std::size_t>)((std::max<std::size_t>)(1, options.resource_download_concurrency), 32);
}

} // namespace

struct SongResourceDownloadWorkflow::ProcessRunState {
    application::model::SongResourceProcessOptions options; // 本次批处理有效选项
    std::mutex result_mutex;                                // 保护 result.items 和计数
    std::mutex related_mutex;                               // 保护关联资源去重集合
    std::mutex pacmtoken_mutex;                             // 保护 pacmtoken 刷新节流
    std::unordered_set<std::string> processed_related_keys; // 同一批次内关联资源去重键
    std::uint64_t last_pacmtoken_refresh_at = 0;            // 最近一次自动刷新时间
};

SongResourceDownloadWorkflow::SongResourceDownloadWorkflow(
    std::shared_ptr<application::ports::SongResourceQueue> resource_queue,
    std::shared_ptr<application::ports::SongResourceProvider> resource_provider,
    std::shared_ptr<application::ports::SongResourceParser> resource_parser,
    std::shared_ptr<application::ports::SongResourceContentFetcher> content_fetcher,
    std::shared_ptr<application::ports::SongResourceBlobStore> blob_store,
    SongResourcePathPlanner path_planner,
    std::shared_ptr<application::ports::TaskExecutor> resource_executor,
    std::shared_ptr<application::ports::MiguPacmtokenRefresher> pacmtoken_refresher,
    std::shared_ptr<application::ports::MiguPacmtokenStore> pacmtoken_store
) : resource_queue_(std::move(resource_queue)),
    resource_provider_(std::move(resource_provider)),
    resource_parser_(std::move(resource_parser)),
    content_fetcher_(std::move(content_fetcher)),
    blob_store_(std::move(blob_store)),
    path_planner_(std::move(path_planner)),
    resource_executor_(std::move(resource_executor)),
    pacmtoken_refresher_(std::move(pacmtoken_refresher)),
    pacmtoken_store_(std::move(pacmtoken_store))
{}

application::model::SongResourceProcessResult SongResourceDownloadWorkflow::processPending(application::model::SongResourceClaimQuery query, SongResourceProcessListener *listener)
{
    application::model::SongResourceProcessResult result;
    result.batch_id = query.batch_id;
    if (!resource_queue_ || !content_fetcher_ || !blob_store_) {
        return result;
    }
    if (query.source.empty()) {
        query.source = "migu";
    }
    if (query.limit == 0) {
        query.limit = 20;
    }

    ProcessRunState state;
    state.options = query.options;
    if (state.options.auto_refresh_pacmtoken) {
        (void)refreshPacmtoken(state, listener, true);
    }

    emitBatchEvent(listener, "resourceClaimStarted", claimPayload(query, "resource_claim_started"));
    std::vector<application::model::SongResourceQueueTarget> targets;
    try {
        targets = resource_queue_->claimPending(query);
    } catch (const std::exception &e) {
        emitBatchEvent(listener, "resourceClaimFailed", claimPayload(query, "resource_claim_failed", "\"error\":\"" + escapeJson(trimMessage(e.what())) + "\""));
        throw;
    } catch (...) {
        emitBatchEvent(listener, "resourceClaimFailed", claimPayload(query, "resource_claim_failed", "\"error\":\"resource claim failed with unknown error\""));
        throw;
    }
    result.claimed_count = static_cast<int>(targets.size());
    emitBatchEvent(listener, "resourceClaimCompleted", claimPayload(query, "resource_claim_completed", "\"claimed_count\":" + std::to_string(result.claimed_count) + ",\"concurrency\":" + std::to_string(normalizedResourceConcurrency(state.options)) + ",\"retries\":" + std::to_string(state.options.resource_download_retries) + ",\"timeout_ms\":" + std::to_string(state.options.resource_download_timeout_ms)));
    if (targets.empty()) {
        emitBatchEvent(listener, "resourceClaimEmpty", claimPayload(query, "resource_claim_empty"));
    }
    const auto record_main_item = [&result, listener](application::model::SongResourceProcessItem item) {
        if (item.ok) {
            ++result.succeeded_count;
        } else if (item.skipped) {
            ++result.skipped_count;
        } else {
            ++result.failed_count;
        }
        if (listener) {
            listener->onItemFinished(item);
        }
        result.items.push_back(std::move(item));
    };

    const auto run_target = [this, &result, &state, listener, batch_id = query.batch_id](application::model::SongResourceQueueTarget target) {
        if (listener) {
            listener->onItemStarted(makeStartedItem(target, false));
        }
        return isAudioResource(target.resource_kind) ? processAudio(std::move(target), batch_id, &result, listener, &state) : processDirectResource(std::move(target), batch_id, state.options, listener);
    };

    const std::size_t concurrency = normalizedResourceConcurrency(state.options);
    if (!resource_executor_ || concurrency <= 1 || targets.size() <= 1) {
        for (auto &target : targets) {
            auto item = run_target(std::move(target));
            record_main_item(std::move(item));
        }
        return result;
    }

    for (std::size_t first = 0; first < targets.size(); first += concurrency) {
        const std::size_t last = (std::min)(targets.size(), first + concurrency);
        std::vector<std::future<application::model::SongResourceProcessItem>> futures;
        futures.reserve(last - first);
        for (std::size_t index = first; index < last; ++index) {
            auto promise = std::make_shared<std::promise<application::model::SongResourceProcessItem>>();
            futures.push_back(promise->get_future());
            auto target = std::move(targets[index]);
            try {
                resource_executor_->submit([promise, target = std::move(target), &run_target]() mutable {
                    try {
                        promise->set_value(run_target(std::move(target)));
                    } catch (...) {
                        promise->set_exception(std::current_exception());
                    }
                });
            } catch (...) {
                promise->set_value(fail(std::move(target), "unknown", "submit_failed", "resource worker submit failed", query.batch_id, listener));
            }
        }
        for (auto &future : futures) {
            try {
                auto item = future.get();
                std::lock_guard<std::mutex> lock(state.result_mutex);
                record_main_item(std::move(item));
            } catch (const std::exception &e) {
                application::model::SongResourceProcessItem item;
                item.source = query.source;
                item.result_code = "worker_failed";
                item.error_category = "unknown";
                item.message = e.what();
                std::lock_guard<std::mutex> lock(state.result_mutex);
                record_main_item(std::move(item));
            }
        }
    }
    return result;
}

application::model::SongResourceProcessItem SongResourceDownloadWorkflow::processClaimed(application::model::SongResourceQueueTarget target, std::string batch_id)
{
    if (isAudioResource(target.resource_kind)) {
        ProcessRunState state;
        return processAudio(std::move(target), batch_id, nullptr, nullptr, &state);
    }
    return processDirectResource(std::move(target), batch_id, {}, nullptr);
}

application::model::SongResourceProcessItem SongResourceDownloadWorkflow::processAudio(application::model::SongResourceQueueTarget target, const std::string &batch_id, application::model::SongResourceProcessResult *batch_result, SongResourceProcessListener *listener, ProcessRunState *state)
{
    if (isKnownDigitalAlbum(target)) {
        return skip(std::move(target), "resource", "digital_album", "digital album requires purchase before download", batch_id, listener);
    }
    if (!resource_provider_ || !resource_parser_) {
        return fail(std::move(target), "invalid_target", "missing_dependency", "song resource provider or parser is not configured", batch_id, listener);
    }
    if (!validAudioTarget(target)) {
        return fail(std::move(target), "invalid_target", "invalid_target", "missing contentId, resourceType, copyrightId or toneFlag", batch_id, listener);
    }

    const auto options = state ? state->options : application::model::SongResourceProcessOptions {};
    int max_attempts = (std::max)(1, options.resource_download_retries + 1);
    bool refreshed_after_440013 = false;
    application::model::SongResourceProcessItem last_failure;
    for (int attempt = 1; attempt <= max_attempts; ++attempt) {
        emitItemEvent(listener, target, "resourceAttemptStarted", stagePayload(target, "resource_attempt_started", "\"attempt\":" + std::to_string(attempt) + ",\"max_attempts\":" + std::to_string(max_attempts)));
        if (state) {
            (void)refreshPacmtoken(*state, listener, false);
        }

        emitItemEvent(listener, target, "resourceUrlRequestStarted", stagePayload(target, "resource_url_request_started", "\"attempt\":" + std::to_string(attempt)));
        auto response = resource_provider_->fetchSongResource(toFetchRequest(target));
        if (!response) {
            emitItemEvent(listener, target, "resourceUrlRequestFailed", stagePayload(target, "resource_url_request_failed", "\"attempt\":" + std::to_string(attempt) + ",\"result_code\":\"resource_url_fetch_failed\""));
            last_failure = makeItem(target, false, false, false, false, "network", "resource_url_fetch_failed", "song resource url request failed");
            if (attempt < max_attempts) {
                emitItemEvent(listener, target, "resourceRetryScheduled", stagePayload(target, "resource_retry_scheduled", "\"attempt\":" + std::to_string(attempt) + ",\"next_attempt\":" + std::to_string(attempt + 1) + ",\"max_attempts\":" + std::to_string(max_attempts) + ",\"failed_stage\":\"resource_url_request\",\"result_code\":\"resource_url_fetch_failed\",\"error_category\":\"network\""));
                continue;
            }
            emitItemEvent(listener, target, "resourceAttemptFinished", stagePayload(target, "resource_attempt_finished", "\"attempt\":" + std::to_string(attempt) + ",\"result_code\":\"" + escapeJson(last_failure.result_code) + "\",\"error_category\":\"" + escapeJson(last_failure.error_category) + "\""));
            return fail(std::move(target), last_failure.error_category, last_failure.result_code, last_failure.message, batch_id, listener);
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
                emitItemEvent(listener, target, "resourceUrlRejected", stagePayload(target, "resource_url_rejected", "\"attempt\":" + std::to_string(attempt) + ",\"result_code\":\"" + escapeJson(code) + "\""));
                const std::string category = permanentResourceSkipCategory(code);
                if (batch_result && resource_queue_) {
                    processRelatedTextResources(target, fetch_result, batch_id, *batch_result, listener, state);
                }
                emitItemEvent(listener, target, "resourceAttemptFinished", stagePayload(target, "resource_attempt_finished", "\"attempt\":" + std::to_string(attempt) + ",\"result_code\":\"" + escapeJson(code) + "\",\"error_category\":\"" + escapeJson(category) + "\""));
                return skip(std::move(target), category, std::move(code), std::move(message), batch_id, listener);
            }
            if (isResourceRestrictionFailure(code) && options.refresh_pacmtoken_on_440013 && state && !refreshed_after_440013) {
                emitItemEvent(listener, target, "resourceUrlRestricted", stagePayload(target, "resource_url_restricted", "\"attempt\":" + std::to_string(attempt) + ",\"result_code\":\"" + escapeJson(code) + "\""));
                refreshed_after_440013 = true;
                if (refreshPacmtoken(*state, listener, true)) {
                    if (attempt == max_attempts) {
                        ++max_attempts;
                    }
                    continue;
                }
            }
            if (isResourceRestrictionFailure(code)) {
                emitItemEvent(listener, target, "resourceUrlRejected", stagePayload(target, "resource_url_rejected", "\"attempt\":" + std::to_string(attempt) + ",\"result_code\":\"" + escapeJson(code) + "\""));
                emitItemEvent(listener, target, "resourceAttemptFinished", stagePayload(target, "resource_attempt_finished", "\"attempt\":" + std::to_string(attempt) + ",\"result_code\":\"" + escapeJson(code) + "\",\"error_category\":\"resource\""));
                return fail(std::move(target), "resource", std::move(code), std::move(message), batch_id, listener);
            }
            emitItemEvent(listener, target, "resourceUrlParseFailed", stagePayload(target, "resource_url_parse_failed", "\"attempt\":" + std::to_string(attempt) + ",\"result_code\":\"" + escapeJson(code) + "\""));
            emitItemEvent(listener, target, "resourceAttemptFinished", stagePayload(target, "resource_attempt_finished", "\"attempt\":" + std::to_string(attempt) + ",\"result_code\":\"" + escapeJson(code) + "\",\"error_category\":\"parse\""));
            return fail(std::move(target), "parse", std::move(code), std::move(message), batch_id, listener);
        }

        emitItemEvent(listener, target, "resourceUrlFetched", stagePayload(target, "resource_url_fetched", "\"attempt\":" + std::to_string(attempt) + ",\"audio_format_type\":\"" + escapeJson(fetch_result.audio_format_type) + "\"" + ",\"version\":\"" + escapeJson(fetch_result.version) + "\"" + ",\"has_lyric\":" + (fetch_result.lyric_url.empty() ? "false" : "true") + ",\"has_mrc\":" + (fetch_result.mrc_url.empty() ? "false" : "true") + ",\"has_trc\":" + (fetch_result.trc_url.empty() ? "false" : "true")));

        auto item = downloadAndStoreAttempt(target, fetch_result.url, batch_id, options, attempt == max_attempts, listener);
        if (item.ok && resource_queue_) {
            application::model::SongResourceProcessResult fallback_result;
            processRelatedTextResources(target, fetch_result, batch_id, batch_result ? *batch_result : fallback_result, listener, state);
            emitItemEvent(listener, target, "resourceAttemptFinished", stagePayload(target, "resource_attempt_finished", "\"attempt\":" + std::to_string(attempt) + ",\"result_code\":\"ok\""));
            return item;
        }
        last_failure = item;
        if (attempt < max_attempts && retryableResourceFailure(item)) {
            emitItemEvent(listener, target, "resourceRetryScheduled", stagePayload(target, "resource_retry_scheduled", "\"attempt\":" + std::to_string(attempt) + ",\"next_attempt\":" + std::to_string(attempt + 1) + ",\"max_attempts\":" + std::to_string(max_attempts) + ",\"failed_stage\":\"resource_content_download\",\"result_code\":\"" + escapeJson(item.result_code) + "\",\"error_category\":\"" + escapeJson(item.error_category) + "\""));
            continue;
        }
        if (!item.ok && !item.skipped && attempt < max_attempts) {
            emitItemEvent(listener, target, "resourceAttemptFinished", stagePayload(target, "resource_attempt_finished", "\"attempt\":" + std::to_string(attempt) + ",\"result_code\":\"" + escapeJson(item.result_code) + "\",\"error_category\":\"" + escapeJson(item.error_category) + "\""));
            return fail(std::move(target), item.error_category, item.result_code, item.message, batch_id, listener);
        }
        emitItemEvent(listener, target, "resourceAttemptFinished", stagePayload(target, "resource_attempt_finished", "\"attempt\":" + std::to_string(attempt) + ",\"result_code\":\"" + escapeJson(item.result_code) + "\",\"error_category\":\"" + escapeJson(item.error_category) + "\""));
        return item;
    }

    return fail(std::move(target), last_failure.error_category.empty() ? "unknown" : last_failure.error_category, last_failure.result_code.empty() ? "failed" : last_failure.result_code, last_failure.message.empty() ? "resource processing failed" : last_failure.message, batch_id, listener);
}

application::model::SongResourceProcessItem SongResourceDownloadWorkflow::processDirectResource(application::model::SongResourceQueueTarget target, const std::string &batch_id, const application::model::SongResourceProcessOptions &options, SongResourceProcessListener *listener)
{
    if (target.remote_url.empty()) {
        return fail(std::move(target), "invalid_target", "missing_remote_url", "resource remote_url is empty", batch_id, listener);
    }
    std::string remote_url = target.remote_url;
    const int max_attempts = (std::max)(1, options.resource_download_retries + 1);
    application::model::SongResourceProcessItem last_failure;
    for (int attempt = 1; attempt <= max_attempts; ++attempt) {
        emitItemEvent(listener, target, "resourceAttemptStarted", stagePayload(target, "resource_attempt_started", "\"attempt\":" + std::to_string(attempt) + ",\"max_attempts\":" + std::to_string(max_attempts)));
        auto item = downloadAndStoreAttempt(target, remote_url, batch_id, options, attempt == max_attempts, listener);
        if (item.ok || item.skipped) {
            emitItemEvent(listener, target, "resourceAttemptFinished", stagePayload(target, "resource_attempt_finished", "\"attempt\":" + std::to_string(attempt) + ",\"result_code\":\"" + escapeJson(item.result_code) + "\",\"error_category\":\"" + escapeJson(item.error_category) + "\""));
            return item;
        }
        last_failure = item;
        if (attempt < max_attempts && retryableResourceFailure(item)) {
            emitItemEvent(listener, target, "resourceRetryScheduled", stagePayload(target, "resource_retry_scheduled", "\"attempt\":" + std::to_string(attempt) + ",\"next_attempt\":" + std::to_string(attempt + 1) + ",\"max_attempts\":" + std::to_string(max_attempts) + ",\"failed_stage\":\"resource_content_download\",\"result_code\":\"" + escapeJson(item.result_code) + "\",\"error_category\":\"" + escapeJson(item.error_category) + "\""));
            continue;
        }
        if (attempt < max_attempts) {
            emitItemEvent(listener, target, "resourceAttemptFinished", stagePayload(target, "resource_attempt_finished", "\"attempt\":" + std::to_string(attempt) + ",\"result_code\":\"" + escapeJson(item.result_code) + "\",\"error_category\":\"" + escapeJson(item.error_category) + "\""));
            return fail(std::move(target), item.error_category, item.result_code, item.message, batch_id, listener);
        }
        emitItemEvent(listener, target, "resourceAttemptFinished", stagePayload(target, "resource_attempt_finished", "\"attempt\":" + std::to_string(attempt) + ",\"result_code\":\"" + escapeJson(item.result_code) + "\",\"error_category\":\"" + escapeJson(item.error_category) + "\""));
        return item;
    }
    return fail(std::move(target), last_failure.error_category.empty() ? "unknown" : last_failure.error_category, last_failure.result_code.empty() ? "failed" : last_failure.result_code, last_failure.message.empty() ? "resource processing failed" : last_failure.message, batch_id, listener);
}

application::model::SongResourceProcessItem SongResourceDownloadWorkflow::downloadAndStore(application::model::SongResourceQueueTarget target, std::string remote_url, const std::string &batch_id, const application::model::SongResourceProcessOptions &options)
{
    return downloadAndStoreAttempt(std::move(target), std::move(remote_url), batch_id, options, true, nullptr);
}

application::model::SongResourceProcessItem SongResourceDownloadWorkflow::downloadAndStoreAttempt(application::model::SongResourceQueueTarget target, std::string remote_url, const std::string &batch_id, const application::model::SongResourceProcessOptions &options, bool write_failure, SongResourceProcessListener *listener)
{
    if (!content_fetcher_ || !blob_store_) {
        return write_failure ? fail(std::move(target), "invalid_target", "missing_dependency", "content fetcher or blob store is not configured", batch_id, listener) : makeItem(target, false, false, false, false, "invalid_target", "missing_dependency", "content fetcher or blob store is not configured");
    }

    application::model::SongResourceContentFetchRequest fetch_request;
    fetch_request.url = remote_url;
    fetch_request.timeout_ms = options.resource_download_timeout_ms;
    emitItemEvent(listener, target, "resourceContentDownloadStarted", stagePayload(target, "resource_content_download_started"));
    std::unique_ptr<application::ports::SongResourceBlobWriteSession> writer;
    application::model::SongResourcePathResult path;
    std::string content_type;
    std::string callback_failure_category;
    std::string callback_failure_code;
    std::string callback_failure_message;
    std::string write_error;

    application::model::SongResourceContentStreamCallbacks callbacks;
    callbacks.on_response = [&](const application::model::SongResourceContentStreamInfo &info) {
        content_type = info.content_type;
        path = path_planner_.plan(toPathRequest(target, remote_url, content_type));
        if (!path.ok) {
            callback_failure_category = "persistence";
            callback_failure_code = "path_plan_failed";
            callback_failure_message = path.error.empty() ? "resource local path planning failed" : path.error;
            emitItemEvent(listener, target, "resourcePathPlanFailed", stagePayload(target, "resource_path_plan_failed", "\"result_code\":\"path_plan_failed\",\"message\":\"" + escapeJson(callback_failure_message) + "\""));
            return false;
        }
        emitItemEvent(listener, target, "resourcePathPlanned", stagePayload(target, "resource_path_planned", "\"local_path\":\"" + escapeJson(path.relative_path) + "\",\"extension\":\"" + escapeJson(path.extension) + "\""));

        application::model::SongResourceBlobStreamSaveRequest save_request;
        save_request.relative_path_hint = path.relative_path;
        save_request.content_type = content_type;
        writer = blob_store_->beginSongResourceBlobWrite(std::move(save_request));
        if (!writer) {
            callback_failure_category = "persistence";
            callback_failure_code = "persist_failed";
            callback_failure_message = "resource blob stream open failed";
            emitItemEvent(listener, target, "resourceBlobSaveFailed", stagePayload(target, "resource_blob_save_failed", "\"result_code\":\"persist_failed\",\"message\":\"" + escapeJson(callback_failure_message) + "\""));
            return false;
        }
        emitItemEvent(listener, target, "resourceBlobSaveStarted", stagePayload(target, "resource_blob_save_started", "\"local_path\":\"" + escapeJson(path.relative_path) + "\""));
        return true;
    };
    callbacks.on_chunk = [&](std::string_view chunk) {
        if (!writer) {
            callback_failure_category = "persistence";
            callback_failure_code = "persist_failed";
            callback_failure_message = "resource blob stream was not opened";
            return false;
        }
        if (!writer->write(chunk, write_error)) {
            callback_failure_category = "persistence";
            callback_failure_code = "persist_failed";
            callback_failure_message = write_error.empty() ? "resource blob stream write failed" : write_error;
            return false;
        }
        return true;
    };

    auto content = content_fetcher_->streamSongResourceContent(std::move(fetch_request), std::move(callbacks));
    if (!content.ok) {
        if (writer) {
            writer->abort();
            emitItemEvent(listener, target, "resourceBlobAbortCompleted", stagePayload(target, "resource_blob_abort_completed"));
        }
        const std::string category = !callback_failure_category.empty() ? callback_failure_category : (content.error_category.empty() ? "network" : content.error_category);
        const std::string code = !callback_failure_code.empty() ? callback_failure_code : (content.result_code.empty() ? "download_failed" : content.result_code);
        const std::string message = !callback_failure_message.empty() ? callback_failure_message : (content.message.empty() ? "resource content download failed" : content.message);
        emitItemEvent(listener, target, "resourceContentDownloadFailed", stagePayload(target, "resource_content_download_failed", "\"result_code\":\"" + escapeJson(code) + "\",\"error_category\":\"" + escapeJson(category) + "\""));
        return write_failure ? fail(std::move(target), category, code, message, batch_id, listener) : makeItem(target, false, false, false, false, category, code, message);
    }
    emitItemEvent(listener, target, "resourceContentDownloaded", stagePayload(target, "resource_content_downloaded", "\"status_code\":" + std::to_string(content.status_code) + ",\"content_type\":\"" + escapeJson(content.content_type) + "\"" + ",\"body_size\":" + std::to_string(content.bytes_received)));
    const auto downloaded_size = content.bytes_received;

    if (!writer) {
        const std::string message = "resource blob stream was not opened";
        emitItemEvent(listener, target, "resourceBlobSaveFailed", stagePayload(target, "resource_blob_save_failed", "\"result_code\":\"persist_failed\",\"message\":\"" + escapeJson(message) + "\""));
        return write_failure ? fail(std::move(target), "persistence", "persist_failed", message, batch_id, listener) : makeItem(target, false, false, false, false, "persistence", "persist_failed", message);
    }

    emitItemEvent(listener, target, "resourceBlobCommitStarted", stagePayload(target, "resource_blob_commit_started", "\"local_path\":\"" + escapeJson(path.relative_path) + "\""));
    auto saved = writer->commit();
    if (!saved.ok) {
        const std::string message = saved.error.empty() ? "resource blob save failed" : saved.error;
        emitItemEvent(listener, target, "resourceBlobSaveFailed", stagePayload(target, "resource_blob_save_failed", "\"result_code\":\"persist_failed\",\"message\":\"" + escapeJson(message) + "\""));
        return write_failure ? fail(std::move(target), "persistence", "persist_failed", message, batch_id, listener) : makeItem(target, false, false, false, false, "persistence", "persist_failed", message);
    }
    if (saved.local_path.empty() || saved.size_bytes == 0 || saved.size_bytes != downloaded_size || saved.checksum.empty()) {
        emitItemEvent(listener, target, "resourceBlobValidationFailed", stagePayload(target, "resource_blob_validation_failed", "\"result_code\":\"persist_validation_failed\"" + std::string(",\"local_path\":\"") + escapeJson(saved.local_path) + "\"" + ",\"downloaded_size\":" + std::to_string(downloaded_size) + ",\"saved_size\":" + std::to_string(saved.size_bytes)));
        return write_failure ? fail(std::move(target), "persistence", "persist_validation_failed", "resource blob save result did not match downloaded content", batch_id, listener) : makeItem(target, false, false, false, false, "persistence", "persist_validation_failed", "resource blob save result did not match downloaded content");
    }
    emitItemEvent(listener, target, "resourceBlobSaved", stagePayload(target, "resource_blob_saved", "\"local_path\":\"" + escapeJson(saved.local_path) + "\"" + ",\"size_bytes\":" + std::to_string(saved.size_bytes) + ",\"checksum\":\"" + escapeJson(saved.checksum) + "\""));

    application::model::SongResourceSuccessUpdate update;
    update.resource_id = target.resource_id;
    update.remote_url = std::move(remote_url);
    update.local_path = saved.local_path;
    update.size_bytes = saved.size_bytes;
    update.checksum = saved.checksum;
    update.result_code = "ok";
    update.batch_id = batch_id;
    if (!resource_queue_ || !resource_queue_->markSucceeded(std::move(update))) {
        emitItemEvent(listener, target, "resourceDbWritebackFailed", stagePayload(target, "resource_db_writeback_failed", "\"result_code\":\"writeback_failed\",\"local_path\":\"" + escapeJson(saved.local_path) + "\""));
        return write_failure ? fail(std::move(target), "persistence", "writeback_failed", "resource success writeback failed", batch_id, listener) : makeItem(target, false, false, false, false, "persistence", "writeback_failed", "resource success writeback failed");
    }
    emitItemEvent(listener, target, "resourceDbWritebackCompleted", stagePayload(target, "resource_db_writeback_completed", "\"local_path\":\"" + escapeJson(saved.local_path) + "\",\"size_bytes\":" + std::to_string(saved.size_bytes)));

    auto item = makeItem(target, true, false, false, false, {}, "ok", {});
    item.local_path = std::move(saved.local_path);
    item.size_bytes = saved.size_bytes;
    item.checksum = std::move(saved.checksum);
    return item;
}

void SongResourceDownloadWorkflow::processRelatedTextResources(const application::model::SongResourceQueueTarget &audio_target, const application::model::SongResourceFetchResult &fetch_result, const std::string &batch_id, application::model::SongResourceProcessResult &batch_result, SongResourceProcessListener *listener, ProcessRunState *state)
{
    const std::array<std::pair<std::string, std::string>, 3> related {{
        {"lyric", fetch_result.lyric_url},
        {"mrc", fetch_result.mrc_url},
        {"trc", fetch_result.trc_url},
    }};

    for (const auto &[kind, url] : related) {
        if (!url.empty()) {
            if (state) {
                const std::string key = relatedResourceKey(audio_target, kind);
                {
                    std::lock_guard<std::mutex> lock(state->related_mutex);
                    if (!state->processed_related_keys.insert(key).second) {
                        emitBatchEvent(listener, "relatedResourceDeduplicated", stagePayload(audio_target, "related_resource_deduplicated", "\"related_kind\":\"" + escapeJson(kind) + "\""));
                        continue;
                    }
                }
            }
            application::model::SongResourceRelatedClaimRequest request;
            request.song_source_ref_id = audio_target.song_source_ref_id;
            request.resource_kind = kind;
            request.remote_url = url;
            request.batch_id = batch_id;

            emitBatchEvent(listener, "relatedResourceClaimStarted", stagePayload(audio_target, "related_resource_claim_started", "\"related_kind\":\"" + escapeJson(kind) + "\""));
            auto related_target = resource_queue_->claimRelated(std::move(request));
            if (!related_target) {
                auto item = makeRelatedSkippedItem(audio_target, kind);
                if (listener) {
                    listener->onItemStarted(item);
                }
                if (listener) {
                    listener->onItemEvent(item, "relatedResourceNotClaimed", stagePayload(audio_target, "related_resource_not_claimed", "\"related_kind\":\"" + escapeJson(kind) + "\",\"result_code\":\"related_not_claimed\""));
                }
                if (state) {
                    std::lock_guard<std::mutex> lock(state->result_mutex);
                    ++batch_result.related_skipped_count;
                    ++batch_result.synthetic_related_skipped_count;
                } else {
                    ++batch_result.related_skipped_count;
                    ++batch_result.synthetic_related_skipped_count;
                }
                if (listener) {
                    listener->onItemFinished(item);
                }
                if (state) {
                    std::lock_guard<std::mutex> lock(state->result_mutex);
                    batch_result.items.push_back(std::move(item));
                } else {
                    batch_result.items.push_back(std::move(item));
                }
            } else {
                if (listener) {
                    listener->onItemStarted(makeStartedItem(*related_target, true));
                    listener->onItemEvent(makeStartedItem(*related_target, true), "relatedResourceClaimed", stagePayload(*related_target, "related_resource_claimed", "\"related_kind\":\"" + escapeJson(kind) + "\""));
                }
                auto item = processDirectResource(std::move(*related_target), batch_id, state ? state->options : application::model::SongResourceProcessOptions {}, listener);
                item.related = true;
                if (state) {
                    std::lock_guard<std::mutex> lock(state->result_mutex);
                    if (item.ok) {
                        ++batch_result.related_succeeded_count;
                    } else if (item.skipped) {
                        ++batch_result.related_skipped_count;
                    } else {
                        ++batch_result.related_failed_count;
                    }
                } else if (item.ok) {
                    ++batch_result.related_succeeded_count;
                } else if (item.skipped) {
                    ++batch_result.related_skipped_count;
                } else {
                    ++batch_result.related_failed_count;
                }
                if (listener) {
                    listener->onItemFinished(item);
                }
                if (state) {
                    std::lock_guard<std::mutex> lock(state->result_mutex);
                    batch_result.items.push_back(std::move(item));
                } else {
                    batch_result.items.push_back(std::move(item));
                }
            }
        }
    }
}

bool SongResourceDownloadWorkflow::refreshPacmtoken(ProcessRunState &state, SongResourceProcessListener *listener, bool force)
{
    if (!state.options.auto_refresh_pacmtoken || !pacmtoken_refresher_ || !pacmtoken_store_) {
        return false;
    }
    if (!force && state.options.pacmtoken_refresh_interval_seconds <= 0) {
        return false;
    }

    const std::uint64_t now = nowSeconds();
    if (!force && state.last_pacmtoken_refresh_at != 0 && now >= state.last_pacmtoken_refresh_at && now - state.last_pacmtoken_refresh_at < static_cast<std::uint64_t>(state.options.pacmtoken_refresh_interval_seconds)) {
        return false;
    }

    std::lock_guard<std::mutex> lock(state.pacmtoken_mutex);
    const std::uint64_t locked_now = nowSeconds();
    if (!force && state.last_pacmtoken_refresh_at != 0 && locked_now >= state.last_pacmtoken_refresh_at && locked_now - state.last_pacmtoken_refresh_at < static_cast<std::uint64_t>(state.options.pacmtoken_refresh_interval_seconds)) {
        return false;
    }

    auto current = pacmtoken_store_->currentPacmtoken();
    if (!current || current->empty()) {
        return false;
    }

    auto refreshed = pacmtoken_refresher_->refreshPacmtoken(*current);
    if (!refreshed.success) {
        return false;
    }
    if (refreshed.pacmtoken.empty()) {
        refreshed.pacmtoken = *current;
    }
    if (refreshed.updated_at == 0) {
        refreshed.updated_at = locked_now;
    }
    refreshed.pacmtoken_length = refreshed.pacmtoken.size();
    pacmtoken_store_->updatePacmtoken(refreshed.pacmtoken, refreshed.updated_at);
    state.last_pacmtoken_refresh_at = refreshed.updated_at;
    if (listener) {
        listener->onPacmtokenRefreshed(refreshed.refreshed, refreshed.pacmtoken_length, refreshed.updated_at);
    }
    return true;
}

application::model::SongResourceProcessItem SongResourceDownloadWorkflow::fail(application::model::SongResourceQueueTarget target, std::string category, std::string code, std::string message, const std::string &batch_id, SongResourceProcessListener *listener)
{
    application::model::SongResourceFailureUpdate update;
    update.resource_id = target.resource_id;
    update.error_category = std::move(category);
    update.result_code = std::move(code);
    update.message = trimMessage(std::move(message));
    update.batch_id = batch_id;
    bool writeback_ok = false;
    if (resource_queue_) {
        writeback_ok = resource_queue_->markFailed(update);
    }
    emitItemEvent(listener, target, writeback_ok ? "resourceFailureWritebackCompleted" : "resourceFailureWritebackFailed", stagePayload(target, writeback_ok ? "resource_failure_writeback_completed" : "resource_failure_writeback_failed", "\"result_code\":\"" + escapeJson(update.result_code) + "\",\"error_category\":\"" + escapeJson(update.error_category) + "\""));
    return makeItem(target, false, false, false, false, update.error_category, update.result_code, update.message);
}

application::model::SongResourceProcessItem SongResourceDownloadWorkflow::skip(application::model::SongResourceQueueTarget target, std::string category, std::string code, std::string message, const std::string &batch_id, SongResourceProcessListener *listener)
{
    application::model::SongResourceSkipUpdate update;
    update.resource_id = target.resource_id;
    update.song_source_ref_id = target.song_source_ref_id;
    update.resource_kind = target.resource_kind;
    update.format_type.clear();
    update.error_category = std::move(category);
    update.result_code = std::move(code);
    update.message = trimMessage(std::move(message));
    update.batch_id = batch_id;
    bool writeback_ok = false;
    if (resource_queue_) {
        writeback_ok = resource_queue_->markSkipped(update);
    }
    emitItemEvent(listener, target, writeback_ok ? "resourceSkipWritebackCompleted" : "resourceSkipWritebackFailed", stagePayload(target, writeback_ok ? "resource_skip_writeback_completed" : "resource_skip_writeback_failed", "\"result_code\":\"" + escapeJson(update.result_code) + "\",\"error_category\":\"" + escapeJson(update.error_category) + "\""));
    return makeItem(target, false, true, false, false, update.error_category, update.result_code, update.message);
}

} // namespace orchestrator::modules::crawler::application::use_cases
