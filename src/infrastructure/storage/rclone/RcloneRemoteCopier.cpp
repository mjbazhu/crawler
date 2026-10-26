#include "crawler/infrastructure/storage/rclone/RcloneRemoteCopier.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::storage::rclone {

namespace {

using application::model::SongResourceRemoteCopyResult;

// RC JSON 请求结果，避免把 HTTP/JSON 细节泄漏到 application。
struct JsonPostResult {
    bool ok = false;         // 是否得到 2xx JSON 响应
    int status_code = 0;     // HTTP 状态码
    nlohmann::json body;     // 解析后的 JSON
    std::string result_code; // 失败编码
    std::string message;     // 脱敏诊断
};

std::string trimTrailingSlashes(std::string value)
{
    while (!value.empty() && value.back() == '/') {
        value.pop_back();
    }
    return value;
}

std::string normalizeRemotePath(std::string value)
{
    std::replace(value.begin(), value.end(), '\\', '/');
    while (!value.empty() && value.front() == '/') {
        value.erase(value.begin());
    }
    return value;
}

std::string lowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

std::string responseBodyText(const std::vector<char> &body)
{
    return std::string(body.begin(), body.end());
}

std::string redactUrlQueries(std::string message)
{
    std::size_t search_from = 0;
    while (search_from < message.size()) {
        const auto http_pos = message.find("http://", search_from);
        const auto https_pos = message.find("https://", search_from);
        std::size_t scheme_pos = std::string::npos;
        if (http_pos == std::string::npos) {
            scheme_pos = https_pos;
        } else if (https_pos == std::string::npos) {
            scheme_pos = http_pos;
        } else {
            scheme_pos = (std::min)(http_pos, https_pos);
        }
        if (scheme_pos == std::string::npos) {
            break;
        }

        const auto query_pos = message.find('?', scheme_pos);
        if (query_pos == std::string::npos) {
            break;
        }
        const auto end_pos = message.find_first_of(" \t\r\n\"'<>", query_pos);
        const auto replace_end = end_pos == std::string::npos ? message.size() : end_pos;
        message.replace(query_pos, replace_end - query_pos, "?[redacted]");
        search_from = query_pos + 11;
    }
    return message;
}

std::string sanitizeMessage(std::string message)
{
    constexpr std::size_t kMaxMessageLength = 512;
    message = redactUrlQueries(std::move(message));
    if (message.size() > kMaxMessageLength) {
        message.resize(kMaxMessageLength);
    }
    return message;
}

SongResourceRemoteCopyResult failure(std::string category, std::string code, std::string message)
{
    SongResourceRemoteCopyResult result;
    result.error_category = std::move(category);
    result.result_code = std::move(code);
    result.message = sanitizeMessage(std::move(message));
    return result;
}

std::string base64Encode(std::string_view input)
{
    static constexpr char kAlphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    std::string output;
    output.reserve(((input.size() + 2) / 3) * 4);
    int value = 0;
    int bits = -6;
    for (unsigned char ch : input) {
        value = (value << 8) + ch;
        bits += 8;
        while (bits >= 0) {
            output.push_back(kAlphabet[(value >> bits) & 0x3F]);
            bits -= 6;
        }
    }
    if (bits > -6) {
        output.push_back(kAlphabet[((value << 8) >> (bits + 8)) & 0x3F]);
    }
    while (output.size() % 4 != 0) {
        output.push_back('=');
    }
    return output;
}

std::vector<std::string> makeHeaders(const RcloneConfig &config)
{
    std::vector<std::string> headers {
        "Accept: application/json",
        "Content-Type: application/json",
    };
    if (!config.rc_user.empty() || !config.rc_password.empty()) {
        headers.push_back("Authorization: Basic " + base64Encode(config.rc_user + ":" + config.rc_password));
    }
    return headers;
}

std::string jobIdText(const nlohmann::json &job_id)
{
    if (job_id.is_string()) {
        return job_id.get<std::string>();
    }
    if (job_id.is_number_integer() || job_id.is_number_unsigned()) {
        return std::to_string(job_id.get<std::uint64_t>());
    }
    return job_id.dump();
}

std::optional<std::uint64_t> jsonUnsigned(const nlohmann::json &value)
{
    if (value.is_number_unsigned()) {
        return value.get<std::uint64_t>();
    }
    if (value.is_number_integer()) {
        const auto signed_value = value.get<std::int64_t>();
        if (signed_value >= 0) {
            return static_cast<std::uint64_t>(signed_value);
        }
    }
    if (value.is_string()) {
        try {
            return static_cast<std::uint64_t>(std::stoull(value.get<std::string>()));
        } catch (...) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

std::optional<std::uint64_t> objectSize(const nlohmann::json &item)
{
    if (item.contains("Size")) {
        return jsonUnsigned(item.at("Size"));
    }
    if (item.contains("size")) {
        return jsonUnsigned(item.at("size"));
    }
    return std::nullopt;
}

std::string jsonString(const nlohmann::json &value)
{
    if (value.is_string()) {
        return value.get<std::string>();
    }
    if (value.is_number_integer() || value.is_number_unsigned()) {
        return jobIdText(value);
    }
    if (value.is_boolean()) {
        return value.get<bool>() ? "true" : "false";
    }
    return {};
}

std::string selectHash(const nlohmann::json &hashes)
{
    if (!hashes.is_object()) {
        return {};
    }
    const std::vector<std::string> preferred_keys {"SHA-256", "sha256", "SHA256", "MD5", "md5", "SHA-1", "sha1"};
    for (const auto &key : preferred_keys) {
        if (!hashes.contains(key)) {
            continue;
        }
        const auto value = jsonString(hashes.at(key));
        if (!value.empty()) {
            return "rclone:" + lowerAscii(key) + ":" + value;
        }
    }
    for (auto iter = hashes.begin(); iter != hashes.end(); ++iter) {
        const auto value = jsonString(iter.value());
        if (!value.empty()) {
            return "rclone:" + lowerAscii(iter.key()) + ":" + value;
        }
    }
    return {};
}

std::string objectChecksum(const nlohmann::json &item, std::uint64_t size_bytes)
{
    if (item.contains("Hashes")) {
        auto checksum = selectHash(item.at("Hashes"));
        if (!checksum.empty()) {
            return checksum;
        }
    }
    if (item.contains("hashes")) {
        auto checksum = selectHash(item.at("hashes"));
        if (!checksum.empty()) {
            return checksum;
        }
    }
    return "rclone:size:" + std::to_string(size_bytes);
}

bool jsonBoolOr(const nlohmann::json &body, std::string_view key, bool fallback)
{
    const auto iter = body.find(std::string(key));
    if (iter == body.end()) {
        return fallback;
    }
    if (iter->is_boolean()) {
        return iter->get<bool>();
    }
    if (iter->is_number_integer()) {
        return iter->get<int>() != 0;
    }
    if (iter->is_string()) {
        const auto normalized = lowerAscii(iter->get<std::string>());
        return normalized == "true" || normalized == "1" || normalized == "yes";
    }
    return fallback;
}

std::string jsonStringOrEmpty(const nlohmann::json &body, std::string_view key)
{
    const auto iter = body.find(std::string(key));
    if (iter == body.end()) {
        return {};
    }
    return jsonString(*iter);
}

int effectiveTimeoutMs(const RcloneConfig &config, int request_timeout_ms)
{
    if (request_timeout_ms > 0) {
        return request_timeout_ms;
    }
    return config.job_timeout_ms > 0 ? config.job_timeout_ms : 600000;
}

int effectivePollIntervalMs(const RcloneConfig &config)
{
    return config.job_poll_interval_ms > 0 ? config.job_poll_interval_ms : 2000;
}

} // namespace

RcloneRemoteCopier::RcloneRemoteCopier(
    std::shared_ptr<http::client::HttpClient> http_client,
    std::shared_ptr<application::ports::AsyncRuntime> runtime,
    RcloneConfig config
) : http_client_(std::move(http_client)),
    runtime_(std::move(runtime)),
    config_(std::move(config))
{
    config_.rc_url = trimTrailingSlashes(std::move(config_.rc_url));
}

application::async::Task<application::model::SongResourceRemoteCopyResult> RcloneRemoteCopier::copyUrl(application::model::SongResourceRemoteCopyRequest request)
{
    if (!runtime_) {
        co_return failure("invalid_target", "missing_dependency", "async runtime is not configured for rclone remote copy");
    }
    auto result = co_await runtime_->postBlocking<application::model::SongResourceRemoteCopyResult>([this, request = std::move(request)]() mutable {
        return copyUrlBlocking(std::move(request));
    });
    co_return result;
}

application::model::SongResourceRemoteCopyResult RcloneRemoteCopier::copyUrlBlocking(application::model::SongResourceRemoteCopyRequest request)
{
    if (!http_client_) {
        return failure("invalid_target", "missing_dependency", "rclone http client is not configured");
    }
    if (config_.rc_url.empty() || config_.target_fs.empty()) {
        return failure("invalid_target", "invalid_rclone_config", "rclone rc_url or target_fs is empty");
    }
    if (request.source_url.empty()) {
        return failure("invalid_target", "missing_remote_url", "resource remote url is empty");
    }

    const std::string remote_path = normalizeRemotePath(std::move(request.relative_path_hint));
    if (remote_path.empty()) {
        return failure("persistence", "path_plan_failed", "resource remote path is empty");
    }

    const auto headers = makeHeaders(config_);
    const auto post_json = [this, &headers](std::string_view endpoint, const nlohmann::json &payload) {
        JsonPostResult result;
        const std::string url = config_.rc_url + "/" + std::string(endpoint);
        auto response = http_client_->post(url, payload.dump(), headers);
        if (!response) {
            result.result_code = "rclone_request_failed";
            result.message = "rclone rc request failed";
            return result;
        }
        result.status_code = response->status_code;
        if (response->status_code < 200 || response->status_code >= 300) {
            result.result_code = "rclone_http_status_" + std::to_string(response->status_code);
            result.message = "rclone rc request returned HTTP " + std::to_string(response->status_code);
            return result;
        }
        auto parsed = nlohmann::json::parse(responseBodyText(response->body), nullptr, false);
        if (parsed.is_discarded()) {
            result.result_code = "rclone_response_parse_failed";
            result.message = "rclone rc response was not valid JSON";
            return result;
        }
        result.ok = true;
        result.body = std::move(parsed);
        return result;
    };

    nlohmann::json copy_payload;
    copy_payload["fs"] = config_.target_fs;
    copy_payload["remote"] = remote_path;
    copy_payload["url"] = request.source_url;
    copy_payload["autoFilename"] = false;
    copy_payload["_async"] = true;

    auto copy_response = post_json("operations/copyurl", copy_payload);
    if (!copy_response.ok) {
        return failure("network", copy_response.result_code, copy_response.message);
    }
    if (!copy_response.body.contains("jobid")) {
        return failure("persistence", "rclone_missing_jobid", "rclone copyurl response did not include jobid");
    }

    const nlohmann::json job_id = copy_response.body.at("jobid");
    const auto timeout = std::chrono::milliseconds(effectiveTimeoutMs(config_, request.timeout_ms));
    const auto poll_interval = std::chrono::milliseconds(effectivePollIntervalMs(config_));
    const auto deadline = std::chrono::steady_clock::now() + timeout;

    while (true) {
        nlohmann::json status_payload;
        status_payload["jobid"] = job_id;
        auto status_response = post_json("job/status", status_payload);
        if (!status_response.ok) {
            return failure("network", status_response.result_code, status_response.message);
        }

        const auto error = jsonStringOrEmpty(status_response.body, "error");
        if (!error.empty()) {
            return failure("network", "rclone_job_failed", error);
        }
        if (jsonBoolOr(status_response.body, "finished", false)) {
            break;
        }

        const auto now = std::chrono::steady_clock::now();
        if (now >= deadline) {
            return failure("timeout", "rclone_job_timeout", "rclone copyurl job timed out: " + jobIdText(job_id));
        }
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - now);
        std::this_thread::sleep_for((std::min)(poll_interval, remaining));
    }

    nlohmann::json stat_payload;
    stat_payload["fs"] = config_.target_fs;
    stat_payload["remote"] = remote_path;
    auto stat_response = post_json("operations/stat", stat_payload);
    if (!stat_response.ok) {
        return failure("persistence", stat_response.result_code, stat_response.message);
    }

    const nlohmann::json *item = &stat_response.body;
    if (stat_response.body.contains("item") && stat_response.body.at("item").is_object()) {
        item = &stat_response.body.at("item");
    }
    auto size_bytes = objectSize(*item);
    if (!size_bytes || *size_bytes == 0) {
        return failure("persistence", "rclone_stat_invalid", "rclone stat did not return a non-empty object size");
    }

    SongResourceRemoteCopyResult result;
    result.ok = true;
    result.local_path = remote_path;
    result.size_bytes = *size_bytes;
    result.checksum = objectChecksum(*item, *size_bytes);
    result.result_code = "ok";
    return result;
}

} // namespace orchestrator::modules::crawler::infrastructure::storage::rclone
