#include "crawler/infrastructure/storage/filesystem/MetadataCoverStore.h"

#include <algorithm>
#include <chrono>
#include <cstddef>
#include <exception>
#include <optional>
#include <string_view>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::storage::filesystem {
namespace {

// 路径片段不接受分隔符、相对目录或平台保留字符。
bool safeSegment(std::string_view value)
{
    return !value.empty() && std::all_of(value.begin(), value.end(), [](unsigned char c) {
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_';
    });
}

// 按内容签名决定扩展名，拒绝成功状态码下的 HTML 错误页。
std::string imageExtension(const std::vector<char> &bytes)
{
    const std::string_view data(bytes.data(), bytes.size());
    if (data.starts_with("\x89PNG\r\n\x1a\n")) return "png";
    if (data.size() >= 3 && static_cast<unsigned char>(data[0]) == 0xff && static_cast<unsigned char>(data[1]) == 0xd8 && static_cast<unsigned char>(data[2]) == 0xff) return "jpg";
    if (data.starts_with("GIF87a") || data.starts_with("GIF89a")) return "gif";
    if (data.size() >= 12 && data.starts_with("RIFF") && data.substr(8, 4) == "WEBP") return "webp";
    if (data.size() >= 12 && data.substr(4, 4) == "ftyp" && (data.substr(8, 4) == "avif" || data.substr(8, 4) == "avis")) return "avif";
    return {};
}

// 保存脱敏的失败结果，原始远端地址不能进入错误记录。
application::model::MetadataResourceFile failure(std::string category, std::string code, std::string message)
{
    application::model::MetadataResourceFile result;
    result.status = 3;
    result.error_category = std::move(category);
    result.result_code = std::move(code);
    result.last_error = std::move(message);
    return result;
}

} // namespace

MetadataCoverStore::MetadataCoverStore(std::shared_ptr<http::client::HttpClient> client, std::shared_ptr<FileStorage> files, std::vector<std::string> headers) : client_(std::move(client)), files_(std::move(files)), headers_(std::move(headers))
{}

application::model::MetadataResourceFile MetadataCoverStore::save(std::string_view source, std::string_view owner_kind, std::string_view source_id, std::string_view url)
{
    if (!client_ || !files_ || !safeSegment(source) || !safeSegment(source_id) || (owner_kind != "singers" && owner_kind != "albums") || (!url.starts_with("https://") && !url.starts_with("http://"))) {
        return failure("invalid_target", "invalid_cover_target", "invalid metadata cover target");
    }
    http::client::HttpRequestOptions options;
    options.request_timeout = std::chrono::seconds(15);
    options.max_response_bytes = 8 * 1024 * 1024;
    std::optional<http::client::HttpResponse> response;
    try {
        response = client_->getWithOptions(url, headers_, options);
    } catch (const std::exception &) {
        return failure("network", "cover_fetch_failed", "metadata cover request failed");
    }
    if (!response || response->status_code < 200 || response->status_code >= 300) {
        return failure("network", "cover_fetch_failed", "metadata cover request failed");
    }
    if (response->body.size() > options.max_response_bytes) {
        return failure("network", "cover_too_large", "metadata cover exceeded the response size limit");
    }
    const std::string extension = imageExtension(response->body);
    if (extension.empty()) {
        return failure("parse", "invalid_cover_content", "metadata cover response is not a supported image");
    }
    const auto stamp = std::chrono::system_clock::now().time_since_epoch().count();
    application::model::SongResourceBlobSaveRequest request;
    request.relative_path_hint = std::string(source) + "/" + std::string(owner_kind) + "/" + std::string(source_id) + "/cover/" + std::to_string(stamp) + "-" + std::to_string(sequence_.fetch_add(1)) + "." + extension;
    request.data = std::move(response->body);
    request.content_type = std::move(response->content_type);
    auto saved = files_->saveSongResourceBlob(std::move(request));
    if (!saved.ok) {
        return failure("persistence", "cover_save_failed", "metadata cover file could not be committed");
    }
    application::model::MetadataResourceFile result;
    result.local_path = std::move(saved.local_path);
    result.size_bytes = saved.size_bytes;
    result.checksum = std::move(saved.checksum);
    result.status = 2;
    result.result_code = "ok";
    return result;
}

bool MetadataCoverStore::discard(std::string_view local_path)
{
    return files_ && !local_path.empty() && files_->remove(local_path);
}

} // namespace orchestrator::modules::crawler::infrastructure::storage::filesystem
