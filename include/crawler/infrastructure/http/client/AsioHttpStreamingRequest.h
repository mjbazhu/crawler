#pragma once

#include "crawler/infrastructure/http/client/AsioHttpClient.h"
#include "crawler/infrastructure/http/client/AsioHttpConnectionHelpers.h"
#include "crawler/infrastructure/http/client/AsioHttpRequestBuilder.h"
#include "crawler/infrastructure/http/client/AsioHttpResponseParser.h"
#include "crawler/infrastructure/http/client/AsioHttpSessionTypes.h"
#include "crawler/infrastructure/http/client/AsioHttpUrl.h"

#include <algorithm>
#include <array>
#include <charconv>
#include <cctype>
#include <cstdint>
#include <future>
#include <iostream>
#include <memory>
#include <optional>
#include <sstream>
#include <string>
#include <string_view>
#include <type_traits>
#include <unordered_map>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::http::client::detail {

template <typename Stream>
Stream makeStreamingSessionStream(IoStrand &strand, ssl::context *ssl_context);

template <>
inline TcpSocket makeStreamingSessionStream<TcpSocket>(IoStrand &strand, ssl::context *)
{
    return TcpSocket(strand);
}

template <>
inline TlsStream makeStreamingSessionStream<TlsStream>(IoStrand &strand, ssl::context *ssl_context)
{
    return TlsStream(strand, *ssl_context);
}

// 流式响应头解析结果，只保留下载流程需要的字段。
struct StreamingResponseHeaders {
    int status_code = 0;                         // HTTP 状态码
    std::string content_type;                    // Content-Type 响应头
    std::string content_encoding;                // Content-Encoding 响应头
    std::optional<std::uint64_t> content_length; // Content-Length；缺失时读取到 EOF
    bool chunked = false;                        // 是否为 chunked body
    std::vector<HttpHeader> raw_headers;         // 原始响应头列表
};

// 转换为小写，供 header 名称和 token 比较使用。
inline std::string streamingToLower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

// 去掉字符串两端空白。
inline std::string streamingTrim(std::string_view value)
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

// 不区分大小写地查找响应头 token。
inline bool streamingContainsToken(std::string_view value, std::string_view token)
{
    return streamingToLower(std::string(value)).find(streamingToLower(std::string(token))) != std::string::npos;
}

// 解析十进制 Content-Length。
inline std::optional<std::uint64_t> parseStreamingContentLength(std::string_view value)
{
    const std::string cleaned = streamingTrim(value);
    std::uint64_t length = 0;
    const char *begin = cleaned.data();
    const char *end = begin + cleaned.size();
    auto [ptr, ec] = std::from_chars(begin, end, length, 10);
    if (ec != std::errc() || ptr != end) {
        return std::nullopt;
    }
    return length;
}

// 解析 chunked body 的十六进制 chunk 大小。
inline std::optional<std::uint64_t> parseStreamingChunkSize(std::string_view line)
{
    const auto extension_pos = line.find(';');
    if (extension_pos != std::string_view::npos) {
        line = line.substr(0, extension_pos);
    }
    const std::string cleaned = streamingTrim(line);
    std::uint64_t size = 0;
    const char *begin = cleaned.data();
    const char *end = begin + cleaned.size();
    auto [ptr, ec] = std::from_chars(begin, end, size, 16);
    if (ec != std::errc() || ptr != end) {
        return std::nullopt;
    }
    return size;
}

