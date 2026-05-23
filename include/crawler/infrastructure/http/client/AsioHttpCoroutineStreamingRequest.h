#pragma once

#include "crawler/infrastructure/http/client/AsioHttpCoroutineRequest.h"
#include "crawler/infrastructure/http/client/AsioHttpStreamingRequest.h"

#include <boost/asio/redirect_error.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <algorithm>
#include <optional>
#include <string>
#include <string_view>
#include <type_traits>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::http::client::detail {

// 协程流式响应 body 处理器，复用 callback 版的 header/chunk 解析规则。
class CoroutineStreamBodyProcessor {
public:
    CoroutineStreamBodyProcessor(AsioHttpClient::Options options, AsioHttpClient::AwaitableStreamCallbacks callbacks) : options_(options),
                                                                                                                         callbacks_(std::move(callbacks))
    {}

    // 响应头解析完成后初始化状态并通知下游。
    asio::awaitable<bool> onHeaders(StreamingResponseHeaders headers)
    {
        result_.status_code = headers.status_code;
        result_.content_type = headers.content_type;
        result_.headers = std::move(headers.raw_headers);
        content_length_ = headers.content_length;
        chunked_ = headers.chunked;

        const std::string content_encoding = streamingToLower(headers.content_encoding);
        if (!content_encoding.empty() && content_encoding != "identity") {
            fail("unsupported_content_encoding", "[AsioHttpClient] Unsupported content encoding");
            co_return false;
        }
        if (result_.status_code < 200 || result_.status_code >= 300) {
            completeOk();
            co_return false;
        }
        if (content_length_ && *content_length_ > options_.max_response_bytes) {
            fail("body_too_large", "[AsioHttpClient] Response body is too large");
            co_return false;
        }

        HttpStreamInfo info;
        info.status_code = result_.status_code;
        info.content_type = result_.content_type;
        info.headers = result_.headers;
        if (callbacks_.on_response && !(co_await callbacks_.on_response(info))) {
            fail("stream_consumer_failed", "[AsioHttpClient] Stream response rejected by consumer");
            co_return false;
        }

        if (content_length_.has_value() && *content_length_ == 0) {
            completeOk();
            co_return false;
        }
        co_return true;
    }

    // 处理一个 body 字节片段。
    asio::awaitable<bool> processBodyBytes(std::string_view data)
    {
        if (completed_) {
            co_return false;
        }
        if (chunked_) {
            chunk_buffer_.append(data.data(), data.size());
            co_return co_await drainChunkedBody();
        }
        co_return co_await deliverPlainBody(data);
    }

    // EOF 时完成 body 校验。
    bool onBodyEof()
    {
        if (completed_) {
            return true;
        }
        if (chunked_) {
            fail("network_failed", "[AsioHttpClient] Chunked response ended before final chunk");
            return false;
        }
        if (content_length_ && result_.bytes_received != *content_length_) {
            fail("network_failed", "[AsioHttpClient] Response ended before Content-Length was satisfied");
            return false;
        }
        completeOk();
        return true;
    }

    HttpStreamResult result() &&
    {
        return std::move(result_);
    }

    bool completed() const
    {
        return completed_;
    }

private:
    asio::awaitable<bool> deliverPlainBody(std::string_view data)
    {
        if (data.empty()) {
            co_return true;
        }

        std::size_t deliver_size = data.size();
        if (content_length_) {
            if (result_.bytes_received >= *content_length_) {
                completeOk();
                co_return false;
            }
            const std::uint64_t remaining = *content_length_ - result_.bytes_received;
            deliver_size = static_cast<std::size_t>((std::min<std::uint64_t>)(remaining, data.size()));
        }

        if (!(co_await deliverChunk(data.substr(0, deliver_size)))) {
            co_return false;
        }
        if (content_length_ && result_.bytes_received >= *content_length_) {
            completeOk();
            co_return false;
        }
        co_return true;
    }

