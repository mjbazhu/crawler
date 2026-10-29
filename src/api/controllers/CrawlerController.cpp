#include "crawler/api/controllers/CrawlerController.h"
#include "crawler/api/dto/MetadataCoverDto.h"
#include "crawler/application/model/AlbumCollectionPolicy.h"

#include "crawler/api/controllers/OpenApiDocumentFactory.h"
#include "crawler/api/dto/CrawlDto.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <ctime>
#include <map>
#include <optional>
#include <sstream>
#include <string>
#include <utility>
#include <vector>

namespace orchestrator::modules::crawler::api {
namespace controllers {

using json = nlohmann::json;

namespace {

// 将 HTTP 状态映射为稳定错误码，前端可基于该字段做本地化。
std::string errorCodeForStatus(int status)
{
    switch (status) {
    case 400: return "bad_request";
    case 404: return "not_found";
    case 409: return "conflict";
    case 503: return "service_unavailable";
    default: return status >= 500 ? "internal_error" : "request_error";
    }
}

http::HttpResponse jsonError(int status, std::string message)
{
    json body;
    body["error_code"] = errorCodeForStatus(status);
    body["message"] = message;
    body["error"] = std::move(message);
    body["details"] = nullptr;
    return http::HttpResponse {status, {{"Content-Type", "application/json"}}, body.dump()};
}

// 归一化 API 层资源类型别名，避免前端传 lrc 时查不到 lyric 队列。
std::string normalizeResourceKind(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    if (value == "lrc") {
        return "lyric";
    }
    return value;
}

int pacmtokenErrorStatus(const application::model::MiguPacmtokenRefreshResult &result)
{
    if (result.error_code == "bad_request") {
        return 400;
    }
    if (result.error_code == "not_configured") {
        return 503;
    }
    return 502;
}

http::HttpResponse pacmtokenErrorResponse(const application::model::MiguPacmtokenRefreshResult &result)
{
    const int status = pacmtokenErrorStatus(result);
    json body;
    body["error_code"] = result.error_code.empty() ? errorCodeForStatus(status) : result.error_code;
    body["message"] = result.message.empty() ? "migu pacmtoken refresh failed" : result.message;
    body["error"] = body["message"];
    body["details"] = {
        {"upstream_status", result.upstream_status == 0 ? json(nullptr) : json(result.upstream_status)},
        {"updated_at", result.updated_at == 0 ? json(nullptr) : json(result.updated_at)},
    };
    return http::HttpResponse {status, {{"Content-Type", "application/json"}}, body.dump()};
}

json pacmtokenRefreshToJson(const application::model::MiguPacmtokenRefreshResult &result)
{
    json body;
    body["success"] = result.success;
    body["refreshed"] = result.refreshed;
    body["pacmtoken"] = result.pacmtoken;
    body["pacmtoken_length"] = result.pacmtoken_length;
    body["upstream_status"] = result.upstream_status == 0 ? json(nullptr) : json(result.upstream_status);
    body["updated_at"] = result.updated_at;
    body["message"] = result.message;
    return body;
}

json pacmtokenStateToJson(const application::model::MiguPacmtokenState &state, bool include_token)
{
    json body;
    body["configured"] = state.configured;
    body["has_pacmtoken"] = state.has_pacmtoken;
    body["pacmtoken_length"] = state.pacmtoken_length;
    body["updated_at"] = state.updated_at == 0 ? json(nullptr) : json(state.updated_at);
    if (include_token) {
        body["pacmtoken"] = state.pacmtoken;
    }
    return body;
}

json settingsToJson(const application::model::CrawlerRuntimeSettings &settings, const dto::CrawlerSettingsReadonlySnapshot &readonly_settings)
{
    json body;
    body["mutable"] = {
        {"max_batch_concurrency", settings.max_batch_concurrency},
        {"max_page_concurrency", settings.max_page_concurrency},
        {"max_retries", settings.max_retries},
        {"task_timeout_ms", settings.task_timeout_ms},
        {"resource_download_concurrency", settings.resource_download_concurrency},
        {"resource_download_retries", settings.resource_download_retries},
        {"resource_download_timeout_ms", settings.resource_download_timeout_ms},
        {"pacmtoken_refresh_interval_seconds", settings.pacmtoken_refresh_interval_seconds},
        {"auto_refresh_pacmtoken", settings.auto_refresh_pacmtoken},
        {"refresh_pacmtoken_on_440013", settings.refresh_pacmtoken_on_440013},
    };
    body["readonly"] = {
        {"http_worker_threads", readonly_settings.http_worker_threads},
        {"async_worker_threads", readonly_settings.async_worker_threads},
        {"resource_worker_threads", readonly_settings.resource_worker_threads},
        {"parse_worker_threads", readonly_settings.parse_worker_threads},
        {"persistence_worker_threads", readonly_settings.persistence_worker_threads},
        {"outbound_http_io_threads", readonly_settings.outbound_http_io_threads},
        {"mysql_io_threads", readonly_settings.mysql_io_threads},
        {"mysql_max_pool_size", readonly_settings.mysql_max_pool_size},
        {"persistence_backend", readonly_settings.persistence_backend},
    };
    body["notes"] = {
        {"readonly", "thread counts, storage backend and MySQL pool sizing require service restart"},
        {"mutable", "runtime scheduling and resource download settings take effect for new tasks"},
    };
    return body;
}

json progressToJson(const application::model::BatchProgressSnapshot &progress)
{
    json body;
    body["batch_id"] = progress.batch_id;
    body["state"] = progress.state_str;
    body["total"] = progress.total;
    body["processed"] = progress.processed;
    body["success"] = progress.success;
    body["failed"] = progress.failed;
    body["current_item"] = progress.current_item;
    body["recent_errors"] = progress.recent_errors;
    body["duration_seconds"] = progress.duration_seconds;
    body["percent"] = progress.total <= 0 ? 0.0 : (static_cast<double>(progress.processed) * 100.0 / static_cast<double>(progress.total));
    return body;
}

int intQuery(const http::HttpRequest &req, const std::string &name, int fallback)
{
    auto it = req.query_params.find(name);
    if (it == req.query_params.end() || it->second.empty()) {
        return fallback;
    }
    try {
        return std::stoi(it->second);
    } catch (...) {
        return fallback;
    }
}

bool parseStrictInt(const std::string &value, int &parsed_value)
{
    try {
        std::size_t parsed = 0;
        parsed_value = std::stoi(value, &parsed);
        return parsed == value.size();
    } catch (...) {
        return false;
    }
}

std::optional<http::HttpResponse> validatePageQuery(const http::HttpRequest &req)
{
    if (auto it = req.query_params.find("page"); it != req.query_params.end()) {
        int page = 0;
        if (!parseStrictInt(it->second, page) || page < 1) {
            return jsonError(400, "page must be a positive integer");
        }
    }

    if (auto it = req.query_params.find("page_size"); it != req.query_params.end()) {
        int page_size = 0;
        if (!parseStrictInt(it->second, page_size) || page_size < 1 || page_size > 200) {
            return jsonError(400, "page_size must be between 1 and 200");
        }
    }

    return std::nullopt;
}

std::string stringQuery(const http::HttpRequest &req, const std::string &name)
{
    auto it = req.query_params.find(name);
    return it == req.query_params.end() ? std::string {} : it->second;
}

std::string stringQuery(const http::HttpRequest &req, std::initializer_list<const char *> names)
{
    for (const char *name : names) {
        auto it = req.query_params.find(name);
        if (it != req.query_params.end()) {
            return it->second;
        }
    }
    return {};
}

bool boolQuery(const http::HttpRequest &req, const std::string &name, bool fallback = false)
{
    auto it = req.query_params.find(name);
    if (it == req.query_params.end()) {
        return fallback;
    }
    std::string value = it->second;
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value == "1" || value == "true" || value == "yes";
}

std::optional<int> optionalIntQuery(const http::HttpRequest &req, const std::string &name)
{
    auto it = req.query_params.find(name);
    if (it == req.query_params.end() || it->second.empty()) {
        return std::nullopt;
    }
    try {
        return std::stoi(it->second);
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<int> optionalIntQuery(const http::HttpRequest &req, std::initializer_list<const char *> names)
{
    for (const char *name : names) {
        if (auto value = optionalIntQuery(req, name)) {
            return value;
        }
    }
    return std::nullopt;
}

std::vector<int> parseStatusList(std::string value)
{
    std::replace(value.begin(), value.end(), '|', ',');
    std::vector<int> statuses;
    std::stringstream input(value);
    std::string token;
    while (std::getline(input, token, ',')) {
        token.erase(std::remove_if(token.begin(), token.end(), [](unsigned char ch) {
                        return std::isspace(ch) != 0;
                    }),
                    token.end());
        if (token.empty()) {
            continue;
        }
        try {
            const int status = std::stoi(token);
            if (status >= 0 && status <= 4) {
                statuses.push_back(status);
            }
        } catch (...) {
        }
    }
    return statuses;
}

std::vector<int> statusQuery(const http::HttpRequest &req, std::initializer_list<const char *> names)
{
    for (const char *name : names) {
        auto it = req.query_params.find(name);
        if (it != req.query_params.end()) {
            return parseStatusList(it->second);
        }
    }
    return {};
}

std::vector<int> defaultBackfillStatuses(std::vector<int> statuses, bool force = false)
{
    if (statuses.empty()) statuses = {0, 3, 4};
    if (force && std::find(statuses.begin(), statuses.end(), 2) == statuses.end()) statuses.push_back(2);
    return statuses;
}

std::string pathValue(const std::string &path, const std::string &prefix, const std::string &suffix = {})
{
    if (path.rfind(prefix, 0) != 0) {
        return {};
    }
    std::string value = path.substr(prefix.size());
    if (!suffix.empty()) {
        if (value.size() <= suffix.size() || value.substr(value.size() - suffix.size()) != suffix) {
            return {};
        }
        value.erase(value.size() - suffix.size());
    }
    return value;
}

json parseJsonOrObject(const std::string &value)
{
    auto parsed = json::parse(value.empty() ? "{}" : value, nullptr, false);
    return parsed.is_discarded() ? json::object() : parsed;
}

std::uint64_t durationSeconds(std::uint64_t started_at, std::uint64_t finished_at)
{
    if (started_at == 0) {
        return 0;
    }
    const std::uint64_t end = finished_at == 0 ? static_cast<std::uint64_t>(std::time(nullptr)) : finished_at;
    return end >= started_at ? end - started_at : 0;
}

json batchToJson(const application::model::CrawlBatchRecord &batch)
{
    return {
        {"batch_id", batch.batch_id},
        {"source", batch.source},
        {"type", batch.task_type},
        {"state", batch.state},
        {"request_params", parseJsonOrObject(batch.request_params)},
        {"settings_snapshot", parseJsonOrObject(batch.settings_snapshot)},
        {"limit", batch.limit_count},
        {"target_count", batch.target_count},
        {"processed", batch.processed_count},
        {"success", batch.success_count},
        {"failed", batch.failure_count},
        {"cancel_requested", batch.cancel_requested},
        {"last_error", batch.last_error},
        {"display_name", batch.display_name},
        {"target_summary", batch.target_summary},
        {"target_keywords", batch.target_keywords},
        {"error_category", batch.error_category},
        {"result_code", batch.result_code},
        {"created_at", batch.created_at},
        {"started_at", batch.started_at == 0 ? json(nullptr) : json(batch.started_at)},
        {"finished_at", batch.finished_at == 0 ? json(nullptr) : json(batch.finished_at)},
        {"updated_at", batch.updated_at},
        {"archived_at", batch.archived_at == 0 ? json(nullptr) : json(batch.archived_at)},
        {"archived_reason", batch.archived_reason.empty() ? json(nullptr) : json(batch.archived_reason)},
        {"parent_batch_id", batch.parent_batch_id.empty() ? json(nullptr) : json(batch.parent_batch_id)},
        {"duration_seconds", durationSeconds(batch.started_at, batch.finished_at)},
    };
}

json itemToJson(const application::model::CrawlBatchItemRecord &item)
{
    return {
        {"batch_id", item.batch_id},
        {"item_index", item.item_index},
        {"item_key", item.item_key},
        {"item_type", item.item_type},
        {"item_label", item.item_label},
        {"source", item.source},
        {"source_singer_id", item.source_singer_id},
        {"source_album_id", item.source_album_id},
        {"source_song_id", item.source_song_id},
        {"state", item.state},
        {"retry_count", item.retry_count},
        {"written_count", item.written_count},
        {"error_category", item.error_category},
        {"result_code", item.result_code},
        {"started_at", item.started_at == 0 ? json(nullptr) : json(item.started_at)},
        {"finished_at", item.finished_at == 0 ? json(nullptr) : json(item.finished_at)},
        {"last_error", item.last_error},
        {"payload", parseJsonOrObject(item.payload)},
        {"created_at", item.created_at},
        {"updated_at", item.updated_at},
    };
}

json eventToJson(const application::model::CrawlBatchEventRecord &event)
{
    return {
        {"batch_id", event.batch_id},
        {"item_index", event.item_index ? json(*event.item_index) : json(nullptr)},
        {"event_type", event.event_type},
        {"message", event.message},
        {"payload", parseJsonOrObject(event.payload)},
        {"created_at", event.created_at},
    };
}

json distributionToJson(const std::map<int, std::uint64_t> &values)
{
    json body = json::object();
    for (const auto &[state, count] : values) {
        body[std::to_string(state)] = count;
    }
    return body;
}

json nullableString(const std::string &value)
{
    return value.empty() ? json(nullptr) : json(value);
}

json nullableInt(const std::optional<int> &value)
{
    return value ? json(*value) : json(nullptr);
}

json remainingCount(int current, const std::optional<int> &total)
{
    if (!total) {
        return nullptr;
    }
    return (std::max)(0, *total - current);
}

json singerToJson(const application::model::MiguSingerCatalogRecord &singer)
{
    return {
        {"singer_db_id", singer.singer_db_id},
        {"name", singer.name},
        {"normalized_name", singer.normalized_name},
        {"gender", singer.gender},
        {"status", singer.status},
        {"source_ref_id", singer.source_ref_id},
        {"source", singer.source},
        {"source_item_key", singer.source_item_key},
        {"source_singer_id", singer.source_singer_id},
        {"source_name_pinyin", singer.source_name_pinyin},
        {"country", singer.source_country},
        {"source_tag", singer.source_tag},
        {"avatar_url", singer.avatar_url},
        {"source_song_total", nullableInt(singer.source_song_total)},
        {"source_album_total", nullableInt(singer.source_album_total)},
        {"songs_sync_status", singer.songs_sync_status},
        {"songs_synced_at", singer.songs_synced_at == 0 ? json(nullptr) : json(singer.songs_synced_at)},
        {"songs_count", singer.songs_sync_count},
        {"songs_total", nullableInt(singer.source_song_total)},
        {"songs_remaining_count", remainingCount(singer.songs_sync_count, singer.source_song_total)},
        {"songs_completed", singer.songs_sync_status == 2},
        {"songs_sync_count", singer.songs_sync_count},
        {"songs_sync_error", singer.songs_sync_error},
        {"songs_last_batch_id", nullableString(singer.songs_last_batch_id)},
        {"songs_running_batch_id", nullableString(singer.songs_running_batch_id)},
        {"albums_sync_status", singer.albums_sync_status},
        {"albums_synced_at", singer.albums_synced_at == 0 ? json(nullptr) : json(singer.albums_synced_at)},
        {"albums_count", singer.albums_sync_count},
        {"albums_total", nullableInt(singer.source_album_total)},
        {"albums_remaining_count", remainingCount(singer.albums_sync_count, singer.source_album_total)},
        {"albums_completed", singer.albums_sync_status == 2},
        {"albums_sync_count", singer.albums_sync_count},
        {"albums_sync_error", singer.albums_sync_error},
        {"albums_last_batch_id", nullableString(singer.albums_last_batch_id)},
        {"albums_running_batch_id", nullableString(singer.albums_running_batch_id)},
        {"created_at", singer.created_at},
        {"updated_at", singer.updated_at},
    };
}

json albumToJson(const application::model::MiguAlbumCatalogRecord &album)
{
    return {
        {"album_db_id", album.album_db_id},
        {"title", album.title},
        {"normalized_title", album.normalized_title},
        {"primary_artist_name", album.primary_artist_name},
        {"release_date", album.release_date},
        {"source_ref_id", album.source_ref_id},
        {"source", album.source},
        {"source_item_key", album.source_item_key},
        {"source_album_id", album.source_album_id},
        {"source_resource_type", album.source_resource_type},
        {"source_res_type", album.source_res_type},
        {"source_album_kind", album.source_album_kind},
        {"source_title", album.source_title},
        {"source_title_pinyin", album.source_title_pinyin},
        {"summary", album.summary},
        {"source_artist_id", album.source_artist_id},
        {"source_artist_name", album.source_artist_name},
        {"cover_url", album.cover_url},
        {"total_count", album.total_count},
        {"publish_date", album.publish_date},
        {"play_num", album.play_num},
        {"play_num_desc", album.play_num_desc},
        {"detail_sync_status", album.detail_sync_status},
        {"detail_synced_at", album.detail_synced_at == 0 ? json(nullptr) : json(album.detail_synced_at)},
        {"detail_sync_error", album.detail_sync_error},
        {"detail_last_batch_id", nullableString(album.detail_last_batch_id)},
        {"detail_running_batch_id", nullableString(album.detail_running_batch_id)},
        {"songs_sync_status", album.songs_sync_status},
        {"songs_synced_at", album.songs_synced_at == 0 ? json(nullptr) : json(album.songs_synced_at)},
        {"songs_count", album.album_songs_count},
        {"album_songs_count", album.album_songs_count},
        {"songs_sync_count", album.songs_sync_count},
        {"songs_sync_error", album.songs_sync_error},
        {"songs_last_batch_id", nullableString(album.songs_last_batch_id)},
        {"songs_running_batch_id", nullableString(album.songs_running_batch_id)},
        {"created_at", album.created_at},
        {"updated_at", album.updated_at},
    };
}

json songToJson(const application::model::MiguSongCatalogRecord &song)
{
    return {
        {"song_db_id", song.song_db_id},
        {"title", song.title},
        {"normalized_title", song.normalized_title},
        {"source_duration_seconds", song.source_duration_seconds},
        {"source_ref_id", song.source_ref_id},
        {"source", song.source},
        {"source_item_key", song.source_item_key},
        {"source_song_id", song.source_song_id},
        {"source_content_id", song.source_content_id},
        {"source_resource_type", song.source_resource_type},
        {"source_title_pinyin", song.source_title_pinyin},
        {"source_album_id", song.source_album_id},
        {"source_album_name", song.source_album_name},
        {"primary_artist_id", song.primary_artist_id},
        {"primary_artist_name", song.primary_artist_name},
        {"lyric_url", song.lyric_url},
        {"mrc_url", song.mrc_url},
        {"trc_url", song.trc_url},
        {"copyright_id", song.copyright_id},
        {"metadata_status", song.metadata_status},
        {"format_types", song.format_types},
        {"resource_count", song.resource_count},
        {"failed_resource_count", song.failed_resource_count},
        {"created_at", song.created_at},
        {"updated_at", song.updated_at},
    };
}

json resourceToJson(const application::model::MiguResourceQueueRecord &resource)
{
    return {
        {"resource_id", resource.resource_id},
        {"song_source_ref_id", resource.song_source_ref_id},
        {"source", resource.source},
        {"source_song_id", resource.source_song_id},
        {"source_content_id", resource.source_content_id},
        {"title", resource.title},
        {"artist", resource.artist},
        {"album", resource.album},
        {"resource_kind", resource.resource_kind},
        {"format_type", resource.format_type},
        {"local_path", resource.local_path},
        {"cloud_backend", resource.cloud_backend},
        {"cloud_path", resource.cloud_path},
        {"size_bytes", resource.size_bytes},
        {"checksum", resource.checksum},
        {"status", resource.status},
        {"retry_count", resource.retry_count},
        {"last_error", resource.last_error},
        {"error_category", resource.error_category},
        {"result_code", resource.result_code},
        {"last_batch_id", nullableString(resource.last_batch_id)},
        {"running_batch_id", nullableString(resource.running_batch_id)},
        {"last_processed_at", resource.last_processed_at == 0 ? json(nullptr) : json(resource.last_processed_at)},
        {"fetched_at", resource.fetched_at == 0 ? json(nullptr) : json(resource.fetched_at)},
        {"cloud_uploaded_at", resource.cloud_uploaded_at == 0 ? json(nullptr) : json(resource.cloud_uploaded_at)},
        {"created_at", resource.created_at},
        {"updated_at", resource.updated_at},
    };
}

template <typename Page, typename Converter>
json pageToJson(const Page &page, Converter converter)
{
    json body;
    body["items"] = json::array();
    for (const auto &item : page.items) {
        body["items"].push_back(converter(item));
    }
    body["page"] = page.page;
    body["page_size"] = page.page_size;
    body["total"] = page.total;
    return body;
}

bool isBackfillTypeSupported(const std::string &type)
{
    return type == "singer-songs" || type == "singer-albums" || type == "album-details" || type == "album-songs";
}

bool isClaimableStatus(int status)
{
    return status == 0 || status == 3 || status == 4;
}

// 预览计数与数据库认领使用相同规则；空选择必须保持空，不能退回全范围。
std::vector<int> previewRetryStatuses(std::vector<int> statuses, bool force)
{
    statuses = defaultBackfillStatuses(std::move(statuses), force);
    std::erase_if(statuses, [force](int status) { return !isClaimableStatus(status) && !(force && status == 2); });
    if (statuses.empty()) statuses.push_back(-999);
    return statuses;
}

bool matchesRequestedStatus(int status, const std::vector<int> &statuses)
{
    return statuses.empty() || std::find(statuses.begin(), statuses.end(), status) != statuses.end();
}

json eligibleActions(bool eligible, int status)
{
    json actions = json::array();
    if (eligible) {
        actions.push_back("retry");
        actions.push_back("reset");
    } else if (status == 1) {
        actions.push_back("recover-stale");
    }
    return actions;
}

std::string ineligibleReason(bool eligible, int status, std::string extra_reason = {})
{
    if (eligible) {
        return {};
    }
    if (!extra_reason.empty()) {
        return extra_reason;
    }
    if (status == 1) {
        return "running";
    }
    if (status == 2) {
        return "completed";
    }
    return "status_not_claimable";
}

json withPreviewEligibility(json item, const std::vector<int> &statuses, bool force = false)
{
    const bool complete = item["sync_status"].is_number_integer() && item["sync_status"].get<int>() == 2;
    const auto reason = item.value("reason", "");
    const bool base_eligible = item.value("eligible", false) || (force && complete && reason != "album_detail_not_completed" && reason != "shared_release_archive");
    const bool status_matched = (force && complete) || (item["sync_status"].is_number_integer() && matchesRequestedStatus(item["sync_status"].get<int>(), statuses));
    const bool eligible = base_eligible && status_matched;
    const int status = item["sync_status"].is_number_integer() ? item["sync_status"].get<int>() : -1;
    item["eligible"] = eligible;
    if (eligible) item["reason"] = "";
    item["eligible_actions"] = eligibleActions(eligible, status);
    if (!eligible && base_eligible && !status_matched) {
        item["reason"] = "status_not_selected";
    } else if (!eligible && item.value("reason", "").empty()) {
        item["reason"] = "status_not_claimable";
    }
    return item;
}

json singerQueueTargetToJson(const application::model::MiguSingerCatalogRecord &singer, const std::string &type)
{
    const bool songs_type = type == "singer-songs";
    const int status = songs_type ? singer.songs_sync_status : singer.albums_sync_status;
    const int count = songs_type ? singer.songs_sync_count : singer.albums_sync_count;
    const auto total = songs_type ? singer.source_song_total : singer.source_album_total;
    const std::uint64_t synced_at = songs_type ? singer.songs_synced_at : singer.albums_synced_at;
    const std::string error = songs_type ? singer.songs_sync_error : singer.albums_sync_error;
    const std::string last_batch_id = songs_type ? singer.songs_last_batch_id : singer.albums_last_batch_id;
    const std::string running_batch_id = songs_type ? singer.songs_running_batch_id : singer.albums_running_batch_id;
    const bool eligible = isClaimableStatus(status);

    return {
        {"target_id", singer.source_singer_id},
        {"target_type", "singer"},
        {"target_name", singer.name},
        {"owner_name", ""},
        {"source", singer.source},
        {"sync_status", status},
        {"sync_count", count},
        {"source_total", nullableInt(total)},
        {"remaining_count", remainingCount(count, total)},
        {"completed", status == 2},
        {"last_error", error},
        {"last_batch_id", nullableString(last_batch_id)},
        {"running_batch_id", nullableString(running_batch_id)},
        {"last_processed_at", synced_at == 0 ? json(nullptr) : json(synced_at)},
        {"eligible_actions", eligibleActions(eligible, status)},
        {"eligible", eligible},
        {"reason", ineligibleReason(eligible, status)},
    };
}

json albumQueueTargetToJson(const application::model::MiguAlbumCatalogRecord &album, const std::string &type)
{
    const bool detail_type = type == "album-details";
    const int status = detail_type ? album.detail_sync_status : album.songs_sync_status;
    const int count = detail_type ? 0 : album.songs_sync_count;
    const std::uint64_t synced_at = detail_type ? album.detail_synced_at : album.songs_synced_at;
    const std::string error = detail_type ? album.detail_sync_error : album.songs_sync_error;
    const std::string last_batch_id = detail_type ? album.detail_last_batch_id : album.songs_last_batch_id;
    const std::string running_batch_id = detail_type ? album.detail_running_batch_id : album.songs_running_batch_id;
    const std::string prerequisite_reason = application::model::isSharedReleaseArchive(album) ? "shared_release_archive" : (type == "album-songs" && album.detail_sync_status != 2 ? "album_detail_not_completed" : "");
    const bool eligible = prerequisite_reason.empty() && isClaimableStatus(status);

    return {
        {"target_id", album.source_album_id},
        {"target_type", "album"},
        {"target_name", album.source_title.empty() ? album.title : album.source_title},
        {"owner_name", album.source_artist_name.empty() ? album.primary_artist_name : album.source_artist_name},
        {"source", album.source},
        {"sync_status", status},
        {"sync_count", count},
        {"last_error", error},
        {"last_batch_id", nullableString(last_batch_id)},
        {"running_batch_id", nullableString(running_batch_id)},
        {"last_processed_at", synced_at == 0 ? json(nullptr) : json(synced_at)},
        {"eligible_actions", eligibleActions(eligible, status)},
        {"eligible", eligible},
        {"reason", ineligibleReason(eligible, status, prerequisite_reason)},
    };
}

// 转换从库歌手同步预览响应，只暴露前端启动前确认所需字段。
json singerFromDbPreviewToJson(const application::model::SingerSourceSyncPreviewResult &preview, bool songs_type)
{
    json body;
    body["matched_count"] = preview.matched_count;
    body["eligible_count"] = preview.eligible_count;
    body["items"] = json::array();
    for (const auto &target : preview.items) {
        const auto total = songs_type ? target.source_song_total : target.source_album_total;
        json item;
        item["singer_id"] = target.source_singer_id;
        item["source_name"] = target.name;
        item["source_song_total"] = nullableInt(target.source_song_total);
        item["source_album_total"] = nullableInt(target.source_album_total);
        if (!target.source_country.empty()) {
            item["country"] = target.source_country;
        }
        item["gender"] = target.gender;
        item["sync_status"] = target.sync_status;
        item["sync_count"] = target.sync_count;
        item["source_total"] = nullableInt(total);
        item["remaining_count"] = remainingCount(target.sync_count, total);
        item["completed"] = target.sync_status == 2;
        body["items"].push_back(std::move(item));
    }
    return body;
}

json backfillQueuePageToJson(const application::model::MiguSingerCatalogPage &page, const std::string &type)
{
    json body;
    body["items"] = json::array();
    for (const auto &item : page.items) {
        body["items"].push_back(singerQueueTargetToJson(item, type));
    }
    body["page"] = page.page;
    body["page_size"] = page.page_size;
    body["total"] = page.total;
    return body;
}

json backfillQueuePageToJson(const application::model::MiguAlbumCatalogPage &page, const std::string &type)
{
    json body;
    body["items"] = json::array();
    for (const auto &item : page.items) {
        body["items"].push_back(albumQueueTargetToJson(item, type));
    }
    body["page"] = page.page;
    body["page_size"] = page.page_size;
    body["total"] = page.total;
    return body;
}

application::model::MiguSingerCatalogQuery singerCatalogQueryFromRequest(const http::HttpRequest &req)
{
    application::model::MiguSingerCatalogQuery query;
    query.page = intQuery(req, "page", 1);
    query.page_size = intQuery(req, "page_size", 20);
    query.keyword = stringQuery(req, "q");
    query.source_singer_id = stringQuery(req, "source_singer_id");
    query.country = stringQuery(req, "country");
    query.gender = optionalIntQuery(req, "gender");
    query.songs_sync_statuses = statusQuery(req, {"songs_sync_status", "songs_status"});
    query.albums_sync_statuses = statusQuery(req, {"albums_sync_status", "albums_status"});
    query.min_songs_count = optionalIntQuery(req, {"min_songs_count", "songs_count_min"});
    query.max_songs_count = optionalIntQuery(req, {"max_songs_count", "songs_count_max"});
    query.min_albums_count = optionalIntQuery(req, {"min_albums_count", "albums_count_min"});
    query.max_albums_count = optionalIntQuery(req, {"max_albums_count", "albums_count_max"});
    return query;
}

application::model::MiguAlbumCatalogQuery albumCatalogQueryFromRequest(const http::HttpRequest &req)
{
    application::model::MiguAlbumCatalogQuery query;
    query.page = intQuery(req, "page", 1);
    query.page_size = intQuery(req, "page_size", 20);
    query.keyword = stringQuery(req, "q");
    query.artist = stringQuery(req, "artist");
    query.album_kind = optionalIntQuery(req, "kind");
    query.detail_sync_statuses = statusQuery(req, {"detail_sync_status", "detail_status"});
    query.songs_sync_statuses = statusQuery(req, {"songs_sync_status", "songs_status"});
    query.min_songs_count = optionalIntQuery(req, {"min_songs_count", "songs_count_min", "min_album_songs_count", "album_songs_count_min"});
    query.max_songs_count = optionalIntQuery(req, {"max_songs_count", "songs_count_max", "max_album_songs_count", "album_songs_count_max"});
    return query;
}

application::model::MiguSongCatalogQuery songCatalogQueryFromRequest(const http::HttpRequest &req)
{
    application::model::MiguSongCatalogQuery query;
    query.page = intQuery(req, "page", 1);
    query.page_size = intQuery(req, "page_size", 20);
    query.keyword = stringQuery(req, "q");
    query.artist = stringQuery(req, "artist");
    query.album = stringQuery(req, "album");
    query.resource_statuses = statusQuery(req, {"resource_status", "status"});
    query.format_type = stringQuery(req, "format");
    return query;
}

application::model::MiguResourceQueueQuery resourceQueueQueryFromRequest(const http::HttpRequest &req)
{
    application::model::MiguResourceQueueQuery query;
    query.page = intQuery(req, "page", 1);
    query.page_size = intQuery(req, "page_size", 20);
    query.keyword = stringQuery(req, "q");
    query.statuses = statusQuery(req, {"status", "resource_status"});
    query.resource_kind = normalizeResourceKind(stringQuery(req, {"kind", "resource_kind", "resourceKind"}));
    query.format_type = stringQuery(req, {"format", "format_type", "formatType"});
    return query;
}

std::vector<std::string> stringArrayField(const json &body, std::initializer_list<const char *> names)
{
    for (const char *name : names) {
        if (!body.contains(name)) {
            continue;
        }
        if (body[name].is_string()) {
            return {body[name].get<std::string>()};
        }
        if (body[name].is_array()) {
            std::vector<std::string> values;
            for (const auto &item : body[name]) {
                if (item.is_string()) {
                    values.push_back(item.get<std::string>());
                } else if (item.is_number_unsigned()) {
                    values.push_back(std::to_string(item.get<std::uint64_t>()));
                } else if (item.is_number_integer()) {
                    const auto value = item.get<long long>();
                    if (value >= 0) {
                        values.push_back(std::to_string(value));
                    }
                }
            }
            return values;
        }
    }
    return {};
}

std::vector<int> intArrayField(const json &body, std::initializer_list<const char *> names)
{
    for (const char *name : names) {
        if (!body.contains(name)) {
            continue;
        }
        if (body[name].is_number_integer()) {
            return {body[name].get<int>()};
        }
        if (body[name].is_array()) {
            std::vector<int> values;
            for (const auto &item : body[name]) {
                if (item.is_number_integer()) {
                    values.push_back(item.get<int>());
                }
            }
            return values;
        }
    }
    return {};
}

std::vector<std::uint64_t> uint64ArrayField(const json &body, std::initializer_list<const char *> names)
{
    std::vector<std::uint64_t> values;
    const auto strings = stringArrayField(body, names);
    values.reserve(strings.size());
    for (const auto &value : strings) {
        try {
            std::size_t parsed = 0;
            const auto id = std::stoull(value, &parsed);
            if (parsed == value.size()) {
                values.push_back(id);
            }
        } catch (...) {
        }
    }
    return values;
}

std::optional<std::string> jsonStringField(const json &body, std::initializer_list<const char *> names)
{
    for (const char *name : names) {
        if (body.contains(name) && body[name].is_string()) {
            return body[name].get<std::string>();
        }
    }
    return std::nullopt;
}

std::optional<int> jsonIntField(const json &body, std::initializer_list<const char *> names)
{
    for (const char *name : names) {
        if (body.contains(name) && body[name].is_number_integer()) {
            return body[name].get<int>();
        }
    }
    return std::nullopt;
}

std::optional<bool> jsonBoolField(const json &body, std::initializer_list<const char *> names)
{
    for (const char *name : names) {
        if (!body.contains(name)) {
            continue;
        }
        if (body[name].is_boolean()) {
            return body[name].get<bool>();
        }
        if (body[name].is_number_integer()) {
            return body[name].get<int>() != 0;
        }
        if (body[name].is_string()) {
            std::string value = body[name].get<std::string>();
            std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
                return static_cast<char>(std::tolower(ch));
            });
            return value == "1" || value == "true" || value == "yes";
        }
    }
    return std::nullopt;
}

void parseResourceProcessOptions(const json &body, application::model::SongResourceProcessOptionOverrides &options)
{
    if (auto concurrency = jsonIntField(body, {"resource_download_concurrency", "resourceDownloadConcurrency", "concurrency"})) {
        options.resource_download_concurrency = *concurrency;
    }
    if (auto retries = jsonIntField(body, {"resource_download_retries", "resourceDownloadRetries", "retries"})) {
        options.resource_download_retries = *retries;
    }
    if (auto timeout = jsonIntField(body, {"resource_download_timeout_ms", "resourceDownloadTimeoutMs", "timeout_ms", "timeoutMs"})) {
        options.resource_download_timeout_ms = *timeout;
    }
    if (auto interval = jsonIntField(body, {"pacmtoken_refresh_interval_seconds", "pacmtokenRefreshIntervalSeconds"})) {
        options.pacmtoken_refresh_interval_seconds = *interval;
    }
    if (auto auto_refresh = jsonBoolField(body, {"auto_refresh_pacmtoken", "autoRefreshPacmtoken"})) {
        options.auto_refresh_pacmtoken = *auto_refresh;
    }
    if (auto refresh_on_440013 = jsonBoolField(body, {"refresh_pacmtoken_on_440013", "refreshPacmtokenOn440013"})) {
        options.refresh_pacmtoken_on_440013 = *refresh_on_440013;
    }
    if (auto process_related = jsonBoolField(body, {"process_related_resources", "processRelatedResources"})) {
        options.process_related_resources = *process_related;
    }
}

std::optional<std::string> validateResourceProcessOptions(const application::model::SongResourceProcessOptionOverrides &options)
{
    if (options.resource_download_concurrency && (*options.resource_download_concurrency < 1 || *options.resource_download_concurrency > 32)) {
        return "resource_download_concurrency must be between 1 and 32";
    }
    if (options.resource_download_retries && (*options.resource_download_retries < 0 || *options.resource_download_retries > 5)) {
        return "resource_download_retries must be between 0 and 5";
    }
    if (options.resource_download_timeout_ms && (*options.resource_download_timeout_ms < 10000 || *options.resource_download_timeout_ms > 600000)) {
        return "resource_download_timeout_ms must be between 10000 and 600000";
    }
    if (options.pacmtoken_refresh_interval_seconds && (*options.pacmtoken_refresh_interval_seconds < 0 || *options.pacmtoken_refresh_interval_seconds > 3600)) {
        return "pacmtoken_refresh_interval_seconds must be between 0 and 3600";
    }
    return std::nullopt;
}

std::optional<application::model::MiguBackfillTargetQuery> backfillQueryFromBody(const http::HttpRequest &req)
{
    try {
        application::model::MiguBackfillTargetQuery query;
        query.page = 1;
        query.page_size = 50;
        query.limit = 50;
        if (req.body.empty()) {
            return query;
        }
        auto body = json::parse(req.body);
        if (auto source = jsonStringField(body, {"source"})) {
            query.source = *source;
        }
        query.limit = jsonIntField(body, {"limit", "max_targets", "maxTargets"}).value_or(50);
        query.source_ids = stringArrayField(body, {"source_ids", "sourceIds", "source_singer_ids", "sourceSingerIds", "source_album_ids", "sourceAlbumIds"});
        if (body.contains("force") && !body["force"].is_boolean()) return std::nullopt;
        query.force = body.value("force", false);
        const json *filter = &body;
        if (body.contains("filter") && body["filter"].is_object()) {
            filter = &body["filter"];
        }
        query.statuses = intArrayField(*filter, {"status", "sync_status", "syncStatus", "sync_statuses", "syncStatuses"});
        if (auto keyword = jsonStringField(*filter, {"q", "keyword"})) {
            query.keyword = *keyword;
        }
        if (auto country = jsonStringField(*filter, {"country"})) {
            query.country = *country;
        }
        if (auto artist = jsonStringField(*filter, {"artist"})) {
            query.artist = *artist;
        }
        query.gender = jsonIntField(*filter, {"gender"});
        query.album_kind = jsonIntField(*filter, {"kind", "album_kind", "albumKind"});
        return query;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<application::model::MiguResourceActionQuery> resourceActionQueryFromBody(const http::HttpRequest &req)
{
    try {
        application::model::MiguResourceActionQuery query;
        if (req.body.empty()) {
            return query;
        }
        auto body = json::parse(req.body);
        if (auto source = jsonStringField(body, {"source"})) {
            query.source = *source;
        }
        if (auto action = jsonStringField(body, {"action"})) {
            query.action = *action;
        }
        query.limit = jsonIntField(body, {"limit", "max_targets", "maxTargets"}).value_or(50);
        query.stale_after_seconds = jsonIntField(body, {"stale_after_seconds", "staleAfterSeconds"}).value_or(3600);
        query.resource_ids = uint64ArrayField(body, {"resource_ids", "resourceIds", "ids"});
        const json *filter = &body;
        if (body.contains("filter") && body["filter"].is_object()) {
            filter = &body["filter"];
        }
        if (query.resource_ids.empty()) {
            query.resource_ids = uint64ArrayField(*filter, {"resource_ids", "resourceIds", "ids"});
        }
        if (query.action == "process" && !query.resource_ids.empty()) {
            query.statuses = {0, 3};
            return query;
        }
        query.statuses = intArrayField(*filter, {"status", "resource_status", "resourceStatus", "statuses"});
        if (auto keyword = jsonStringField(*filter, {"q", "keyword"})) {
            query.keyword = *keyword;
        }
        if (auto kind = jsonStringField(*filter, {"kind", "resource_kind", "resourceKind"})) {
            query.resource_kind = normalizeResourceKind(*kind);
        }
        if (auto format = jsonStringField(*filter, {"format", "format_type", "formatType"})) {
            query.format_type = *format;
        }
        return query;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<application::model::SongResourceClaimQuery> resourceProcessQueryFromBody(const http::HttpRequest &req)
{
    try {
        application::model::SongResourceClaimQuery query;
        query.limit = 20;
        if (req.body.empty()) {
            return query;
        }
        auto body = json::parse(req.body);
        if (!body.is_object()) {
            return std::nullopt;
        }
        if (auto source = jsonStringField(body, {"source"})) {
            query.source = *source;
        }
        if (auto batch_id = jsonStringField(body, {"batch_id", "batchId"})) {
            query.batch_id = *batch_id;
        }
        const json *filter = &body;
        if (body.contains("filter") && body["filter"].is_object()) {
            filter = &body["filter"];
        }
        query.resource_ids = uint64ArrayField(body, {"resource_ids", "resourceIds", "ids"});
        if (query.resource_ids.empty()) {
            query.resource_ids = uint64ArrayField(*filter, {"resource_ids", "resourceIds", "ids"});
        }
        const auto requested_limit = jsonIntField(body, {"limit", "max_targets", "maxTargets"});
        if (requested_limit) {
            query.limit = static_cast<std::size_t>((std::max)(1, *requested_limit));
        } else if (!query.resource_ids.empty()) {
            query.limit = query.resource_ids.size();
        } else {
            query.limit = 20;
        }
        query.offset = static_cast<std::size_t>((std::max)(0, jsonIntField(body, {"offset"}).value_or(0)));

        if (!query.resource_ids.empty()) {
            query.offset = 0;
            query.statuses = {0, 3};
        } else {
            query.statuses = intArrayField(*filter, {"status", "resource_status", "resourceStatus", "statuses"});
            if (auto kind = jsonStringField(*filter, {"kind", "resource_kind", "resourceKind"})) {
                query.resource_kind = normalizeResourceKind(*kind);
            }
            if (auto content_id = jsonStringField(*filter, {"content_id", "contentId", "source_content_id", "sourceContentId"})) {
                query.source_content_id = *content_id;
            }
            if (auto format = jsonStringField(*filter, {"format", "format_type", "formatType"})) {
                query.format_type = *format;
            }
        }
        if (body.contains("options")) {
            if (!body["options"].is_object()) {
                return std::nullopt;
            }
            parseResourceProcessOptions(body["options"], query.option_overrides);
        }
        return query;
    } catch (...) {
        return std::nullopt;
    }
}

bool resourceProcessAsyncRequested(const http::HttpRequest &req)
{
    bool requested = boolQuery(req, "async", false);
    if (req.body.empty()) {
        return requested;
    }
    auto body = json::parse(req.body, nullptr, false);
    if (!body.is_object()) {
        return requested;
    }
    if (auto value = jsonBoolField(body, {"async"})) {
        requested = *value;
    }
    return requested;
}

bool hasResetSelector(const application::model::MiguBackfillTargetQuery &query)
{
    return !query.source_ids.empty() || !query.statuses.empty() || !query.keyword.empty() || !query.country.empty() || query.gender || !query.artist.empty() || query.album_kind;
}

bool hasResourceSelector(const application::model::MiguResourceActionQuery &query)
{
    return !query.resource_ids.empty() || !query.statuses.empty() || !query.keyword.empty() || !query.resource_kind.empty() || !query.format_type.empty();
}

application::model::MiguResourceActionQuery processPreviewQueryFromClaim(const application::model::SongResourceClaimQuery &query)
{
    application::model::MiguResourceActionQuery preview;
    preview.source = query.source.empty() ? "migu" : query.source;
    preview.action = "process";
    preview.resource_ids = query.resource_ids;
    preview.statuses = {0, 3};
    preview.resource_kind = query.resource_kind;
    preview.format_type = query.format_type;
    preview.limit = static_cast<int>((std::min<std::size_t>)(query.limit == 0 ? query.resource_ids.size() : query.limit, 200));
    if (preview.limit <= 0) {
        preview.limit = 50;
    }
    return preview;
}

void addProcessPreviewAliases(json &body, const application::model::MiguResourcePreviewResult &preview)
{
    body["downloadable_count"] = preview.eligible_count;
    body["skipped_count"] = preview.ineligible_count;
}

// 判断维护恢复类型是否在当前支持范围内。
bool isRecoveryTypeSupported(const std::string &type)
{
    static const std::vector<std::string> supported {
        "singer-songs",
        "singer-albums",
        "album-details",
        "album-songs",
        "resources",
        "batches",
    };
    return std::find(supported.begin(), supported.end(), type) != supported.end();
}

// 解析 stale running 恢复请求；空 body 使用默认恢复范围。
std::optional<application::model::MiguStaleRecoveryRequest> staleRecoveryRequestFromBody(const http::HttpRequest &req)
{
    try {
        application::model::MiguStaleRecoveryRequest request;
        if (req.body.empty()) {
            return request;
        }

        auto body = json::parse(req.body);
        if (!body.is_object()) {
            return std::nullopt;
        }
        if (auto source = jsonStringField(body, {"source"})) {
            request.source = *source;
        }
        request.stale_after_seconds = jsonIntField(body, {"stale_after_seconds", "staleAfterSeconds"}).value_or(request.stale_after_seconds);
        request.types = stringArrayField(body, {"types", "type"});
        for (const auto &type : request.types) {
            if (!isRecoveryTypeSupported(type)) {
                return std::nullopt;
            }
        }
        return request;
    } catch (...) {
        return std::nullopt;
    }
}

int limitPerTargetFromBody(const http::HttpRequest &req)
{
    try {
        if (req.body.empty()) {
            return 20;
        }
        auto body = json::parse(req.body);
        return jsonIntField(body, {"limit_per_target", "limitPerTarget", "limit_per_singer", "limitPerSinger"}).value_or(20);
    } catch (...) {
        return 20;
    }
}

std::string backfillTypeFromPath(const std::string &path, const std::string &suffix)
{
    return pathValue(path, "/api/migu/backfill/", suffix);
}

json actionItemsToJson(const std::vector<application::model::MiguBackfillActionItem> &items)
{
    json body = json::array();
    for (const auto &item : items) {
        body.push_back({
            {"source_id", item.source_id},
            {"name", item.name},
            {"status", item.status},
            {"reason", item.reason},
        });
    }
    return body;
}

json resourceActionItemsToJson(const std::vector<application::model::MiguResourceActionItem> &items)
{
    json body = json::array();
    for (const auto &item : items) {
        body.push_back({
            {"resource_id", item.resource_id},
            {"source_id", item.source_id},
            {"name", item.name},
            {"status", item.status},
            {"reason", item.reason},
        });
    }
    return body;
}

json resourceActionResultToJson(std::string action, const application::model::MiguResourceActionResult &result)
{
    return {
        {"action", std::move(action)},
        {"updated_count", result.updated_count},
        {"success_count", result.success_count},
        {"failed_count", result.failed_count},
        {"skipped_count", result.skipped_count},
        {"items", resourceActionItemsToJson(result.items)},
    };
}

json resourcePreviewToJson(const application::model::MiguResourcePreviewResult &preview)
{
    json body;
    body["matched_count"] = preview.matched_count;
    body["eligible_count"] = preview.eligible_count;
    body["ineligible_count"] = preview.ineligible_count;
    body["missing_count"] = preview.missing_count;
    body["items"] = json::array();
    for (const auto &item : preview.items) {
        body["items"].push_back({
            {"resource_id", item.resource_id},
            {"target_id", item.target_id},
            {"target_name", item.target_name},
            {"resource_kind", item.resource_kind},
            {"format_type", item.format_type},
            {"sync_status", item.sync_status},
            {"eligible", item.eligible},
            {"reason", item.reason},
        });
    }
    return body;
}

json resourceProcessResultToJson(const application::model::SongResourceProcessResult &result)
{
    json body;
    body["batch_id"] = result.batch_id;
    body["claimed_count"] = result.claimed_count;
    body["succeeded_count"] = result.succeeded_count;
    body["failed_count"] = result.failed_count;
    body["skipped_count"] = result.skipped_count;
    body["related_succeeded_count"] = result.related_succeeded_count;
    body["related_failed_count"] = result.related_failed_count;
    body["related_skipped_count"] = result.related_skipped_count;
    body["synthetic_related_skipped_count"] = result.synthetic_related_skipped_count;
    body["items"] = json::array();
    for (const auto &item : result.items) {
        body["items"].push_back({
            {"resource_id", item.resource_id},
            {"song_source_ref_id", item.song_source_ref_id},
            {"source", item.source},
            {"source_song_id", item.source_song_id},
            {"source_content_id", item.source_content_id},
            {"title", item.title},
            {"resource_kind", item.resource_kind},
            {"format_type", item.format_type},
            {"ok", item.ok},
            {"skipped", item.skipped},
            {"related", item.related},
            {"synthetic", item.synthetic},
            {"local_path", item.local_path},
            {"cloud_backend", item.cloud_backend},
            {"cloud_path", item.cloud_path},
            {"size_bytes", item.size_bytes},
            {"checksum", item.checksum},
            {"error_category", item.error_category},
            {"result_code", item.result_code},
            {"message", item.message},
        });
    }
    return body;
}

json resourceProcessAcceptedToJson(const std::string &batch_id)
{
    return {
        {"accepted", true},
        {"async", true},
        {"batch_id", batch_id},
        {"task_url", "/api/migu/tasks/" + batch_id},
        {"items_url", "/api/migu/tasks/" + batch_id + "/items"},
        {"events_url", "/api/migu/tasks/" + batch_id + "/events"},
    };
}

json pairDistributionToJson(const std::vector<std::pair<std::string, std::uint64_t>> &values)
{
    json body = json::array();
    for (const auto &[key, count] : values) {
        body.push_back({{"key", key}, {"count", count}});
    }
    return body;
}

json taskSummaryToJson(const application::model::CrawlBatchSummary &summary)
{
    return {
        {"state_distribution", pairDistributionToJson(summary.state_distribution)},
        {"failure_task_types", pairDistributionToJson(summary.failure_task_types)},
        {"latest_success", summary.latest_success ? batchToJson(*summary.latest_success) : json(nullptr)},
        {"latest_failed", summary.latest_failed ? batchToJson(*summary.latest_failed) : json(nullptr)},
        {"running_count", summary.running_count},
        {"oldest_stale_running_at", summary.oldest_stale_running_at == 0 ? json(nullptr) : json(summary.oldest_stale_running_at)},
        {"generated_at", summary.generated_at},
    };
}

json batchGovernanceResultToJson(const application::model::CrawlBatchGovernanceResult &result)
{
    json body;
    body["batch_id"] = result.batch_id.empty() ? json(nullptr) : json(result.batch_id);
    body["claimed_count"] = result.claimed_count;
    body["success_count"] = result.claimed_count;
    body["skipped_count"] = result.skipped_count;
    body["failed_count"] = result.skipped_count;
    body["items"] = json::array();
    for (const auto &item : result.items) {
        body["items"].push_back({{"source_id", item.source_id}, {"name", item.name}, {"status", item.status}, {"reason", item.reason}});
    }
    return body;
}

json governanceEventToJson(const application::model::MiguGovernanceEventRecord &event)
{
    return {
        {"event_id", event.event_id},
        {"source", event.source},
        {"action", event.action},
        {"target_type", event.target_type},
        {"target_id", event.target_id},
        {"target_name", nullableString(event.target_name)},
        {"target_summary", nullableString(event.target_summary)},
        {"target_source_id", nullableString(event.target_source_id)},
        {"target_owner_name", nullableString(event.target_owner_name)},
        {"batch_id", nullableString(event.batch_id)},
        {"before_status", nullableString(event.before_status)},
        {"after_status", nullableString(event.after_status)},
        {"result_code", nullableString(event.result_code)},
        {"error_category", nullableString(event.error_category)},
        {"reason", nullableString(event.reason)},
        {"payload", parseJsonOrObject(event.payload)},
        {"created_at", event.created_at},
    };
}

std::optional<application::model::CrawlBatchGovernanceRequest> batchGovernanceRequestFromBody(const http::HttpRequest &req)
{
    try {
        application::model::CrawlBatchGovernanceRequest request;
        if (req.body.empty()) {
            return request;
        }
        auto body = json::parse(req.body);
        if (!body.is_object()) {
            return std::nullopt;
        }
        if (auto mode = jsonStringField(body, {"mode"})) {
            request.mode = *mode;
        }
        request.states = stringArrayField(body, {"state", "states"});
        if (auto error_category = jsonStringField(body, {"error_category", "errorCategory"})) {
            request.error_category = *error_category;
        }
        if (auto result_code = jsonStringField(body, {"result_code", "resultCode"})) {
            request.result_code = *result_code;
        }
        request.limit = jsonIntField(body, {"limit"}).value_or(request.limit);
        request.limit_per_target = jsonIntField(body, {"limit_per_target", "limitPerTarget"}).value_or(request.limit_per_target);
        if (auto reason = jsonStringField(body, {"reason"})) {
            request.reason = *reason;
        }
        return request;
    } catch (...) {
        return std::nullopt;
    }
}

application::model::MiguGovernanceEventQuery governanceEventQueryFromRequest(const http::HttpRequest &req)
{
    application::model::MiguGovernanceEventQuery query;
    query.page = intQuery(req, "page", 1);
    query.page_size = intQuery(req, "page_size", 20);
    query.target_type = stringQuery(req, "target_type");
    query.target_id = stringQuery(req, "target_id");
    query.action = stringQuery(req, "action");
    query.batch_id = stringQuery(req, "batch_id");
    return query;
}



http::HttpResponse taskAcceptedResponse(
    std::string batch_id,
    std::string message,
    std::size_t target_count,
    int limit,
    int start_index,
    int end_index,
    std::optional<int> claimed_count = std::nullopt,
    std::optional<int> skipped_count = std::nullopt,
    json items = nullptr
)
{
    json body;
    body["batch_id"] = std::move(batch_id);
    body["message"] = std::move(message);
    body["target_count"] = target_count;
    body["limit"] = limit;
    body["start_index"] = start_index;
    body["end_index"] = end_index;
    if (claimed_count) {
        body["claimed_count"] = *claimed_count;
    }
    if (skipped_count) {
        body["skipped_count"] = *skipped_count;
        body["failed_count"] = *skipped_count;
    }
    if (claimed_count) {
        body["success_count"] = *claimed_count;
    }
    if (!items.is_null()) {
        body["items"] = std::move(items);
    }
    return http::HttpResponse {202, {{"Content-Type", "application/json"}}, body.dump()};
}

std::size_t batchTargetCount(const std::shared_ptr<application::use_cases::CrawlerService> &service, const std::string &batch_id)
{
    if (!service || batch_id.empty()) {
        return 0;
    }
    if (auto progress = service->getBatchProgress(batch_id)) {
        return static_cast<std::size_t>((std::max)(0, progress->total));
    }
    if (auto batch = service->findBatch(batch_id)) {
        return static_cast<std::size_t>((std::max)(0, batch->target_count));
    }
    return 0;
}

} // namespace

CrawlerController::CrawlerController(
    std::shared_ptr<application::use_cases::CrawlerService> service,
    dto::CrawlerSettingsReadonlySnapshot readonly_settings,
    std::shared_ptr<application::use_cases::MiguPacmtokenService> pacmtoken_service,
    std::shared_ptr<application::use_cases::MetadataCoverService> cover_service
) : cover_service_(std::move(cover_service)), service_(std::move(service)),
    readonly_settings_(std::move(readonly_settings)),
    pacmtoken_service_(std::move(pacmtoken_service))
{}

http::HttpResponse CrawlerController::handleStartMiguSingerIndexCrawl(http::HttpRequest req)
{
    auto request = dto::MiguSingerIndexCrawlRequestDto::fromRequest(req);
    if (!request) {
        return jsonError(400, "invalid migu singer index crawl request");
    }
    if (auto error = request->validate()) {
        return jsonError(400, *error);
    }

    auto targets = request->selectedTargets();
    const std::string batch_id = service_->startBatchAsync(targets, request->limit, "singerIndex", req.body);
    const int end_index = request->end_index.value_or(static_cast<int>(request->tabs.size()));
    return taskAcceptedResponse(batch_id, "migu singer index crawl started", targets.size(), request->limit, request->start_index, end_index);
}


http::HttpResponse CrawlerController::handleStartMiguSingerSongsCrawlFromDb(http::HttpRequest req)
{
    auto request = dto::MiguSingerFromDbCrawlRequestDto::fromRequest(req);
    if (!request) {
        return jsonError(400, "invalid migu singer songs from db crawl request");
    }
    if (auto error = request->validate()) {
        return jsonError(400, *error);
    }

    const std::string batch_id = service_->startSingerSongsFromDbAsync(request->query, request->limit_per_singer, req.body.empty() ? R"({"source":"migu"})" : req.body);
    if (batch_id.empty()) {
        return jsonError(503, "migu singer source sync queue is not configured");
    }
    const auto target_count = batchTargetCount(service_, batch_id);
    return taskAcceptedResponse(batch_id, "migu singer songs crawl from db started", target_count, request->limit_per_singer, 0, static_cast<int>(target_count));
}

http::HttpResponse CrawlerController::handlePreviewMiguSingerSongsCrawlFromDb(http::HttpRequest req)
{
    auto request = dto::MiguSingerFromDbCrawlRequestDto::fromRequest(req);
    if (!request) {
        return jsonError(400, "invalid migu singer songs from db preview request");
    }
    if (auto error = request->validate()) {
        return jsonError(400, *error);
    }

    auto preview = service_->previewSingerSongsFromDb(request->query);
    if (!preview) {
        return jsonError(503, "migu singer source sync queue is not configured");
    }
    return http::HttpResponse {200, {{"Content-Type", "application/json"}}, singerFromDbPreviewToJson(*preview, true).dump()};
}


http::HttpResponse CrawlerController::handleStartMiguSingerAlbumsCrawlFromDb(http::HttpRequest req)
{
    auto request = dto::MiguSingerFromDbCrawlRequestDto::fromRequest(req);
    if (!request) {
        return jsonError(400, "invalid migu singer albums from db crawl request");
    }
    if (auto error = request->validate()) {
        return jsonError(400, *error);
    }

    const std::string batch_id = service_->startSingerAlbumsFromDbAsync(request->query, request->limit_per_singer, req.body.empty() ? R"({"source":"migu"})" : req.body);
    if (batch_id.empty()) {
        return jsonError(503, "migu singer source sync queue is not configured");
    }
    const auto target_count = batchTargetCount(service_, batch_id);
    return taskAcceptedResponse(batch_id, "migu singer albums crawl from db started", target_count, request->limit_per_singer, 0, static_cast<int>(target_count));
}

http::HttpResponse CrawlerController::handlePreviewMiguSingerAlbumsCrawlFromDb(http::HttpRequest req)
{
    auto request = dto::MiguSingerFromDbCrawlRequestDto::fromRequest(req);
    if (!request) {
        return jsonError(400, "invalid migu singer albums from db preview request");
    }
    if (auto error = request->validate()) {
        return jsonError(400, *error);
    }

    auto preview = service_->previewSingerAlbumsFromDb(request->query);
    if (!preview) {
        return jsonError(503, "migu singer source sync queue is not configured");
    }
    return http::HttpResponse {200, {{"Content-Type", "application/json"}}, singerFromDbPreviewToJson(*preview, false).dump()};
}




http::HttpResponse CrawlerController::handleStopMiguBatch(http::HttpRequest req)
{
    auto request = dto::BatchControlRequestDto::fromRequest(req);
    if (!request) {
        return jsonError(400, "invalid batch control request");
    }
    if (auto error = request->validate()) {
        return jsonError(400, *error);
    }

    const bool cover_batch = cover_service_ && cover_service_->progress(request->batch_id).has_value();
    if (!cover_batch && !service_->getBatchProgress(request->batch_id)) {
        return jsonError(404, "batch_id not found");
    }

    const bool cancelled = cover_batch ? cover_service_->cancel(request->batch_id) : service_->cancelBatch(request->batch_id);
    if (!cancelled) {
        return jsonError(409, "batch cannot be cancelled");
    }

    json body;
    body["batch_id"] = request->batch_id;
    body["message"] = "migu task stop requested";
    return http::HttpResponse {200, {{"Content-Type", "application/json"}}, body.dump()};
}

http::HttpResponse CrawlerController::handleGetMiguBatchProgress(http::HttpRequest req)
{
    std::string batch_id;
    if (req.query_params.count("batch_id")) {
        batch_id = req.query_params.at("batch_id");
    }

    if (batch_id.empty()) {
        return jsonError(400, "batch_id is required");
    }

    auto progress_opt = cover_service_ ? cover_service_->progress(batch_id) : std::nullopt;
    if (!progress_opt) progress_opt = service_->getBatchProgress(batch_id);
    if (!progress_opt) {
        return jsonError(404, "batch_id not found");
    }

    return http::HttpResponse {200, {{"Content-Type", "application/json"}}, progressToJson(*progress_opt).dump()};
}

http::HttpResponse CrawlerController::handleListMiguTasks(http::HttpRequest req)
{
    if (auto error = validatePageQuery(req)) {
        return *error;
    }

    application::model::CrawlBatchQuery query;
    query.page = intQuery(req, "page", 1);
    query.page_size = intQuery(req, "page_size", 20);
    query.state = stringQuery(req, "state");
    query.task_type = stringQuery(req, "type");
    query.keyword = stringQuery(req, "q");
    query.display_name = stringQuery(req, "display_name");
    query.target_summary = stringQuery(req, "target_summary");
    query.target_keywords = stringQuery(req, "target_keywords");
    query.last_error = stringQuery(req, "last_error");
    query.error_category = stringQuery(req, "error_category");
    query.result_code = stringQuery(req, "result_code");
    query.include_archived = boolQuery(req, "include_archived", false);
    auto page = service_->listBatches(std::move(query));

    json body;
    body["items"] = json::array();
    for (const auto &batch : page.items) {
        body["items"].push_back(batchToJson(batch));
    }
    body["page"] = page.page;
    body["page_size"] = page.page_size;
    body["total"] = page.total;
    return http::HttpResponse {200, {{"Content-Type", "application/json"}}, body.dump()};
}

http::HttpResponse CrawlerController::handleGetMiguTask(http::HttpRequest req)
{
    const std::string batch_id = pathValue(req.path, "/api/migu/tasks/");
    if (batch_id.empty() || batch_id.find('/') != std::string::npos) {
        return jsonError(400, "batch_id is required");
    }

    auto batch = service_->findBatch(batch_id);
    if (!batch) {
        return jsonError(404, "batch_id not found");
    }
    return http::HttpResponse {200, {{"Content-Type", "application/json"}}, batchToJson(*batch).dump()};
}

http::HttpResponse CrawlerController::handleListMiguTaskItems(http::HttpRequest req)
{
    if (auto error = validatePageQuery(req)) {
        return *error;
    }

    const std::string batch_id = pathValue(req.path, "/api/migu/tasks/", "/items");
    if (batch_id.empty()) {
        return jsonError(400, "batch_id is required");
    }

    application::model::CrawlBatchItemQuery query;
    query.page = intQuery(req, "page", 1);
    query.page_size = intQuery(req, "page_size", 50);
    query.state = stringQuery(req, "state");
    query.error_category = stringQuery(req, "error_category");
    query.result_code = stringQuery(req, "result_code");
    auto page = service_->listBatchItems(batch_id, std::move(query));

    json body;
    body["items"] = json::array();
    for (const auto &item : page.items) {
        body["items"].push_back(itemToJson(item));
    }
    body["page"] = page.page;
    body["page_size"] = page.page_size;
    body["total"] = page.total;
    return http::HttpResponse {200, {{"Content-Type", "application/json"}}, body.dump()};
}

http::HttpResponse CrawlerController::handleListMiguTaskEvents(http::HttpRequest req)
{
    if (auto error = validatePageQuery(req)) {
        return *error;
    }

    const std::string batch_id = pathValue(req.path, "/api/migu/tasks/", "/events");
    if (batch_id.empty()) {
        return jsonError(400, "batch_id is required");
    }

    application::model::CrawlBatchEventQuery query;
    query.page = intQuery(req, "page", 1);
    query.page_size = intQuery(req, "page_size", 100);
    auto page = service_->listBatchEvents(batch_id, std::move(query));

    json body;
    body["items"] = json::array();
    for (const auto &event : page.items) {
        body["items"].push_back(eventToJson(event));
    }
    body["page"] = page.page;
    body["page_size"] = page.page_size;
    body["total"] = page.total;
    return http::HttpResponse {200, {{"Content-Type", "application/json"}}, body.dump()};
}

http::HttpResponse CrawlerController::handleGetMiguTaskSummary(http::HttpRequest req)
{
    const int stale_after_seconds = intQuery(req, "stale_after_seconds", 3600);
    if (stale_after_seconds <= 0) {
        return jsonError(400, "stale_after_seconds must be positive");
    }
    return http::HttpResponse {200, {{"Content-Type", "application/json"}}, taskSummaryToJson(service_->batchSummary(stale_after_seconds)).dump()};
}

http::HttpResponse CrawlerController::handleRerunFailedMiguTask(http::HttpRequest req)
{
    const std::string batch_id = pathValue(req.path, "/api/migu/tasks/", "/rerun-failed");
    auto request = batchGovernanceRequestFromBody(req);
    if (batch_id.empty() || !request) {
        return jsonError(400, "invalid batch rerun request");
    }
    auto result = service_->rerunFailedBatch(batch_id, std::move(*request), req.body.empty() ? "{}" : req.body);
    if (result.batch_id.empty() && result.claimed_count == 0) {
        return jsonError(404, "no failed batch items can be rerun");
    }
    return http::HttpResponse {202, {{"Content-Type", "application/json"}}, batchGovernanceResultToJson(result).dump()};
}

http::HttpResponse CrawlerController::handleCloneMiguTask(http::HttpRequest req)
{
    const std::string batch_id = pathValue(req.path, "/api/migu/tasks/", "/clone");
    auto request = batchGovernanceRequestFromBody(req);
    if (batch_id.empty() || !request) {
        return jsonError(400, "invalid batch clone request");
    }
    if (request->mode != "all" && request->mode != "failed") {
        return jsonError(400, "mode must be all or failed");
    }
    auto result = service_->cloneBatch(batch_id, std::move(*request), req.body.empty() ? "{}" : req.body);
    if (result.batch_id.empty() && result.claimed_count == 0) {
        return jsonError(404, "no batch items can be cloned");
    }
    return http::HttpResponse {202, {{"Content-Type", "application/json"}}, batchGovernanceResultToJson(result).dump()};
}

http::HttpResponse CrawlerController::handleBackfillRetryMiguTask(http::HttpRequest req)
{
    const std::string batch_id = pathValue(req.path, "/api/migu/tasks/", "/backfill-retry");
    auto request = batchGovernanceRequestFromBody(req);
    if (batch_id.empty() || !request) {
        return jsonError(400, "invalid batch backfill retry request");
    }
    auto result = service_->backfillRetryFromBatch(batch_id, std::move(*request), req.body.empty() ? "{}" : req.body);
    if (result.batch_id.empty() && result.claimed_count == 0) {
        return jsonError(404, "no supported batch items can be retried through backfill");
    }
    return http::HttpResponse {202, {{"Content-Type", "application/json"}}, batchGovernanceResultToJson(result).dump()};
}

http::HttpResponse CrawlerController::handleArchiveMiguTask(http::HttpRequest req)
{
    const std::string batch_id = pathValue(req.path, "/api/migu/tasks/", "/archive");
    auto request = batchGovernanceRequestFromBody(req);
    if (batch_id.empty() || !request) {
        return jsonError(400, "invalid batch archive request");
    }
    const bool archived = service_->archiveBatch(batch_id, request->reason);
    if (!archived) {
        return jsonError(404, "batch_id not found");
    }
    return http::HttpResponse {200, {{"Content-Type", "application/json"}}, json({{"batch_id", batch_id}, {"archived", true}}).dump()};
}

http::HttpResponse CrawlerController::handleGetMiguOverview(http::HttpRequest)
{
    const auto overview = service_->overview();
    json body;
    body["singers"] = overview.singers;
    body["singer_sources"] = overview.singer_sources;
    body["songs"] = overview.songs;
    body["song_sources"] = overview.song_sources;
    body["albums"] = overview.albums;
    body["album_sources"] = overview.album_sources;
    body["singer_song_relations"] = overview.singer_song_relations;
    body["singer_album_relations"] = overview.singer_album_relations;
    body["album_song_relations"] = overview.album_song_relations;
    body["singer_song_sync"] = distributionToJson(overview.singer_song_sync);
    body["singer_album_sync"] = distributionToJson(overview.singer_album_sync);
    body["album_detail_sync"] = distributionToJson(overview.album_detail_sync);
    body["album_song_sync"] = distributionToJson(overview.album_song_sync);
    body["resource_status"] = distributionToJson(overview.resource_status);
    return http::HttpResponse {200, {{"Content-Type", "application/json"}}, body.dump()};
}

http::HttpResponse CrawlerController::handleListMiguSingers(http::HttpRequest req)
{
    if (auto error = validatePageQuery(req)) {
        return *error;
    }

    auto page = service_->listMiguSingers(singerCatalogQueryFromRequest(req));
    auto body = pageToJson(page, singerToJson);
    if (!dto::attachMetadataCovers(body, application::model::CoverOwner::Singer, cover_service_)) return jsonError(503, "metadata cover query failed");
    return http::HttpResponse {200, {{"Content-Type", "application/json"}}, body.dump()};
}

http::HttpResponse CrawlerController::handleGetMiguSinger(http::HttpRequest req)
{
    const std::string source_singer_id = pathValue(req.path, "/api/migu/singers/");
    if (source_singer_id.empty() || source_singer_id.find('/') != std::string::npos || source_singer_id == "crawl") {
        return jsonError(400, "source_singer_id is required");
    }
    auto singer = service_->findMiguSinger(source_singer_id);
    if (!singer) {
        return jsonError(404, "singer not found");
    }
    auto body = singerToJson(*singer);
    if (!dto::attachMetadataCovers(body, application::model::CoverOwner::Singer, cover_service_)) return jsonError(503, "metadata cover query failed");
    return http::HttpResponse {200, {{"Content-Type", "application/json"}}, body.dump()};
}

http::HttpResponse CrawlerController::handleListMiguSingerSongs(http::HttpRequest req)
{
    if (auto error = validatePageQuery(req)) {
        return *error;
    }

    const std::string source_singer_id = pathValue(req.path, "/api/migu/singers/", "/songs");
    if (source_singer_id.empty()) {
        return jsonError(400, "source_singer_id is required");
    }
    auto page = service_->listMiguSingerSongs(source_singer_id, songCatalogQueryFromRequest(req));
    return http::HttpResponse {200, {{"Content-Type", "application/json"}}, pageToJson(page, songToJson).dump()};
}

http::HttpResponse CrawlerController::handleListMiguSingerAlbums(http::HttpRequest req)
{
    if (auto error = validatePageQuery(req)) {
        return *error;
    }

    const std::string source_singer_id = pathValue(req.path, "/api/migu/singers/", "/albums");
    if (source_singer_id.empty()) {
        return jsonError(400, "source_singer_id is required");
    }
    auto page = service_->listMiguSingerAlbums(source_singer_id, albumCatalogQueryFromRequest(req));
    auto body = pageToJson(page, albumToJson);
    if (!dto::attachMetadataCovers(body, application::model::CoverOwner::Album, cover_service_)) return jsonError(503, "metadata cover query failed");
    return http::HttpResponse {200, {{"Content-Type", "application/json"}}, body.dump()};
}

http::HttpResponse CrawlerController::handleListMiguAlbums(http::HttpRequest req)
{
    if (auto error = validatePageQuery(req)) {
        return *error;
    }

    auto page = service_->listMiguAlbums(albumCatalogQueryFromRequest(req));
    auto body = pageToJson(page, albumToJson);
    if (!dto::attachMetadataCovers(body, application::model::CoverOwner::Album, cover_service_)) return jsonError(503, "metadata cover query failed");
    return http::HttpResponse {200, {{"Content-Type", "application/json"}}, body.dump()};
}

http::HttpResponse CrawlerController::handleGetMiguAlbum(http::HttpRequest req)
{
    const std::string source_album_id = pathValue(req.path, "/api/migu/albums/");
    if (source_album_id.empty() || source_album_id.find('/') != std::string::npos) {
        return jsonError(400, "source_album_id is required");
    }
    auto album = service_->findMiguAlbum(source_album_id);
    if (!album) {
        return jsonError(404, "album not found");
    }
    auto body = albumToJson(*album);
    if (!dto::attachMetadataCovers(body, application::model::CoverOwner::Album, cover_service_)) return jsonError(503, "metadata cover query failed");
    return http::HttpResponse {200, {{"Content-Type", "application/json"}}, body.dump()};
}

http::HttpResponse CrawlerController::handleListMiguAlbumSongs(http::HttpRequest req)
{
    if (auto error = validatePageQuery(req)) {
        return *error;
    }

    const std::string source_album_id = pathValue(req.path, "/api/migu/albums/", "/songs");
    if (source_album_id.empty()) {
        return jsonError(400, "source_album_id is required");
    }
    auto page = service_->listMiguAlbumSongs(source_album_id, songCatalogQueryFromRequest(req));
    return http::HttpResponse {200, {{"Content-Type", "application/json"}}, pageToJson(page, songToJson).dump()};
}

http::HttpResponse CrawlerController::handleListMiguSongs(http::HttpRequest req)
{
    if (auto error = validatePageQuery(req)) {
        return *error;
    }

    auto page = service_->listMiguSongs(songCatalogQueryFromRequest(req));
    return http::HttpResponse {200, {{"Content-Type", "application/json"}}, pageToJson(page, songToJson).dump()};
}

http::HttpResponse CrawlerController::handleGetMiguSong(http::HttpRequest req)
{
    const std::string source_song_id = pathValue(req.path, "/api/migu/songs/");
    if (source_song_id.empty() || source_song_id.find('/') != std::string::npos) {
        return jsonError(400, "source_song_id is required");
    }
    auto song = service_->findMiguSong(source_song_id);
    if (!song) {
        return jsonError(404, "song not found");
    }
    return http::HttpResponse {200, {{"Content-Type", "application/json"}}, songToJson(*song).dump()};
}

http::HttpResponse CrawlerController::handleListMiguResources(http::HttpRequest req)
{
    if (auto error = validatePageQuery(req)) {
        return *error;
    }

    auto page = service_->listMiguResources(resourceQueueQueryFromRequest(req));
    return http::HttpResponse {200, {{"Content-Type", "application/json"}}, pageToJson(page, resourceToJson).dump()};
}

http::HttpResponse CrawlerController::handlePreviewMiguResources(http::HttpRequest req)
{
    auto query = resourceActionQueryFromBody(req);
    if (!query || query->action.empty()) {
        return jsonError(400, "invalid migu resource preview request");
    }
    if (query->source != "migu") {
        return jsonError(400, "source must be migu");
    }
    if (query->action != "retry" && query->action != "reset" && query->action != "recover-stale" && query->action != "process") {
        return jsonError(400, "action must be retry, reset, recover-stale or process");
    }
    if (query->action == "recover-stale" && query->stale_after_seconds <= 0) {
        return jsonError(400, "stale_after_seconds must be positive");
    }
    const std::string action = query->action;
    const auto preview = service_->previewMiguResources(std::move(*query));
    auto body = resourcePreviewToJson(preview);
    body["action"] = action;
    body["source"] = "migu";
    if (action == "process") {
        addProcessPreviewAliases(body, preview);
    }
    return http::HttpResponse {200, {{"Content-Type", "application/json"}}, body.dump()};
}

http::HttpResponse CrawlerController::handleRetryMiguResources(http::HttpRequest req)
{
    auto query = resourceActionQueryFromBody(req);
    if (!query) {
        return jsonError(400, "invalid migu resource retry request");
    }
    if (query->source != "migu") {
        return jsonError(400, "source must be migu");
    }
    if (!hasResourceSelector(*query)) {
        return jsonError(400, "resource retry requires resource_ids or at least one filter");
    }

    const auto result = service_->retryMiguResources(std::move(*query));
    return http::HttpResponse {200, {{"Content-Type", "application/json"}}, resourceActionResultToJson("retry", result).dump()};
}

http::HttpResponse CrawlerController::handleResetMiguResources(http::HttpRequest req)
{
    auto query = resourceActionQueryFromBody(req);
    if (!query) {
        return jsonError(400, "invalid migu resource reset request");
    }
    if (query->source != "migu") {
        return jsonError(400, "source must be migu");
    }
    if (!hasResourceSelector(*query)) {
        return jsonError(400, "resource reset requires resource_ids or at least one filter");
    }

    const auto result = service_->resetMiguResources(std::move(*query));
    return http::HttpResponse {200, {{"Content-Type", "application/json"}}, resourceActionResultToJson("reset", result).dump()};
}

http::HttpResponse CrawlerController::handleRecoverStaleMiguResources(http::HttpRequest req)
{
    auto query = resourceActionQueryFromBody(req);
    if (!query || query->stale_after_seconds <= 0) {
        return jsonError(400, "invalid migu resource recovery request");
    }
    if (query->source != "migu") {
        return jsonError(400, "source must be migu");
    }

    const auto result = service_->recoverStaleMiguResources(std::move(*query));
    return http::HttpResponse {200, {{"Content-Type", "application/json"}}, resourceActionResultToJson("recover-stale", result).dump()};
}

http::HttpResponse CrawlerController::handleProcessMiguResources(http::HttpRequest req)
{
    auto query = resourceProcessQueryFromBody(req);
    if (!query) {
        return jsonError(400, "invalid migu resource process request");
    }
    if (query->source != "migu") {
        return jsonError(400, "source must be migu");
    }
    if (query->limit == 0 || query->limit > 1000) {
        return jsonError(400, "limit must be between 1 and 1000");
    }
    if (auto error = validateResourceProcessOptions(query->option_overrides)) {
        return jsonError(400, *error);
    }

    std::optional<application::model::MiguResourcePreviewResult> selected_preview;
    if (!query->resource_ids.empty()) {
        selected_preview = service_->previewMiguResources(processPreviewQueryFromClaim(*query));
    }

    if (resourceProcessAsyncRequested(req)) {
        const std::string batch_id = service_->startMiguResourcesProcessAsync(std::move(*query));
        if (batch_id.empty()) {
            return jsonError(503, "migu async resource workflow is not configured");
        }
        auto body = resourceProcessAcceptedToJson(batch_id);
        if (selected_preview) {
            auto preview_body = resourcePreviewToJson(*selected_preview);
            preview_body["action"] = "process";
            preview_body["source"] = "migu";
            addProcessPreviewAliases(preview_body, *selected_preview);
            body["preview"] = std::move(preview_body);
        }
        return http::HttpResponse {202, {{"Content-Type", "application/json"}}, body.dump()};
    }

    const auto result = service_->processMiguResources(std::move(*query));
    if (result.batch_id.empty()) {
        return jsonError(503, "migu resource workflow is not configured");
    }
    auto body = resourceProcessResultToJson(result);
    if (selected_preview) {
        auto preview_body = resourcePreviewToJson(*selected_preview);
        preview_body["action"] = "process";
        preview_body["source"] = "migu";
        addProcessPreviewAliases(preview_body, *selected_preview);
        body["preview"] = std::move(preview_body);
    }
    return http::HttpResponse {200, {{"Content-Type", "application/json"}}, body.dump()};
}

http::HttpResponse CrawlerController::handleListMiguBackfillQueue(http::HttpRequest req)
{
    if (auto error = validatePageQuery(req)) {
        return *error;
    }

    const std::string type = backfillTypeFromPath(req.path, "/queue");
    if (type.empty()) {
        return jsonError(400, "backfill type is required");
    }
    if (!isBackfillTypeSupported(type)) {
        return jsonError(404, "backfill type not found");
    }

    if (type == "singer-songs" || type == "singer-albums") {
        auto query = singerCatalogQueryFromRequest(req);
        if (type == "singer-songs") {
            query.songs_sync_statuses = defaultBackfillStatuses(statusQuery(req, {"status", "sync_status"}));
        } else {
            query.albums_sync_statuses = defaultBackfillStatuses(statusQuery(req, {"status", "sync_status"}));
        }
        auto page = service_->listMiguSingers(std::move(query));
        return http::HttpResponse {200, {{"Content-Type", "application/json"}}, backfillQueuePageToJson(page, type).dump()};
    }

    if (type == "album-details" || type == "album-songs") {
        auto query = albumCatalogQueryFromRequest(req);
        query.exclude_shared_release_archives = true;
        if (type == "album-details") {
            query.detail_sync_statuses = defaultBackfillStatuses(statusQuery(req, {"status", "sync_status"}));
        } else {
            query.detail_sync_statuses = {2};
            query.songs_sync_statuses = defaultBackfillStatuses(statusQuery(req, {"status", "sync_status"}));
        }
        auto page = service_->listMiguAlbums(std::move(query));
        return http::HttpResponse {200, {{"Content-Type", "application/json"}}, backfillQueuePageToJson(page, type).dump()};
    }

    return jsonError(404, "backfill type not found");
}

http::HttpResponse CrawlerController::handlePreviewMiguBackfill(http::HttpRequest req)
{
    const std::string type = backfillTypeFromPath(req.path, "/preview");
    auto query = backfillQueryFromBody(req);
    if (type.empty() || !query) {
        return jsonError(400, "invalid backfill preview request");
    }
    if (!isBackfillTypeSupported(type)) {
        return jsonError(404, "backfill type not found");
    }
    if (query->source != "migu") {
        return jsonError(400, "source must be migu");
    }

    const int sample_size = (std::min)((std::max)(1, query->limit), 20);
    json body;
    body["type"] = type;
    body["source"] = "migu";
    body["items"] = json::array();
    body["sample_size"] = sample_size;
    std::uint64_t matched_count = 0;
    std::uint64_t eligible_count = 0;
    std::uint64_t missing_count = 0;

    if (type == "singer-songs" || type == "singer-albums") {
        if (!query->source_ids.empty()) {
            for (const auto &source_id : query->source_ids) {
                auto singer = service_->findMiguSinger(source_id);
                if (!singer) {
                    ++missing_count;
                    if (body["items"].size() < static_cast<std::size_t>(sample_size)) {
                        body["items"].push_back({{"target_id", source_id}, {"target_type", "singer"}, {"target_name", ""}, {"owner_name", ""}, {"sync_status", nullptr}, {"sync_count", 0}, {"last_error", ""}, {"last_batch_id", nullptr}, {"running_batch_id", nullptr}, {"last_processed_at", nullptr}, {"eligible_actions", json::array()}, {"eligible", false}, {"reason", "not_found"}});
                    }
                    continue;
                }
                ++matched_count;
                const auto item = withPreviewEligibility(singerQueueTargetToJson(*singer, type), query->statuses, query->force);
                if (item.value("eligible", false)) {
                    ++eligible_count;
                }
                if (body["items"].size() < static_cast<std::size_t>(sample_size)) {
                    body["items"].push_back(item);
                }
            }
        } else {
            application::model::MiguSingerCatalogQuery base;
            base.page = 1;
            base.page_size = sample_size;
            base.keyword = query->keyword;
            base.country = query->country;
            base.gender = query->gender;
            auto matched = service_->listMiguSingers(base);
            matched_count = matched.total;
            for (const auto &singer : matched.items) {
                body["items"].push_back(withPreviewEligibility(singerQueueTargetToJson(singer, type), query->statuses, query->force));
            }

            application::model::MiguSingerCatalogQuery eligible_query = base;
            eligible_query.page_size = 1;
            if (type == "singer-songs") {
                eligible_query.songs_sync_statuses = previewRetryStatuses(query->statuses, query->force);
            } else {
                eligible_query.albums_sync_statuses = previewRetryStatuses(query->statuses, query->force);
            }
            eligible_count = service_->listMiguSingers(std::move(eligible_query)).total;
        }
    } else {
        if (!query->source_ids.empty()) {
            for (const auto &source_id : query->source_ids) {
                auto album = service_->findMiguAlbum(source_id);
                if (!album) {
                    ++missing_count;
                    if (body["items"].size() < static_cast<std::size_t>(sample_size)) {
                        body["items"].push_back({{"target_id", source_id}, {"target_type", "album"}, {"target_name", ""}, {"owner_name", ""}, {"sync_status", nullptr}, {"sync_count", 0}, {"last_error", ""}, {"last_batch_id", nullptr}, {"running_batch_id", nullptr}, {"last_processed_at", nullptr}, {"eligible_actions", json::array()}, {"eligible", false}, {"reason", "not_found"}});
                    }
                    continue;
                }
                ++matched_count;
                const auto item = withPreviewEligibility(albumQueueTargetToJson(*album, type), query->statuses, query->force);
                if (item.value("eligible", false)) {
                    ++eligible_count;
                }
                if (body["items"].size() < static_cast<std::size_t>(sample_size)) {
                    body["items"].push_back(item);
                }
            }
        } else {
            application::model::MiguAlbumCatalogQuery base;
            base.page = 1;
            base.page_size = sample_size;
            base.keyword = query->keyword;
            base.artist = query->artist;
            base.album_kind = query->album_kind;
            auto matched = service_->listMiguAlbums(base);
            matched_count = matched.total;
            for (const auto &album : matched.items) {
                body["items"].push_back(withPreviewEligibility(albumQueueTargetToJson(album, type), query->statuses, query->force));
            }

            application::model::MiguAlbumCatalogQuery eligible_query = base;
            eligible_query.exclude_shared_release_archives = true;
            eligible_query.page_size = 1;
            if (type == "album-details") {
                eligible_query.detail_sync_statuses = previewRetryStatuses(query->statuses, query->force);
            } else {
                eligible_query.detail_sync_statuses = {2};
                eligible_query.songs_sync_statuses = previewRetryStatuses(query->statuses, query->force);
            }
            eligible_count = service_->listMiguAlbums(std::move(eligible_query)).total;
        }
    }

    body["matched_count"] = matched_count;
    body["eligible_count"] = eligible_count;
    body["ineligible_count"] = matched_count > eligible_count ? matched_count - eligible_count : 0;
    body["missing_count"] = missing_count;
    return http::HttpResponse {200, {{"Content-Type", "application/json"}}, body.dump()};
}

http::HttpResponse CrawlerController::handleRetryMiguBackfill(http::HttpRequest req)
{
    const std::string type = backfillTypeFromPath(req.path, "/retry");
    auto query = backfillQueryFromBody(req);
    if (type.empty() || !query) {
        return jsonError(400, "invalid backfill retry request");
    }
    if (query->source != "migu") {
        return jsonError(400, "source must be migu");
    }

    const int limit_per_target = limitPerTargetFromBody(req);
    const auto retry = service_->retryMiguBackfill(type, std::move(*query), limit_per_target, req.body.empty() ? "{}" : req.body);
    if (retry.batch_id.empty()) {
        return jsonError(404, "backfill type not found or queue is not configured");
    }
    const auto target_count = batchTargetCount(service_, retry.batch_id);
    return taskAcceptedResponse(retry.batch_id, "migu backfill retry started", target_count, limit_per_target, 0, static_cast<int>(target_count), retry.claimed_count, retry.skipped_count, actionItemsToJson(retry.items));
}

http::HttpResponse CrawlerController::handleResetMiguBackfill(http::HttpRequest req)
{
    const std::string type = backfillTypeFromPath(req.path, "/reset");
    auto query = backfillQueryFromBody(req);
    if (type.empty() || !query) {
        return jsonError(400, "invalid backfill reset request");
    }
    if (query->force) return jsonError(400, "force is supported by retry and preview only");
    if (query->source != "migu") {
        return jsonError(400, "source must be migu");
    }
    if (!hasResetSelector(*query)) {
        return jsonError(400, "reset requires source_ids or at least one filter");
    }

    json items = json::array();
    if (!query->source_ids.empty()) {
        for (const auto &source_id : query->source_ids) {
            if (type == "singer-songs" || type == "singer-albums") {
                auto singer = service_->findMiguSinger(source_id);
                if (!singer) {
                    items.push_back({{"source_id", source_id}, {"name", ""}, {"status", nullptr}, {"reason", "not_found"}});
                    continue;
                }
                const auto target = singerQueueTargetToJson(*singer, type);
                items.push_back({{"source_id", source_id}, {"name", target.value("target_name", "")}, {"status", target["sync_status"]}, {"reason", target.value("eligible", false) ? "reset" : target.value("reason", "skipped")}});
            } else {
                auto album = service_->findMiguAlbum(source_id);
                if (!album) {
                    items.push_back({{"source_id", source_id}, {"name", ""}, {"status", nullptr}, {"reason", "not_found"}});
                    continue;
                }
                const auto target = albumQueueTargetToJson(*album, type);
                items.push_back({{"source_id", source_id}, {"name", target.value("target_name", "")}, {"status", target["sync_status"]}, {"reason", target.value("eligible", false) ? "reset" : target.value("reason", "skipped")}});
            }
        }
    } else {
        const int sample_size = (std::min)((std::max)(1, query->limit), 20);
        if (type == "singer-songs" || type == "singer-albums") {
            application::model::MiguSingerCatalogQuery preview;
            preview.page = 1;
            preview.page_size = sample_size;
            preview.keyword = query->keyword;
            preview.country = query->country;
            preview.gender = query->gender;
            if (type == "singer-songs") {
                preview.songs_sync_statuses = defaultBackfillStatuses(query->statuses);
            } else {
                preview.albums_sync_statuses = defaultBackfillStatuses(query->statuses);
            }
            const auto page = service_->listMiguSingers(std::move(preview));
            for (const auto &singer : page.items) {
                const auto target = singerQueueTargetToJson(singer, type);
                items.push_back({{"source_id", target["target_id"]}, {"name", target.value("target_name", "")}, {"status", target["sync_status"]}, {"reason", "reset"}});
            }
        } else {
            application::model::MiguAlbumCatalogQuery preview;
            preview.page = 1;
            preview.page_size = sample_size;
            preview.keyword = query->keyword;
            preview.artist = query->artist;
            preview.album_kind = query->album_kind;
            preview.exclude_shared_release_archives = true;
            if (type == "album-details") {
                preview.detail_sync_statuses = defaultBackfillStatuses(query->statuses);
            } else {
                preview.songs_sync_statuses = defaultBackfillStatuses(query->statuses);
            }
            const auto page = service_->listMiguAlbums(std::move(preview));
            for (const auto &album : page.items) {
                const auto target = albumQueueTargetToJson(album, type);
                items.push_back({{"source_id", target["target_id"]}, {"name", target.value("target_name", "")}, {"status", target["sync_status"]}, {"reason", "reset"}});
            }
        }
    }

    const int requested_reset_count = static_cast<int>(query->source_ids.size());
    const int updated_count = service_->resetMiguBackfill(type, std::move(*query));
    json body;
    body["updated_count"] = updated_count;
    body["success_count"] = updated_count;
    body["failed_count"] = 0;
    body["skipped_count"] = requested_reset_count == 0 ? 0 : (std::max)(0, requested_reset_count - updated_count);
    body["items"] = std::move(items);
    body["type"] = type;
    return http::HttpResponse {200, {{"Content-Type", "application/json"}}, body.dump()};
}

http::HttpResponse CrawlerController::handleGetMiguIntegrity(http::HttpRequest req)
{
    const int stale_after_seconds = intQuery(req, "stale_after_seconds", 3600);
    const auto summary = service_->miguIntegrity(stale_after_seconds);
    json body;
    body["singers_without_songs"] = summary.singers_without_songs;
    body["singers_without_albums"] = summary.singers_without_albums;
    body["albums_without_details"] = summary.albums_without_details;
    body["albums_without_songs"] = summary.albums_without_songs;
    body["songs_without_resource_tasks"] = summary.songs_without_resource_tasks;
    body["resources_failed"] = summary.resources_failed;
    body["resources_running_stale"] = summary.resources_running_stale;
    body["singers_songs_running_stale"] = summary.singers_songs_running_stale;
    body["singers_albums_running_stale"] = summary.singers_albums_running_stale;
    body["albums_details_running_stale"] = summary.albums_details_running_stale;
    body["albums_songs_running_stale"] = summary.albums_songs_running_stale;
    body["running_batches_stale"] = summary.running_batches_stale;
    body["generated_at"] = summary.generated_at;
    return http::HttpResponse {200, {{"Content-Type", "application/json"}}, body.dump()};
}

http::HttpResponse CrawlerController::handleGetMiguSchemaStatus(http::HttpRequest)
{
    const auto status = service_->miguSchemaStatus();
    json body;
    body["source"] = "migu";
    body["schema_version"] = status.schema_version;
    body["latest_applied_version"] = status.latest_applied_version;
    body["latest_available_version"] = status.latest_available_version;
    body["schema_name"] = status.schema_name;
    body["ok"] = status.ok;
    body["pending_migrations"] = status.pending_migrations;
    body["checksum_mismatches"] = status.checksum_mismatches;
    body["missing_tables"] = status.missing_tables;
    body["missing_columns"] = status.missing_columns;
    body["checked_at"] = status.checked_at;
    return http::HttpResponse {200, {{"Content-Type", "application/json"}}, body.dump()};
}

http::HttpResponse CrawlerController::handleRecoverMiguStale(http::HttpRequest req)
{
    auto request = staleRecoveryRequestFromBody(req);
    if (!request || request->stale_after_seconds <= 0) {
        return jsonError(400, "invalid migu stale recovery request");
    }
    if (request->source != "migu") {
        return jsonError(400, "source must be migu");
    }

    const auto result = service_->recoverMiguStale(std::move(*request));
    json body;
    body["source"] = "migu";
    body["singer_songs_recovered"] = result.singer_songs_recovered;
    body["singer_albums_recovered"] = result.singer_albums_recovered;
    body["album_details_recovered"] = result.album_details_recovered;
    body["album_songs_recovered"] = result.album_songs_recovered;
    body["resources_recovered"] = result.resources_recovered;
    body["batches_recovered"] = result.batches_recovered;
    body["recovered_at"] = result.recovered_at;
    return http::HttpResponse {200, {{"Content-Type", "application/json"}}, body.dump()};
}

http::HttpResponse CrawlerController::handleListMiguGovernanceEvents(http::HttpRequest req)
{
    if (auto error = validatePageQuery(req)) {
        return *error;
    }
    auto page = service_->listMiguGovernanceEvents(governanceEventQueryFromRequest(req));
    return http::HttpResponse {200, {{"Content-Type", "application/json"}}, pageToJson(page, governanceEventToJson).dump()};
}

http::HttpResponse CrawlerController::handleGetMiguDictionaries(http::HttpRequest)
{
    json body;
    body["sync_status"] = json::array({
        {{"value", 0}, {"code", "pending"}, {"label", "未同步"}},
        {{"value", 1}, {"code", "running"}, {"label", "同步中"}},
        {{"value", 2}, {"code", "completed"}, {"label", "已完成"}},
        {{"value", 3}, {"code", "failed"}, {"label", "失败"}},
        {{"value", 4}, {"code", "partial"}, {"label", "部分完成"}},
    });
    body["gender"] = json::array({
        {{"value", 0}, {"code", "unknown"}, {"label", "未知"}},
        {{"value", 1}, {"code", "male"}, {"label", "男"}},
        {{"value", 2}, {"code", "female"}, {"label", "女"}},
        {{"value", 3}, {"code", "group"}, {"label", "组合"}},
    });
    body["album_kind"] = json::array({
        {{"value", 0}, {"code", "unknown"}, {"label", "未知"}},
        {{"value", 1}, {"code", "album"}, {"label", "普通专辑"}},
        {{"value", 2}, {"code", "digital_album"}, {"label", "数字专辑"}},
    });
    body["resource_kind"] = json::array({"audio", "cover", "lyric", "mrc", "trc"});
    body["audio_format"] = json::array({"PQ", "HQ", "SQ", "ZQ24", "Z3D"});
    body["error_category"] = json::array({"network", "parse", "persistence", "copyright", "resource", "invalid_target", "timeout", "unknown"});
    body["result_code"] = json::array({"ok", "network_failed", "parse_failed", "persist_failed", "invalid_target", "timeout", "partial_completed", "failed"});
    return http::HttpResponse {200, {{"Content-Type", "application/json"}}, body.dump()};
}

http::HttpResponse CrawlerController::handleGetMiguSettings(http::HttpRequest)
{
    return http::HttpResponse {200, {{"Content-Type", "application/json"}}, settingsToJson(service_->getRuntimeSettings(), readonly_settings_).dump()};
}

http::HttpResponse CrawlerController::handleUpdateMiguSettings(http::HttpRequest req)
{
    auto request = dto::MiguSettingsUpdateDto::fromRequest(req);
    if (!request) {
        return jsonError(400, "invalid migu settings request");
    }
    if (auto error = request->validate()) {
        return jsonError(400, *error);
    }

    if (auto error = service_->updateRuntimeSettings(request->update)) {
        return jsonError(400, *error);
    }

    return http::HttpResponse {200, {{"Content-Type", "application/json"}}, settingsToJson(service_->getRuntimeSettings(), readonly_settings_).dump()};
}

http::HttpResponse CrawlerController::handleRefreshMiguPacmtoken(http::HttpRequest req)
{
    if (!pacmtoken_service_) {
        return jsonError(503, "migu pacmtoken refresher is not configured");
    }

    auto request = dto::MiguPacmtokenRefreshDto::fromRequest(req);
    if (!request) {
        return jsonError(400, "invalid migu pacmtoken refresh request");
    }
    if (auto error = request->validate()) {
        return jsonError(400, *error);
    }

    auto result = pacmtoken_service_->refresh(std::move(request->request));
    if (!result.success) {
        return pacmtokenErrorResponse(result);
    }
    return http::HttpResponse {200, {{"Content-Type", "application/json"}}, pacmtokenRefreshToJson(result).dump()};
}

http::HttpResponse CrawlerController::handleGetMiguPacmtokenState(http::HttpRequest)
{
    if (!pacmtoken_service_) {
        return jsonError(503, "migu pacmtoken service is not configured");
    }
    return http::HttpResponse {200, {{"Content-Type", "application/json"}}, pacmtokenStateToJson(pacmtoken_service_->state(), false).dump()};
}

http::HttpResponse CrawlerController::handleGetCurrentMiguPacmtoken(http::HttpRequest)
{
    if (!pacmtoken_service_) {
        return jsonError(503, "migu pacmtoken service is not configured");
    }
    return http::HttpResponse {200, {{"Content-Type", "application/json"}}, pacmtokenStateToJson(pacmtoken_service_->current(), true).dump()};
}

http::HttpResponse CrawlerController::handleOpenApi(http::HttpRequest)
{
    return http::HttpResponse {200, {{"Content-Type", "application/json"}}, OpenApiDocumentFactory::miguDocumentJson()};
}

}
} // namespace orchestrator::modules::crawler::api::controllers