// 解析 HTTP 响应头，不读取或保存 body。
inline std::optional<StreamingResponseHeaders> parseStreamingResponseHeaders(std::string_view headers_part)
{
    StreamingResponseHeaders parsed;
    parsed.status_code = parseStatusCode(headers_part);
    if (parsed.status_code <= 0) {
        return std::nullopt;
    }

    std::istringstream stream {std::string(headers_part)};
    std::string line;
    std::getline(stream, line);
    std::unordered_map<std::string, std::string> headers;
    while (std::getline(stream, line)) {
        if (!line.empty() && line.back() == '\r') {
            line.pop_back();
        }
        if (line.empty()) {
            break;
        }
        const auto pos = line.find(':');
        if (pos == std::string::npos) {
            continue;
        }
        const std::string name = streamingTrim(std::string_view(line).substr(0, pos));
        const std::string value = streamingTrim(std::string_view(line).substr(pos + 1));
        if (name.empty()) {
            continue;
        }
        headers[streamingToLower(name)] = value;
        parsed.raw_headers.push_back({name, value});
    }

    if (auto iter = headers.find("content-type"); iter != headers.end()) {
        parsed.content_type = iter->second;
    }
    if (auto iter = headers.find("content-encoding"); iter != headers.end()) {
        parsed.content_encoding = iter->second;
    }
    if (auto iter = headers.find("content-length"); iter != headers.end()) {
        parsed.content_length = parseStreamingContentLength(iter->second);
    }
    if (auto iter = headers.find("transfer-encoding"); iter != headers.end()) {
        parsed.chunked = streamingContainsToken(iter->second, "chunked");
    }
    return parsed;
}

// 基于 Asio 原始 socket 的流式 HTTP 请求会话，body 分片直接回调给调用方。
template <typename Stream>
class HttpStreamingSession : public std::enable_shared_from_this<HttpStreamingSession<Stream>> {
public:
    HttpStreamingSession(
        asio::io_context &io_context,
        ssl::context *ssl_context,
        ParsedUrl url,
        std::string request,
        AsioHttpClient::Options options,
        HttpStreamCallbacks callbacks,
        std::shared_ptr<std::promise<HttpStreamResult>> promise
    ) : strand_(asio::make_strand(io_context)),
        resolver_(strand_),
        timer_(strand_),
        stream_(makeStreamingSessionStream<Stream>(strand_, ssl_context)),
        url_(std::move(url)),
        request_(std::move(request)),
        options_(options),
        callbacks_(std::move(callbacks)),
        promise_(std::move(promise))
    {}

    // 启动完整流式请求生命周期。
    void start()
    {
        armTimeout();
        const std::pair<std::string, std::string> endpoint = endpointTarget();
        resolver_.async_resolve(
            endpoint.first,
            endpoint.second,
            asio::bind_executor(
                strand_,
                [self = this->shared_from_this()](const ErrorCode &ec, const asio::ip::tcp::resolver::results_type &results) {
                    self->onResolve(ec, results);
                }
            )
        );
    }

private:
    std::pair<std::string, std::string> endpointTarget() const
    {
        if (proxyEnabled(options_)) {
            return {options_.proxy->host, options_.proxy->port};
        }
        return {url_.host, url_.port};
    }

    void armTimeout()
    {
        timer_.expires_after(options_.request_timeout);
        timer_.async_wait(asio::bind_executor(strand_, [self = this->shared_from_this()](const ErrorCode &ec) {
            if (!ec) {
                self->timed_out_ = true;
                self->fail("timeout", "[AsioHttpClient] Request timed out");
            }
        }));
    }

    void onResolve(const ErrorCode &ec, const asio::ip::tcp::resolver::results_type &results)
    {
        if (completed_) {
            return;
        }
        if (ec) {
            fail("network_failed", "[AsioHttpClient] Resolve failed: " + ec.message());
            return;
        }

        asio::async_connect(
            stream_.lowest_layer(),
            results,
            asio::bind_executor(
                strand_,
                [self = this->shared_from_this()](const ErrorCode &connect_ec, const asio::ip::tcp::endpoint &) {
                    self->onConnect(connect_ec);
                }
            )
        );
    }

    void onConnect(const ErrorCode &ec)
    {
        if (completed_) {
            return;
        }
        if (ec) {
            fail("network_failed", "[AsioHttpClient] Connect failed: " + ec.message());
            return;
        }

        if constexpr (std::is_same_v<Stream, TlsStream>) {
            if (proxyEnabled(options_)) {
                writeConnectRequest();
                return;
            }
            startTlsHandshake();
        } else {
            writeRequest();
        }
    }

