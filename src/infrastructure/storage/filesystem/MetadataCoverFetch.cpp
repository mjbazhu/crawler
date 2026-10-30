#include "MetadataCoverIo.h"

#include "crawler/infrastructure/http/client/AsioHttpUrl.h"

#include <algorithm>
#include <charconv>
#include <chrono>
#include <cctype>
#include <exception>
#include <set>
#include <string_view>

namespace orchestrator::modules::crawler::infrastructure::storage::filesystem::cover_io {
namespace {

// Header 比较不区分大小写。
std::string lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}

// 生成无 URL、无上游响应正文的失败。
FetchResult failure(std::string category, std::string code)
{
    FetchResult result;
    result.error.status = 3;
    result.error.error_category = std::move(category);
    result.error.result_code = std::move(code);
    result.error.last_error = "metadata cover download failed";
    return result;
}

// 只处理 HTTP 图片跳转；相对路径基于当前 URL 解析。
std::string redirected(const std::string &base, const std::string &location)
{
    if (location.empty() || location.find_first_of("\r\n") != std::string::npos) return {};
    if (location.starts_with("http://") || location.starts_with("https://")) return location;
    const auto scheme = base.find("://");
    if (scheme == std::string::npos) return {};
    if (location.starts_with("//")) return base.substr(0, scheme + 1) + location;
    if (location.find(':') != std::string::npos) return {};
    const auto path = base.find('/', scheme + 3);
    const auto origin = path == std::string::npos ? base : base.substr(0, path);
    if (location.front() == '/') return origin + location;
    auto current = path == std::string::npos ? origin + '/' : base.substr(0, base.find_first_of("?#"));
    if (location.front() == '?') return current + location;
    return current.substr(0, current.rfind('/') + 1) + location;
}

} // namespace

std::string imageExtension(const std::vector<char> &bytes)
{
    const std::string_view data(bytes.data(), bytes.size());
    if (data.size() >= 45 && data.starts_with("\x89PNG\r\n\x1a\n") && data.substr(12, 4) == "IHDR" && data.substr(data.size() - 8, 4) == "IEND") return "png";
    if (data.size() >= 5 && static_cast<unsigned char>(data[0]) == 0xff && static_cast<unsigned char>(data[1]) == 0xd8 && static_cast<unsigned char>(data[2]) == 0xff && static_cast<unsigned char>(data[data.size() - 2]) == 0xff && static_cast<unsigned char>(data.back()) == 0xd9) return "jpg";
    if (data.size() >= 14 && (data.starts_with("GIF87a") || data.starts_with("GIF89a")) && data.back() == ';') return "gif";
    if (data.size() >= 20 && data.starts_with("RIFF") && data.substr(8, 4) == "WEBP") {
        std::uint64_t size = 0;
        for (int i = 0; i < 4; ++i) size |= static_cast<std::uint64_t>(static_cast<unsigned char>(data[4 + i])) << (8 * i);
        if (size + 8 == data.size()) return "webp";
    }
    if (data.size() >= 24 && data.substr(4, 4) == "ftyp" && (data.substr(8, 4) == "avif" || data.substr(8, 4) == "avis")) return "avif";
    return {};
}

std::string imageMime(const std::string &extension)
{
    return extension == "jpg" ? "image/jpeg" : "image/" + extension;
}