    asio::awaitable<bool> drainChunkedBody()
    {
        for (;;) {
            if (chunk_remaining_ == 0 && !expecting_chunk_crlf_) {
                const auto line_end = chunk_buffer_.find("\r\n");
                if (line_end == std::string::npos) {
                    if (chunk_buffer_.size() > kMaxHeaderBytes) {
                        fail("network_failed", "[AsioHttpClient] Chunk size line is too large");
                        co_return false;
                    }
                    co_return true;
                }
                auto chunk_size = parseStreamingChunkSize(std::string_view(chunk_buffer_).substr(0, line_end));
                if (!chunk_size) {
                    fail("network_failed", "[AsioHttpClient] Invalid chunk size");
                    co_return false;
                }
                chunk_buffer_.erase(0, line_end + 2);
                if (*chunk_size == 0) {
                    completeOk();
                    co_return false;
                }
                chunk_remaining_ = *chunk_size;
            }

            if (chunk_remaining_ > 0) {
                if (chunk_buffer_.empty()) {
                    co_return true;
                }
                const std::size_t deliver_size = static_cast<std::size_t>((std::min<std::uint64_t>)(chunk_remaining_, chunk_buffer_.size()));
                if (!(co_await deliverChunk(std::string_view(chunk_buffer_.data(), deliver_size)))) {
                    co_return false;
                }
                chunk_buffer_.erase(0, deliver_size);
                chunk_remaining_ -= deliver_size;
                if (chunk_remaining_ > 0) {
                    co_return true;
                }
                expecting_chunk_crlf_ = true;
            }

            if (expecting_chunk_crlf_) {
                if (chunk_buffer_.size() < 2) {
                    co_return true;
                }
                if (chunk_buffer_.substr(0, 2) != "\r\n") {
                    fail("network_failed", "[AsioHttpClient] Invalid chunk terminator");
                    co_return false;
                }
                chunk_buffer_.erase(0, 2);
                expecting_chunk_crlf_ = false;
            }
        }
    }

    asio::awaitable<bool> deliverChunk(std::string_view chunk)
    {
        if (chunk.empty()) {
            co_return true;
        }
        if (result_.bytes_received + chunk.size() > options_.max_response_bytes) {
            fail("body_too_large", "[AsioHttpClient] Response body is too large");
            co_return false;
        }
        if (callbacks_.on_chunk && !(co_await callbacks_.on_chunk(chunk))) {
            fail("stream_consumer_failed", "[AsioHttpClient] Stream chunk rejected by consumer");
            co_return false;
        }
        result_.bytes_received += static_cast<std::uint64_t>(chunk.size());
        co_return true;
    }

    void fail(std::string code, std::string message)
    {
        result_.ok = false;
        result_.result_code = std::move(code);
        result_.message = std::move(message);
        completed_ = true;
    }

    void completeOk()
    {
        result_.ok = true;
        result_.result_code = "ok";
        completed_ = true;
    }

    AsioHttpClient::Options options_;                         // 请求选项
    AsioHttpClient::AwaitableStreamCallbacks callbacks_;      // 下游异步回调
    std::optional<std::uint64_t> content_length_;             // Content-Length
    std::string chunk_buffer_;                                // chunked 解码暂存
    std::uint64_t chunk_remaining_ = 0;                       // 当前 chunk 剩余字节
    HttpStreamResult result_;                                 // 流式请求结果
    bool chunked_ = false;                                    // 是否 chunked 编码
    bool expecting_chunk_crlf_ = false;                       // 是否等待 chunk 终止 CRLF
    bool completed_ = false;                                  // 是否已完成
};

