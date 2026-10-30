#include "RcloneTransferProtocol.h"

#include "crawler/infrastructure/storage/cloud/CloudTransferSupport.h"
#include "crawler/infrastructure/storage/rclone/RcloneRcSupport.h"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::storage::rclone::transfer {
namespace {
using json = nlohmann::json;
using Error = result::ServiceErrorType;
// 大小写兼容但不将不合法 hash 当作证据。
std::string normalizedHash(std::string value, std::size_t length)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value.size() == length && std::all_of(value.begin(), value.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }) ? value : std::string {};
}
} // namespace

result::ServiceResult<json> parse(const http::client::HttpUploadResult &response)
{
    using Result = result::ServiceResult<json>;
    if (!response.response || response.response->status_code < 200 || response.response->status_code >= 300) return Result::failure(Error::ExternalApiError, "rclone_rpc_unknown");
    auto value = json::parse(std::string(response.response->body.begin(), response.response->body.end()), nullptr, false);
    if (value.is_discarded() || !value.is_object() || (value.contains("error") && (!value["error"].is_string() || !value["error"].get<std::string>().empty()))) return Result::failure(Error::ExternalApiError, "rclone_rpc_unknown");
    return Result::success(std::move(value));
}

result::ServiceResult<json> post(http::client::HttpClient &client, const RcloneConfig &config, std::string endpoint, json body, const model::ResourceTransferContext &context, bool readback, std::uint64_t bytes_per_second)
{
    if (context.stopped()) return result::ServiceResult<json>::failure(Error::ExternalApiError, context.remaining() == 0 ? "timeout" : "cancelled");
    body["_config"] = {{"Retries", 1}, {"LowLevelRetries", 1}};
    if (bytes_per_second) body["_config"]["BwLimitFile"] = std::to_string(bytes_per_second) + 'B';
    auto text = body.dump();
    http::client::HttpUploadBody input;
    input.size_bytes = text.size();
    input.cancelled = [context] { return context.stopped(); };
    input.read = [text = std::move(text), offset = std::size_t {0}](std::span<char> chunk) mutable -> std::optional<std::size_t> {
        const auto size = (std::min)(chunk.size(), text.size() - offset);
        std::memcpy(chunk.data(), text.data() + offset, size);
        offset += size;
        return size;
    };
    http::client::HttpRequestOptions options;
    options.request_timeout = std::chrono::milliseconds(readback ? context.remaining() : (std::min)(context.remaining(), (std::max)(1, config.api_timeout_ms)));
    options.max_response_bytes = config.api_max_response_bytes;
    return parse(client.streamPostWithOptions(config.rc_url + '/' + endpoint, std::move(input), detail::rcHeaders(config), options));
}

result::ServiceResult<model::ResourceAttemptInspection> verify(http::client::HttpClient &client, const RcloneConfig &config, const model::ResourceTransferAttempt &attempt, std::string md5, const model::ResourceTransferContext &context, bool allow_absent, std::uint64_t bytes_per_second)
{
    using Result = result::ServiceResult<model::ResourceAttemptInspection>;
    using Observation = model::ResourceAttemptObservation;
    const auto prefix = config.target_fs + '/';
    const auto relative = attempt.path.substr(prefix.size());
    const auto queried = post(client, config, "operations/stat", {{"fs", config.target_fs}, {"remote", relative}, {"opt", {{"showHash", true}}}}, context, false, bytes_per_second);
    if (!queried.isSuccess()) return Result::failure(queried.error(), queried.message());
    const auto &value = *queried.data();
    if (!value.contains("item")) return Result::success({Observation::Unknown, {}});
    const auto &item = value["item"];
    if (item.is_null()) return Result::success({allow_absent ? Observation::Absent : Observation::Unknown, {}});
    if (!item.is_object() || !item.contains("Size") || !item["Size"].is_number_integer() || !item.contains("IsDir") || !item["IsDir"].is_boolean()) return Result::success({Observation::Unknown, {}});
    if (item["IsDir"].get<bool>() || item["Size"] < 0 || item["Size"] != attempt.size_bytes) return Result::success({Observation::Conflict, {}});
    std::string sha, remote_md5;
    if (item.contains("Hashes") && item["Hashes"].is_object()) {
        for (const auto &[key, hash] : item["Hashes"].items()) {
            if (!hash.is_string()) continue;
            auto name = key;
            std::transform(name.begin(), name.end(), name.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
            if (name == "sha256" || name == "sha-256") sha = normalizedHash(hash.get<std::string>(), 64);
            if (name == "md5") remote_md5 = normalizedHash(hash.get<std::string>(), 32);
        }
    }
    std::string algorithm, hash, method;
    if (!sha.empty()) {
        algorithm = "sha256";
        hash = sha;
        method = "remote_sha256";
    } else if (!md5.empty() && !remote_md5.empty()) {
        algorithm = "md5";
        hash = remote_md5;
        method = "remote_md5";
    } else {
        // repair 不重新访问源站；让 RC 对这个精确对象逐块回读 SHA-256。
        const auto readback = post(client, config, "operations/hashsumfile", {{"fs", config.target_fs}, {"remote", relative}, {"hashType", "SHA-256"}, {"download", true}, {"base64", false}}, context, true, bytes_per_second);
        if (!readback.isSuccess()) return Result::failure(readback.error(), readback.message());
        if (!readback.data()->contains("hash") || !(*readback.data())["hash"].is_string()) return Result::success({Observation::Unknown, {}});
        if (!readback.data()->contains("hashType") || !(*readback.data())["hashType"].is_string()) return Result::success({Observation::Unknown, {}});
        auto hash_type = (*readback.data())["hashType"].get<std::string>();
        std::transform(hash_type.begin(), hash_type.end(), hash_type.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
        if (hash_type != "sha256" && hash_type != "sha-256") return Result::success({Observation::Unknown, {}});
        hash = normalizedHash((*readback.data())["hash"].get<std::string>(), 64);
        if (hash.empty()) return Result::success({Observation::Unknown, {}});
        algorithm = "sha256";
        method = "remote_readback_sha256";
    }
    if (hash != (algorithm == "sha256" ? attempt.checksum : md5)) return Result::success({Observation::Conflict, {}});
    return Result::success({Observation::Matched, cloud::cloudTransferReceipt(attempt, "rclone", algorithm, hash, method)});
}

} // namespace orchestrator::modules::crawler::infrastructure::storage::rclone::transfer