    void writeConnectRequest()
    {
        if constexpr (std::is_same_v<Stream, TlsStream>) {
            connect_request_ = buildConnectRequest(url_, *options_.proxy);
            asio::async_write(
                stream_.next_layer(),
                asio::buffer(connect_request_),
                asio::bind_executor(
                    strand_,
                    [self = this->shared_from_this()](const ErrorCode &ec, std::size_t) {
                        self->onConnectRequestWritten(ec);
                    }
                )
            );
        }
    }

    void onConnectRequestWritten(const ErrorCode &ec)
    {
        if (completed_) {
            return;
        }
        if (ec) {
            fail("network_failed", "[AsioHttpClient] Proxy CONNECT write failed: " + ec.message());
            return;
        }
        readConnectResponse();
    }

    void readConnectResponse()
    {
        if constexpr (std::is_same_v<Stream, TlsStream>) {
            stream_.next_layer().async_read_some(
                asio::buffer(read_buffer_),
                asio::bind_executor(
                    strand_,
                    [self = this->shared_from_this()](const ErrorCode &ec, std::size_t bytes_read) {
                        self->onConnectResponse(ec, bytes_read);
                    }
                )
            );
        }
    }

    void onConnectResponse(const ErrorCode &ec, std::size_t bytes_read)
    {
        if (completed_) {
            return;
        }
        if (ec) {
            fail("network_failed", "[AsioHttpClient] Proxy CONNECT read failed: " + ec.message());
            return;
        }

        proxy_response_.append(read_buffer_.data(), bytes_read);
        const ProxyConnectValidation validation = validateProxyConnectResponse(proxy_response_);
        if (!validation.complete) {
            readConnectResponse();
            return;
        }
        if (!validation.success) {
            fail("network_failed", validation.error);
            return;
        }
        startTlsHandshake();
    }

    void startTlsHandshake()
    {
        if constexpr (std::is_same_v<Stream, TlsStream>) {
            std::string error;
            if (!configureTlsStream(stream_, url_, options_, error)) {
                fail("network_failed", error);
                return;
            }

            stream_.async_handshake(
                ssl::stream_base::client,
                asio::bind_executor(
                    strand_,
                    [self = this->shared_from_this()](const ErrorCode &handshake_ec) {
                        self->onHandshake(handshake_ec);
                    }
                )
            );
        }
    }

    void onHandshake(const ErrorCode &ec)
    {
        if (completed_) {
            return;
        }
        if (ec) {
            fail("network_failed", "[AsioHttpClient] TLS handshake failed: " + ec.message());
            return;
        }
        writeRequest();
    }

    void writeRequest()
    {
        asio::async_write(
            stream_,
            asio::buffer(request_),
            asio::bind_executor(
                strand_,
                [self = this->shared_from_this()](const ErrorCode &ec, std::size_t) {
                    self->onWrite(ec);
                }
            )
        );
    }

    void onWrite(const ErrorCode &ec)
    {
        if (completed_) {
            return;
        }
        if (ec) {
            fail("network_failed", "[AsioHttpClient] Write failed: " + ec.message());
            return;
        }
        readMore();
    }

    void readMore()
    {
        stream_.async_read_some(
            asio::buffer(read_buffer_),
            asio::bind_executor(
                strand_,
                [self = this->shared_from_this()](const ErrorCode &ec, std::size_t bytes_read) {
                    self->onRead(ec, bytes_read);
                }
            )
        );
    }

    void onRead(const ErrorCode &ec, std::size_t bytes_read)
    {
        if (completed_) {
            return;
        }
        if (bytes_read > 0) {
            if (!headers_parsed_) {
                if (!appendHeaderBytes(read_buffer_.data(), bytes_read)) {
                    return;
                }
            } else if (!processBodyBytes(std::string_view(read_buffer_.data(), bytes_read))) {
                return;
            }
        }

        if (completed_) {
            return;
        }
        if (!ec) {
            readMore();
            return;
        }
        if (ec == asio::error::eof || ec == ssl::error::stream_truncated) {
            if (!headers_parsed_) {
                fail("network_failed", "[AsioHttpClient] Response ended before headers completed");
                return;
            }
            onBodyEof();
            return;
        }
        fail(timed_out_ ? "timeout" : "network_failed", "[AsioHttpClient] Read failed: " + ec.message());
    }

