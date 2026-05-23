#include "crawler/infrastructure/http/client/AsioHttpResponseParser.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <iostream>
#include <sstream>
#include <system_error>
#include <unordered_map>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::http::client::detail {

namespace {

// 转换为小写，供 header 名称和 token 比较使用。
std::string toLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

// 去掉字符串两端空白。
std::string trim(std::string_view value)
{
    auto begin = value.begin();
    auto end = value.end();
    while (begin != end && std::isspace(static_cast<unsigned char>(*begin))) {
        ++begin;
    }
    while (begin != end && std::isspace(static_cast<unsigned char>(*(end - 1)))) {
        --end;
    }
    return std::string(begin, end);
}

// 不区分大小写地查找子串。
bool containsCaseInsensitive(std::string_view value, std::string_view needle)
{
    return toLower(std::string(value)).find(toLower(std::string(needle))) != std::string::npos;
}

// 解析 chunked body 单个 chunk 的十六进制长度。
std::optional<std::size_t> parseChunkSize(std::string_view line)
{
    const auto extension_pos = line.find(';');
    if (extension_pos != std::string_view::npos) {
        line = line.substr(0, extension_pos);
    }
    const std::string cleaned = trim(line);

    std::size_t size = 0;
    const char *begin = cleaned.data();
    const char *end = begin + cleaned.size();
    auto [ptr, ec] = std::from_chars(begin, end, size, 16);
    if (ec != std::errc() || ptr != end) {
        return std::nullopt;
    }
    return size;
}

// 解码 HTTP chunked body，并强制响应体大小上限。
std::optional<std::vector<char>> decodeChunkedBody(std::string_view encoded, std::size_t max_response_bytes)
{
    std::vector<char> decoded;
    std::size_t offset = 0;

    while (offset < encoded.size()) {
        const auto line_end = encoded.find("\r\n", offset);
        if (line_end == std::string_view::npos) {
            return std::nullopt;
        }

        const auto chunk_size = parseChunkSize(encoded.substr(offset, line_end - offset));
        if (!chunk_size) {
            return std::nullopt;
        }
        offset = line_end + 2;

        if (*chunk_size == 0) {
            return decoded;
        }
        if (*chunk_size > encoded.size() - offset || decoded.size() + *chunk_size > max_response_bytes) {
            return std::nullopt;
        }

        decoded.insert(decoded.end(), encoded.begin() + static_cast<std::ptrdiff_t>(offset), encoded.begin() + static_cast<std::ptrdiff_t>(offset + *chunk_size));
        offset += *chunk_size;

        if (offset + 2 > encoded.size() || encoded.substr(offset, 2) != "\r\n") {
            return std::nullopt;
        }
        offset += 2;
    }

    return std::nullopt;
}

} // namespace

std::size_t findHeaderEnd(const std::string &raw_response)
{
    const auto crlf_pos = raw_response.find("\r\n\r\n");
    if (crlf_pos != std::string::npos) {
        return crlf_pos + 4;
    }
    const auto lf_pos = raw_response.find("\n\n");
    if (lf_pos != std::string::npos) {
        return lf_pos + 2;
    }
    return std::string::npos;
}

int parseStatusCode(std::string_view headers_part)
{
    const auto line_end = headers_part.find('\n');
    std::string status_line(headers_part.substr(0, line_end));
    if (!status_line.empty() && status_line.back() == '\r') {
        status_line.pop_back();
    }

    std::istringstream status_stream(status_line);
    std::string http_version;
    int status_code = 0;
    status_stream >> http_version >> status_code;
    return status_code;
}

std::optional<HttpResponse> parseHttpResponse(const std::string &raw_response, std::size_t max_response_bytes)
{
    const auto header_end = findHeaderEnd(raw_response);
    if (header_end == std::string::npos || header_end > kMaxHeaderBytes) {
        return std::nullopt;
    }

    std::string headers_part = raw_response.substr(0, header_end);
    std::string body_part = raw_response.substr(header_end);

    std::istringstream stream(headers_part);
    std::string status_line;
    std::getline(stream, status_line);
    if (!status_line.empty() && status_line.back() == '\r') {
        status_line.pop_back();
    }

    std::istringstream status_stream(status_line);
    std::string http_version;
    int status_code = 0;
    status_stream >> http_version >> status_code;
    if (status_code <= 0) {
        return std::nullopt;
    }

    std::unordered_map<std::string, std::string> headers;
    std::vector<HttpHeader> parsed_headers;
    std::string line;
    bool reading_headers = true;
    while (reading_headers && std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            reading_headers = false;
        } else {
            const auto pos = line.find(':');
            if (pos != std::string::npos) {
                const std::string header_name = trim(std::string_view(line).substr(0, pos));
                const std::string header_value = trim(std::string_view(line).substr(pos + 1));
                headers[toLower(header_name)] = header_value;
                parsed_headers.push_back({header_name, header_value});
            }
        }
    }

    if (auto encoding = headers.find("content-encoding"); encoding != headers.end() && !encoding->second.empty() && toLower(encoding->second) != "identity") {
        std::cerr << "[AsioHttpClient] Unsupported content encoding: " << encoding->second << std::endl;
        return std::nullopt;
    }

    std::vector<char> body;
    if (auto transfer_encoding = headers.find("transfer-encoding"); transfer_encoding != headers.end() && containsCaseInsensitive(transfer_encoding->second, "chunked")) {
        auto decoded = decodeChunkedBody(body_part, max_response_bytes);
        if (!decoded) {
            return std::nullopt;
        }
        body = std::move(*decoded);
    } else {
        if (body_part.size() > max_response_bytes) {
            return std::nullopt;
        }
        body.assign(body_part.begin(), body_part.end());
    }

    HttpResponse response;
    response.status_code = status_code;
    response.body = std::move(body);
    if (auto content_type = headers.find("content-type"); content_type != headers.end()) {
        response.content_type = content_type->second;
    }
    response.headers = std::move(parsed_headers);
    return response;
}

} // namespace orchestrator::modules::crawler::infrastructure::http::client::detail
