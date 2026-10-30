#include "crawler/infrastructure/storage/rclone/RcloneCloudFileStore.h"

#include "crawler/infrastructure/http/client/AsioHttpUrl.h"
#include "crawler/infrastructure/storage/cloud/CloudFileSupport.h"
#include "crawler/infrastructure/storage/rclone/RcloneRcSupport.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <optional>
#include <stdexcept>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::storage::rclone {
namespace {
namespace model = application::model;
using json = nlohmann::json;

// 摘要字段名在不同 rclone 后端使用不同大小写。
std::string lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}
} // namespace

RcloneCloudFileStore::RcloneCloudFileStore(std::shared_ptr<http::client::HttpClient> client, RcloneConfig config) : client_(std::move(client)), config_(detail::normalizeCloudConfig(std::move(config)))
{
    if (!client_) throw std::invalid_argument("rclone client is required");
}

std::string RcloneCloudFileStore::backend() const
{
    return "rclone";
}
std::string RcloneCloudFileStore::prefix() const
{
    return config_.target_fs + '/';
}

model::CloudFileResult RcloneCloudFileStore::upload(model::CloudFileUploadRequest request)
{
    if (!cloud::validUpload(request)) return cloud::failure("invalid_cloud_snapshot", "invalid_target");
    cloud::Deadline deadline(request.timeout_ms, request.cancelled);
    const auto md5 = cloud::md5(request.bytes);
    const auto post = [&](const std::string &endpoint, const std::string &body, const std::string &type) -> std::optional<json> {
        if (!deadline.remaining()) return std::nullopt;
        http::client::HttpRequestOptions options;
        options.request_timeout = std::chrono::milliseconds(deadline.remaining());
        options.max_response_bytes = config_.api_max_response_bytes;
        auto response = client_->postWithOptions(config_.rc_url + '/' + endpoint, body, detail::rcHeaders(config_, type), options);
        if (!response || response->status_code < 200 || response->status_code >= 300) return std::nullopt;
        auto parsed = json::parse(std::string(response->body.begin(), response->body.end()), nullptr, false);
        if (parsed.is_discarded() || !parsed.is_object() || (parsed.contains("error") && parsed["error"].is_string() && !parsed["error"].get<std::string>().empty())) return std::nullopt;
        return parsed;
    };
    // -1 未知，0 明确不存在，1 内容匹配，2 已有不同内容或目录。
    const auto verify = [&]() -> int {
        const auto response = post("operations/stat", json {{"fs", config_.target_fs}, {"remote", request.relative_path}, {"opt", {{"showHash", true}, {"filesOnly", true}}}}.dump(), "application/json");
        if (!response || !response->contains("item")) return -1;
        const auto &item = response->at("item");
        if (item.is_null()) return 0;
        if (!item.is_object() || !item.contains("Size") || !item["Size"].is_number_integer()) return -1;
        if (item.contains("IsDir") && !item["IsDir"].is_boolean()) return -1;
        if (item.value("IsDir", false) || item["Size"] < 0 || item["Size"] != request.bytes.size()) return 2;
        if (!item.contains("Hashes") || !item["Hashes"].is_object()) return -1;
        std::string remote_md5;
        for (const auto &[name, hash] : item["Hashes"].items()) {
            if (!hash.is_string() || hash.get<std::string>().empty()) continue;
            const auto key = lower(name), value = lower(hash.get<std::string>());
            if (key == "sha-256" || key == "sha256") return value == request.checksum ? 1 : 2;
            if (key == "md5") remote_md5 = value;
        }
        if (!remote_md5.empty() && !md5.empty()) return remote_md5 == md5 ? 1 : 2;
        return -1;
    };
    const auto stopped = [&] { return cloud::failure(deadline.cancelled() ? "cancelled" : "cloud_timeout", deadline.cancelled() ? "resource" : "timeout", true); };
    int observed = verify();
    if (!deadline.remaining()) return stopped();
    if (observed == 2) return cloud::failure("cloud_content_mismatch");
    if (observed < 0) return cloud::failure("rclone_verification_unknown", "network", true);
    const bool reused = observed == 1;
    if (!reused) {
        const auto slash = request.relative_path.find_last_of('/');
        const auto directory = slash == std::string::npos ? std::string {} : request.relative_path.substr(0, slash);
        const auto filename = request.relative_path.substr(slash == std::string::npos ? 0 : slash + 1);
        const auto boundary = "crawler-" + cloud::uniqueToken();
        const auto multipart = "--" + boundary + "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"" + filename + "\"\r\nContent-Type: application/octet-stream\r\n\r\n" + request.bytes + "\r\n--" + boundary + "--\r\n";
        const auto uploaded = post("operations/uploadfile?fs=" + detail::queryEncode(config_.target_fs) + "&remote=" + detail::queryEncode(directory), multipart, "multipart/form-data; boundary=" + boundary);
        if (!deadline.remaining()) return stopped();
        if (!uploaded) return cloud::failure("rclone_upload_uncertain", "network", true);
        observed = verify();
        if (!deadline.remaining()) return stopped();
        if (observed != 1) return cloud::failure(observed == 2 ? "cloud_content_mismatch" : "rclone_verification_unknown", "persistence", observed != 2);
    }
    model::CloudFileResult result;
    result.ok = true;
    result.reused = reused;
    result.receipt = {backend(), prefix() + request.relative_path, request.bytes.size(), request.checksum};
    result.result_code = "ok";
    return result;
}

} // namespace orchestrator::modules::crawler::infrastructure::storage::rclone
