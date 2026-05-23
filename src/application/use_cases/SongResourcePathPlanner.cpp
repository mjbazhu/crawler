#include "crawler/application/use_cases/SongResourcePathPlanner.h"

#include <algorithm>
#include <cctype>
#include <initializer_list>
#include <sstream>
#include <utility>

namespace orchestrator::modules::crawler::application::use_cases {

namespace {

// 转为小写 ASCII，用于资源类型和扩展名比较。
std::string toLowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

// 将路径片段规整为安全文件名，避免歌名和外部字段影响路径结构。
std::string sanitizeSegment(std::string value)
{
    std::string sanitized;
    sanitized.reserve(value.size());
    for (unsigned char ch : value) {
        if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.') {
            sanitized.push_back(static_cast<char>(ch));
        } else {
            sanitized.push_back('_');
        }
    }
    while (!sanitized.empty() && sanitized.front() == '.') {
        sanitized.erase(sanitized.begin());
    }
    while (!sanitized.empty() && sanitized.back() == '.') {
        sanitized.pop_back();
    }
    return sanitized;
}

// 返回第一个非空安全片段。
std::string firstSafeSegment(std::initializer_list<std::string> values)
{
    for (const auto &value : values) {
        auto sanitized = sanitizeSegment(value);
        if (!sanitized.empty()) {
            return sanitized;
        }
    }
    return {};
}

// 提取 URL path 里的扩展名，忽略 query 和 fragment。
std::string extensionFromUrl(std::string url)
{
    const auto query_pos = url.find_first_of("?#");
    if (query_pos != std::string::npos) {
        url = url.substr(0, query_pos);
    }
    const auto scheme_pos = url.find("://");
    const auto path_start = scheme_pos == std::string::npos ? url.find('/') : url.find('/', scheme_pos + 3);
    if (path_start == std::string::npos) {
        return {};
    }
    const auto path = url.substr(path_start);
    const auto slash_pos = path.find_last_of('/');
    const auto dot_pos = path.find_last_of('.');
    if (dot_pos == std::string::npos || (slash_pos != std::string::npos && dot_pos < slash_pos)) {
        return {};
    }
    auto extension = sanitizeSegment(toLowerAscii(path.substr(dot_pos + 1)));
    if (extension.empty() || extension.size() > 8) {
        return {};
    }
    return extension;
}

// 根据 Content-Type 推断扩展名。
std::string extensionFromContentType(std::string content_type)
{
    content_type = toLowerAscii(std::move(content_type));
    const auto semicolon_pos = content_type.find(';');
    if (semicolon_pos != std::string::npos) {
        content_type = content_type.substr(0, semicolon_pos);
    }

    if (content_type == "audio/flac" || content_type == "audio/x-flac") return "flac";
    if (content_type == "audio/mpeg" || content_type == "audio/mp3") return "mp3";
    if (content_type == "audio/mp4" || content_type == "audio/x-m4a") return "m4a";
    if (content_type == "audio/aac") return "aac";
    if (content_type == "audio/wav" || content_type == "audio/x-wav") return "wav";
    if (content_type == "image/webp") return "webp";
    if (content_type == "image/png") return "png";
    if (content_type == "image/jpeg" || content_type == "image/jpg") return "jpg";
    if (content_type == "application/json") return "json";
    if (content_type == "text/plain" || content_type == "application/octet-stream") return {};
    return {};
}

// 根据资源类型和音质档位选择兜底扩展名。
std::string fallbackExtension(const std::string &resource_kind, const std::string &format_type)
{
    const auto kind = toLowerAscii(resource_kind);
    const auto format = toLowerAscii(format_type);
    if (kind == "lyric") return "lrc";
    if (kind == "mrc") return "mrc";
    if (kind == "trc") return "trc";
    if (kind == "cover") return "webp";
    if (kind == "audio") {
        if (format == "pq" || format == "hq") return "mp3";
        if (format == "sq" || format == "zq24" || format == "z3d") return "flac";
    }
    return "bin";
}

// 规划资源子目录和文件基础名。
std::pair<std::string, std::string> resourceFolderAndBaseName(const application::model::SongResourcePathRequest &request)
{
    const auto kind = toLowerAscii(request.resource_kind);
    if (kind == "audio") {
        return {"audio", firstSafeSegment({request.format_type, "unknown"})};
    }
    if (kind == "lyric") return {"lyrics", "lyric"};
    if (kind == "mrc") return {"lyrics", "mrc"};
    if (kind == "trc") return {"lyrics", "trc"};
    if (kind == "cover") return {"cover", "cover"};
    return {firstSafeSegment({kind, "resource"}), "resource"};
}

// 从多种来源推断最终扩展名。
std::string chooseExtension(const application::model::SongResourcePathRequest &request)
{
    auto extension = extensionFromUrl(request.remote_url);
    if (!extension.empty()) {
        return extension;
    }
    extension = extensionFromContentType(request.content_type);
    if (!extension.empty()) {
        return extension;
    }
    return fallbackExtension(request.resource_kind, request.format_type);
}

} // namespace

application::model::SongResourcePathResult SongResourcePathPlanner::plan(application::model::SongResourcePathRequest request) const
{
    application::model::SongResourcePathResult result;
    const auto source = firstSafeSegment({request.source, "unknown"});
    const auto identity = firstSafeSegment({
        request.source_content_id,
        request.source_song_id,
        request.song_source_ref_id == 0 ? std::string {} : std::to_string(request.song_source_ref_id),
        request.resource_id == 0 ? std::string {} : std::to_string(request.resource_id),
    });
    if (identity.empty()) {
        result.error = "missing_resource_identity";
        return result;
    }

    auto [folder, base_name] = resourceFolderAndBaseName(request);
    folder = firstSafeSegment({folder, "resource"});
    base_name = firstSafeSegment({base_name, "resource"});
    result.extension = chooseExtension(request);

    std::ostringstream path;
    path << source << "/songs/" << identity << "/" << folder << "/" << base_name << "." << result.extension;
    result.relative_path = path.str();
    result.ok = true;
    return result;
}

} // namespace orchestrator::modules::crawler::application::use_cases