FetchResult fetch(http::client::HttpClient &client, std::string url, std::vector<std::string> headers, int timeout_ms, const std::function<bool()> &cancelled, std::size_t max_bytes)
{
    const auto limit = (std::min)(max_bytes, maxCoverBytes);
    if (limit == 0) return failure("invalid_target", "invalid_cover_limit");
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::milliseconds((std::clamp)(timeout_ms, 100, 600000));
    std::set<std::string> visited;
    for (int hop = 0; hop <= 3; ++hop) {
        const auto parsed = http::client::detail::parseUrl(url);
        if (!parsed || parsed->host.find('@') != std::string::npos) return failure("invalid_target", "invalid_cover_url");
        if (!visited.insert(url).second) return failure("network", "cover_redirect_loop");
        if (cancelled && cancelled()) return failure("resource", "cancelled");
        const auto remaining = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now());
        if (remaining.count() <= 0) return failure("timeout", "timeout");
        http::client::HttpRequestOptions options;
        options.request_timeout = remaining;
        options.max_response_bytes = limit;
        http::client::HttpStreamInfo response;
        std::vector<char> bytes;
        bool oversized = false;
        http::client::HttpStreamCallbacks callbacks;
        callbacks.on_response = [&](const auto &info) { response = info; return info.status_code >= 200 && info.status_code < 300; };
        callbacks.on_chunk = [&](std::string_view chunk) {
            if (cancelled && cancelled()) return false;
            if (chunk.size() > limit - bytes.size()) {
                oversized = true;
                return false;
            }
            bytes.insert(bytes.end(), chunk.begin(), chunk.end());
            return true;
        };
        http::client::HttpStreamResult transfer;
        try {
            transfer = client.streamGetWithOptions(url, headers, options, std::move(callbacks));
        } catch (const std::exception &) {
            return failure("network", "network_failed");
        }
        // Asio 对非 2xx 提前返回，不调用 body 消费者；状态与头仍保留在传输结果中。
        if (response.status_code == 0) response = {transfer.status_code, transfer.content_type, transfer.headers};
        if (cancelled && cancelled()) return failure("resource", "cancelled");
        if (response.status_code == 301 || response.status_code == 302 || response.status_code == 303 || response.status_code == 307 || response.status_code == 308) {
            std::string location;
            for (const auto &header : response.headers)
                if (lower(header.name) == "location") location = header.value;
            const auto next = redirected(url, location);
            const auto destination = http::client::detail::parseUrl(next);
            if (!destination || (parsed->tls && !destination->tls)) return failure("network", "invalid_cover_redirect");
            if (hop == 3) return failure("network", "cover_redirect_limit");
            if (destination->host != parsed->host || destination->port != parsed->port || destination->scheme != parsed->scheme) {
                headers.erase(std::remove_if(headers.begin(), headers.end(), [](const std::string &header) {
                                  const auto name = lower(header.substr(0, header.find(':')));
                                  return name != "user-agent" && name != "accept";
                              }),
                              headers.end());
            }
            url = next;
            continue;
        }
        if (oversized || transfer.result_code == "response_too_large" || transfer.result_code == "body_too_large") return failure("resource", "cover_too_large");
        if (response.status_code != 0 && (response.status_code < 200 || response.status_code >= 300)) return failure("network", "http_" + std::to_string(response.status_code));
        if (!transfer.ok) return failure(transfer.result_code == "timeout" ? "timeout" : "network", transfer.result_code == "timeout" ? "timeout" : "network_failed");
        if (transfer.bytes_received != bytes.size()) return failure("network", "cover_incomplete");
        const bool encoded = std::any_of(response.headers.begin(), response.headers.end(), [](const auto &header) { return lower(header.name) == "content-encoding" && lower(header.value) != "identity"; });
        for (const auto &header : response.headers) {
            if (lower(header.name) != "content-length") continue;
            // 流式客户端已验证压缩传输长度，此处 bytes 是解码后的图片。
            if (encoded) continue;
            std::uint64_t expected = 0;
            const auto parsed_size = std::from_chars(header.value.data(), header.value.data() + header.value.size(), expected);
            if (parsed_size.ec != std::errc {} || parsed_size.ptr != header.value.data() + header.value.size() || expected != bytes.size()) return failure("network", "cover_incomplete");
        }
        auto extension = imageExtension(bytes);
        if (extension.empty()) return failure("parse", "invalid_cover_content");
        FetchResult result;
        result.bytes = std::move(bytes);
        result.extension = std::move(extension);
        return result;
    }
    return failure("network", "cover_redirect_limit");
}

} // namespace orchestrator::modules::crawler::infrastructure::storage::filesystem::cover_io
