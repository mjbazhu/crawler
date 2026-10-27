#pragma once

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
namespace resource_detail {

inline std::string trimMessage(std::string message)
{
    constexpr std::size_t kMaxMessageLength = 512;
    if (message.size() > kMaxMessageLength) {
        message.resize(kMaxMessageLength);
    }
    return message;
}

inline std::string escapeJson(std::string_view value)
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

inline std::string stagePayload(const application::model::SongResourceQueueTarget &target, std::string_view stage, std::string_view extra = {})
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

inline std::string statusesJson(const std::vector<int> &statuses)
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

inline std::string claimPayload(const application::model::SongResourceClaimQuery &query, std::string_view stage, std::string_view extra = {})
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

inline bool isAudioResource(const std::string &resource_kind)
{
    return resource_kind == "audio";
}

inline bool isRelatedResource(const std::string &resource_kind)
{
    return resource_kind == "cover" || resource_kind == "lyric" || resource_kind == "mrc" || resource_kind == "trc";
}

inline bool isKnownDigitalAlbum(const application::model::SongResourceQueueTarget &target)
{
    return target.source_album_kind == 2;
}

inline bool isPermanentResourceSkip(const std::string &code)
{
    return code == "210001" || code == "440000" || code == "440015" || code == "440016";
}

inline std::string permanentResourceSkipCategory(const std::string &code)
{
    if (code == "440000") {
        return "copyright";
    }
    return "resource";
}

inline bool isResourceRestrictionFailure(const std::string &code)
{
    return code == "440013";
}

inline application::model::SongResourceProcessItem makeItem(const application::model::SongResourceQueueTarget &target, bool ok, bool skipped, bool related, bool synthetic, std::string category, std::string code, std::string message)
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

inline application::model::SongResourceProcessItem makeStartedItem(const application::model::SongResourceQueueTarget &target, bool related)
{
    return makeItem(target, false, false, related, false, {}, {}, {});
}

inline application::model::SongResourceProcessItem makeRelatedSkippedItem(const application::model::SongResourceQueueTarget &audio_target, const std::string &kind)
{
    application::model::SongResourceQueueTarget target = audio_target;
    target.resource_id = 0;
    target.resource_kind = kind;
    target.format_type.clear();
    return makeItem(target, false, true, true, true, "resource", "related_not_claimed", "related resource task not found or already processed");
}

inline std::string relatedResourceKey(const application::model::SongResourceQueueTarget &audio_target, const std::string &kind)
{
    return std::to_string(audio_target.song_source_ref_id) + ":" + audio_target.source_song_id + ":" + kind;
}

inline application::model::SongResourceFetchRequest toFetchRequest(const application::model::SongResourceQueueTarget &target)
{
    application::model::SongResourceFetchRequest request;
    request.content_id = target.source_content_id;
    request.resource_type = target.audio_resource_type.empty() ? target.source_resource_type : target.audio_resource_type;
    request.copyright_id = target.copyright_id;
    request.tone_flag = target.resource_fetch_format_type.empty() ? target.format_type : target.resource_fetch_format_type;
    return request;
}

inline std::string relatedResourceUrl(const application::model::SongResourceFetchResult &fetch_result, const std::string &resource_kind)
{
    if (resource_kind == "cover") {
        return fetch_result.cover_url;
    }
    if (resource_kind == "lyric") {
        return fetch_result.lyric_url;
    }
    if (resource_kind == "mrc") {
        return fetch_result.mrc_url;
    }
    if (resource_kind == "trc") {
        return fetch_result.trc_url;
    }
    return {};
}

inline bool validAudioTarget(const application::model::SongResourceQueueTarget &target)
{
    const auto request = toFetchRequest(target);
    return !request.content_id.empty() && !request.resource_type.empty() && !request.copyright_id.empty() && !request.tone_flag.empty();
}

inline application::model::SongResourcePathRequest toPathRequest(const application::model::SongResourceQueueTarget &target, const std::string &remote_url, const std::string &content_type)
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

inline std::uint64_t nowSeconds()
{
    return static_cast<std::uint64_t>(std::time(nullptr));
}

inline bool retryableResourceFailure(const application::model::SongResourceProcessItem &item)
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

inline std::size_t normalizedResourceConcurrency(const application::model::SongResourceProcessOptions &options)
{
    return (std::min<std::size_t>)((std::max<std::size_t>)(1, options.resource_download_concurrency), 32);
}

}
} // namespace orchestrator::modules::crawler::application::use_cases::resource_detail