    bool appendHeaderBytes(const char *data, std::size_t size)
    {
        header_buffer_.append(data, size);
        if (header_buffer_.size() > kMaxHeaderBytes + options_.max_response_bytes) {
            fail("body_too_large", "[AsioHttpClient] Response is too large before headers completed");
            return false;
        }

        const auto header_end = findHeaderEnd(header_buffer_);
        if (header_end == std::string::npos) {
            if (header_buffer_.size() > kMaxHeaderBytes) {
                fail("network_failed", "[AsioHttpClient] Response header is too large");
                return false;
            }
            return true;
        }

        const std::string headers_part = header_buffer_.substr(0, header_end);
        auto headers = parseStreamingResponseHeaders(headers_part);
        if (!headers) {
            fail("network_failed", "[AsioHttpClient] Response header parse failed");
            return false;
        }
        headers_parsed_ = true;
        result_.status_code = headers->status_code;
        result_.content_type = headers->content_type;
        result_.headers = headers->raw_headers;
        content_length_ = headers->content_length;
        chunked_ = headers->chunked;

        const std::string content_encoding = streamingToLower(headers->content_encoding);
        if (!content_encoding.empty() && content_encoding != "identity") {
            fail("unsupported_content_encoding", "[AsioHttpClient] Unsupported content encoding: " + content_encoding);
            return false;
        }
        if (result_.status_code < 200 || result_.status_code >= 300) {
            completeOk();
            return false;
        }
        if (content_length_ && *content_length_ > options_.max_response_bytes) {
            fail("body_too_large", "[AsioHttpClient] Response body is too large");
            return false;
        }

        HttpStreamInfo info;
        info.status_code = result_.status_code;
        info.content_type = result_.content_type;
        info.headers = result_.headers;
        if (callbacks_.on_response && !callbacks_.on_response(info)) {
            fail("stream_consumer_failed", "[AsioHttpClient] Stream response rejected by consumer");
            return false;
        }

        const std::string body_part = header_buffer_.substr(header_end);
        header_buffer_.clear();
        if (!body_part.empty()) {
            return processBodyBytes(body_part);
        }
        if (content_length_.has_value() && *content_length_ == 0) {
            completeOk();
            return false;
        }
        return true;
    }

    bool processBodyBytes(std::string_view data)
    {
        if (chunked_) {
            chunk_buffer_.append(data.data(), data.size());
            return drainChunkedBody();
        }
        return deliverPlainBody(data);
    }

    bool deliverPlainBody(std::string_view data)
    {
        if (data.empty()) {
            return true;
        }

        std::size_t deliver_size = data.size();
        if (content_length_) {
            if (result_.bytes_received >= *content_length_) {
                completeOk();
                return false;
            }
            const std::uint64_t remaining = *content_length_ - result_.bytes_received;
            deliver_size = static_cast<std::size_t>((std::min<std::uint64_t>)(remaining, data.size()));
        }

        if (!deliverChunk(data.substr(0, deliver_size))) {
            return false;
        }
        if (content_length_ && result_.bytes_received >= *content_length_) {
            completeOk();
            return false;
        }
        return true;
    }

    bool drainChunkedBody()
    {
        for (;;) {
            if (chunk_remaining_ == 0 && !expecting_chunk_crlf_) {
                const auto line_end = chunk_buffer_.find("\r\n");
                if (line_end == std::string::npos) {
                    if (chunk_buffer_.size() > kMaxHeaderBytes) {
                        fail("network_failed", "[AsioHttpClient] Chunk size line is too large");
                        return false;
                    }
                    return true;
                }
                auto chunk_size = parseStreamingChunkSize(std::string_view(chunk_buffer_).substr(0, line_end));
                if (!chunk_size) {
                    fail("network_failed", "[AsioHttpClient] Invalid chunk size");
                    return false;
                }
                chunk_buffer_.erase(0, line_end + 2);
                if (*chunk_size == 0) {
                    completeOk();
                    return false;
                }
                chunk_remaining_ = *chunk_size;
            }

            if (chunk_remaining_ > 0) {
                if (chunk_buffer_.empty()) {
                    return true;
                }
                const std::size_t deliver_size = static_cast<std::size_t>((std::min<std::uint64_t>)(chunk_remaining_, chunk_buffer_.size()));
                if (!deliverChunk(std::string_view(chunk_buffer_.data(), deliver_size))) {
                    return false;
                }
                chunk_buffer_.erase(0, deliver_size);
                chunk_remaining_ -= deliver_size;
                if (chunk_remaining_ > 0) {
                    return true;
                }
                expecting_chunk_crlf_ = true;
            }

            if (expecting_chunk_crlf_) {
                if (chunk_buffer_.size() < 2) {
                    return true;
                }
                if (chunk_buffer_.substr(0, 2) != "\r\n") {
                    fail("network_failed", "[AsioHttpClient] Invalid chunk terminator");
                    return false;
                }
                chunk_buffer_.erase(0, 2);
                expecting_chunk_crlf_ = false;
            }
        }
    }