// 在指定 TCP/TLS 流类型上执行协程式流式 HTTP GET。
template <typename Stream>
asio::awaitable<HttpStreamResult> coStreamRequestOnStream(
    ssl::context *ssl_context,
    ParsedUrl url,
    std::string request,
    AsioHttpClient::Options options,
    AsioHttpClient::AwaitableStreamCallbacks callbacks
)
{
    HttpStreamResult result;
    asio::any_io_executor executor = co_await asio::this_coro::executor;
    std::shared_ptr<CoroutineRequestState<Stream>> state = std::make_shared<CoroutineRequestState<Stream>>(executor, ssl_context);
    armCoroutineTimeout(state, options);

    const std::pair<std::string, std::string> endpoint = coroutineEndpointTarget(url, options);
    ErrorCode ec;
    asio::ip::tcp::resolver::results_type results = co_await state->resolver.async_resolve(
        endpoint.first,
        endpoint.second,
        asio::redirect_error(asio::use_awaitable, ec)
    );
    if (ec) {
        result.result_code = "network_failed";
        result.message = "[AsioHttpClient] Resolve failed";
        finishCoroutineRequest(state);
        co_return result;
    }

    co_await asio::async_connect(
        state->stream.lowest_layer(),
        results,
        asio::redirect_error(asio::use_awaitable, ec)
    );
    if (ec) {
        result.result_code = "network_failed";
        result.message = "[AsioHttpClient] Connect failed";
        finishCoroutineRequest(state);
        co_return result;
    }

    if constexpr (std::is_same_v<Stream, TlsStream>) {
        if (proxyEnabled(options)) {
            const bool connected = co_await coConnectProxyTunnel(state, url, options);
            if (!connected) {
                result.result_code = "network_failed";
                result.message = "[AsioHttpClient] Proxy CONNECT failed";
                finishCoroutineRequest(state);
                co_return result;
            }
        }

        const bool handshaked = co_await coHandshakeTls(state, url, options);
        if (!handshaked) {
            result.result_code = "network_failed";
            result.message = "[AsioHttpClient] TLS handshake failed";
            finishCoroutineRequest(state);
            co_return result;
        }
    }

    co_await asio::async_write(
        state->stream,
        asio::buffer(request),
        asio::redirect_error(asio::use_awaitable, ec)
    );
    if (ec) {
        result.result_code = "network_failed";
        result.message = "[AsioHttpClient] Write failed";
        finishCoroutineRequest(state);
        co_return result;
    }

    CoroutineStreamBodyProcessor processor(options, std::move(callbacks));
    std::string header_buffer;
    bool headers_parsed = false;

    for (;;) {
        const std::size_t bytes_read = co_await state->stream.async_read_some(
            asio::buffer(state->read_buffer),
            asio::redirect_error(asio::use_awaitable, ec)
        );

        if (bytes_read > 0) {
            std::string_view read_chunk(state->read_buffer.data(), bytes_read);
            if (!headers_parsed) {
                header_buffer.append(read_chunk.data(), read_chunk.size());
                if (header_buffer.size() > kMaxHeaderBytes + options.max_response_bytes) {
                    result.result_code = "body_too_large";
                    result.message = "[AsioHttpClient] Response is too large before headers completed";
                    finishCoroutineRequest(state);
                    co_return result;
                }

                const auto header_end = findHeaderEnd(header_buffer);
                if (header_end == std::string::npos) {
                    if (header_buffer.size() > kMaxHeaderBytes) {
                        result.result_code = "network_failed";
                        result.message = "[AsioHttpClient] Response header is too large";
                        finishCoroutineRequest(state);
                        co_return result;
                    }
                } else {
                    const std::string headers_part = header_buffer.substr(0, header_end);
                    auto headers = parseStreamingResponseHeaders(headers_part);
                    if (!headers) {
                        result.result_code = "network_failed";
                        result.message = "[AsioHttpClient] Response header parse failed";
                        finishCoroutineRequest(state);
                        co_return result;
                    }
                    headers_parsed = true;
                    if (!(co_await processor.onHeaders(std::move(*headers)))) {
                        finishCoroutineRequest(state);
                        co_return std::move(processor).result();
                    }

                    const std::string body_part = header_buffer.substr(header_end);
                    header_buffer.clear();
                    if (!body_part.empty() && !(co_await processor.processBodyBytes(body_part))) {
                        finishCoroutineRequest(state);
                        co_return std::move(processor).result();
                    }
                }
            } else if (!(co_await processor.processBodyBytes(read_chunk))) {
                finishCoroutineRequest(state);
                co_return std::move(processor).result();
            }
        }

        if (processor.completed()) {
            finishCoroutineRequest(state);
            co_return std::move(processor).result();
        }
        if (!ec) {
            continue;
        }
        if (ec == asio::error::eof || ec == ssl::error::stream_truncated) {
            if (!headers_parsed) {
                result.result_code = "network_failed";
                result.message = "[AsioHttpClient] Response ended before headers completed";
                finishCoroutineRequest(state);
                co_return result;
            }
            processor.onBodyEof();
            finishCoroutineRequest(state);
            co_return std::move(processor).result();
        }

        result.result_code = state->timed_out.load() ? "timeout" : "network_failed";
        result.message = state->timed_out.load() ? "[AsioHttpClient] Request timed out" : "[AsioHttpClient] Read failed";
        finishCoroutineRequest(state);
        co_return result;
    }
}

} // namespace orchestrator::modules::crawler::infrastructure::http::client::detail