    bool deliverChunk(std::string_view chunk)
    {
        if (chunk.empty()) {
            return true;
        }
        if (result_.bytes_received + chunk.size() > options_.max_response_bytes) {
            fail("body_too_large", "[AsioHttpClient] Response body is too large");
            return false;
        }
        if (callbacks_.on_chunk && !callbacks_.on_chunk(chunk)) {
            fail("stream_consumer_failed", "[AsioHttpClient] Stream chunk rejected by consumer");
            return false;
        }
        result_.bytes_received += static_cast<std::uint64_t>(chunk.size());
        return true;
    }

    void onBodyEof()
    {
        if (chunked_) {
            fail("network_failed", "[AsioHttpClient] Chunked response ended before final chunk");
            return;
        }
        if (content_length_ && result_.bytes_received != *content_length_) {
            fail("network_failed", "[AsioHttpClient] Response ended before Content-Length was satisfied");
            return;
        }
        completeOk();
    }

    void fail(std::string code, std::string message)
    {
        if (!completed_) {
            std::cerr << message << std::endl;
        }
        result_.ok = false;
        result_.result_code = std::move(code);
        result_.message = std::move(message);
        complete();
    }

    void completeOk()
    {
        result_.ok = true;
        result_.result_code = "ok";
        complete();
    }

    void complete()
    {
        if (completed_) {
            return;
        }
        completed_ = true;

        ErrorCode ignored;
        timer_.cancel();
        stream_.lowest_layer().close(ignored);

        promise_->set_value(std::move(result_));
    }

    IoStrand strand_;                                         // 串行化单个请求的异步回调
    asio::ip::tcp::resolver resolver_;                        // DNS 解析器
    asio::steady_timer timer_;                                // 请求总超时控制
    Stream stream_;                                           // TCP 或 TLS 流
    ParsedUrl url_;                                           // 已解析 URL
    std::string request_;                                     // 完整 HTTP 请求报文
    AsioHttpClient::Options options_;                         // 请求选项快照
    HttpStreamCallbacks callbacks_;                           // 调用方流式回调
    std::shared_ptr<std::promise<HttpStreamResult>> promise_; // 同步接口返回桥接
    std::array<char, kReadBufferSize> read_buffer_ {};        // 单次 socket 读取缓冲区
    std::string connect_request_;                             // HTTPS 代理 CONNECT 请求
    std::string proxy_response_;                              // HTTPS 代理 CONNECT 响应头
    std::string header_buffer_;                               // 响应头暂存区
    std::string chunk_buffer_;                                // chunked 编码暂存区
    std::optional<std::uint64_t> content_length_;             // Content-Length
    std::uint64_t chunk_remaining_ = 0;                       // 当前 chunk 剩余字节
    HttpStreamResult result_;                                 // 当前流式请求结果
    bool headers_parsed_ = false;                             // 是否已解析响应头
    bool chunked_ = false;                                    // 是否使用 chunked 解码
    bool expecting_chunk_crlf_ = false;                       // 当前 chunk 数据后是否等待 CRLF
    bool timed_out_ = false;                                  // 请求是否已超时
    bool completed_ = false;                                  // 防止重复设置 promise
};

} // namespace orchestrator::modules::crawler::infrastructure::http::client::detail
