#pragma once

#include "crawler/infrastructure/http/client/AsioHttpClient.h"
#include "crawler/infrastructure/http/client/AsioHttpConnectionHelpers.h"
#include "crawler/infrastructure/http/client/AsioHttpRequestBuilder.h"
#include "crawler/infrastructure/http/client/AsioHttpResponseParser.h"
#include "crawler/infrastructure/http/client/AsioHttpSessionTypes.h"
#include "crawler/infrastructure/http/client/AsioHttpUrl.h"

#include <boost/asio/redirect_error.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>

#include <array>
#include <atomic>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::http::client::detail {

template <typename Stream>
Stream makeCoroutineStream(const asio::any_io_executor &executor, ssl::context *ssl_context);

template <>
inline TcpSocket makeCoroutineStream<TcpSocket>(const asio::any_io_executor &executor, ssl::context *)
{
    return TcpSocket(executor);
}

template <>
inline TlsStream makeCoroutineStream<TlsStream>(const asio::any_io_executor &executor, ssl::context *ssl_context)
{
    return TlsStream(executor, *ssl_context);
}

// 协程请求状态，绑定解析器、超时器、流和读取缓冲。
template <typename Stream>
struct CoroutineRequestState {
    asio::ip::tcp::resolver resolver;                  // DNS 解析器
    asio::steady_timer timer;                          // 请求总超时控制
    Stream stream;                                     // TCP 或 TLS 流
    std::array<char, kReadBufferSize> read_buffer {};  // 单次读取缓冲区
    std::atomic_bool completed {false};                // 请求是否已结束
    std::atomic_bool timed_out {false};                // 是否被超时取消

    CoroutineRequestState(const asio::any_io_executor &executor, ssl::context *ssl_context) : resolver(executor),
                                                                                              timer(executor),
                                                                                              stream(makeCoroutineStream<Stream>(executor, ssl_context))
    {}
};

// 启动协程请求总超时；超时后取消 DNS 并关闭 socket。
template <typename Stream>
void armCoroutineTimeout(
    const std::shared_ptr<CoroutineRequestState<Stream>> &state,
    const AsioHttpClient::Options &options
)
{
    state->timer.expires_after(options.request_timeout);
    state->timer.async_wait([state](const ErrorCode &ec) {
        if (!ec && !state->completed.load()) {
            state->timed_out.store(true);
            ErrorCode ignored;
            state->resolver.cancel();
            state->stream.lowest_layer().close(ignored);
        }
    });
}

// 返回实际连接端点；启用代理时连接代理，否则连接目标站点。
inline std::pair<std::string, std::string> coroutineEndpointTarget(
    const ParsedUrl &url,
    const AsioHttpClient::Options &options
)
{
    if (proxyEnabled(options)) {
        return {options.proxy->host, options.proxy->port};
    }
    return {url.host, url.port};
}

// 结束协程请求并释放底层 I/O 资源。
template <typename Stream>
void finishCoroutineRequest(const std::shared_ptr<CoroutineRequestState<Stream>> &state)
{
    state->completed.store(true);
    ErrorCode ignored;
    state->timer.cancel();
    state->stream.lowest_layer().close(ignored);
}

// 对 HTTPS 代理请求建立 CONNECT 隧道。
template <typename Stream>
asio::awaitable<bool> coConnectProxyTunnel(
    const std::shared_ptr<CoroutineRequestState<Stream>> &state,
    const ParsedUrl &url,
    const AsioHttpClient::Options &options
)
{
    static_assert(std::is_same_v<Stream, TlsStream>, "proxy tunnel is only needed before TLS handshake");

    const std::string connect_request = buildConnectRequest(url, *options.proxy);
    ErrorCode ec;
    co_await asio::async_write(
        state->stream.next_layer(),
        asio::buffer(connect_request),
        asio::redirect_error(asio::use_awaitable, ec)
    );
    if (ec) {
        std::cerr << "[AsioHttpClient] Proxy CONNECT write failed: " << ec.message() << std::endl;
        co_return false;
    }

    std::string proxy_response;
    for (;;) {
        const std::size_t bytes_read = co_await state->stream.next_layer().async_read_some(
            asio::buffer(state->read_buffer),
            asio::redirect_error(asio::use_awaitable, ec)
        );
        if (bytes_read > 0) {
            proxy_response.append(state->read_buffer.data(), bytes_read);
        }

        const ProxyConnectValidation validation = validateProxyConnectResponse(proxy_response);
        if (validation.complete && validation.success) {
            co_return true;
        }
        if (validation.complete) {
            std::cerr << validation.error << std::endl;
            co_return false;
        }
        if (ec) {
            std::cerr << "[AsioHttpClient] Proxy CONNECT read failed: " << ec.message() << std::endl;
            co_return false;
        }
    }
}

// 完成 TLS SNI、证书校验策略和握手。
template <typename Stream>
asio::awaitable<bool> coHandshakeTls(
    const std::shared_ptr<CoroutineRequestState<Stream>> &state,
    const ParsedUrl &url,
    const AsioHttpClient::Options &options
)
{
    static_assert(std::is_same_v<Stream, TlsStream>, "TLS handshake requires a TLS stream");

    std::string tls_error;
    if (!configureTlsStream(state->stream, url, options, tls_error)) {
        std::cerr << tls_error << std::endl;
        co_return false;
    }

    ErrorCode ec;
    co_await state->stream.async_handshake(
        ssl::stream_base::client,
        asio::redirect_error(asio::use_awaitable, ec)
    );
    if (ec) {
        std::cerr << "[AsioHttpClient] TLS handshake failed: " << ec.message() << std::endl;
        co_return false;
    }
    co_return true;
}

// 在指定 TCP/TLS 流类型上执行一次完整 HTTP 请求。
template <typename Stream>
asio::awaitable<std::optional<HttpResponse>> coRequestOnStream(
    ssl::context *ssl_context,
    ParsedUrl url,
    std::string request,
    AsioHttpClient::Options options
)
{
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
        std::cerr << "[AsioHttpClient] Resolve failed: " << ec.message() << std::endl;
        finishCoroutineRequest(state);
        co_return std::nullopt;
    }

    co_await asio::async_connect(
        state->stream.lowest_layer(),
        results,
        asio::redirect_error(asio::use_awaitable, ec)
    );
    if (ec) {
        std::cerr << "[AsioHttpClient] Connect failed: " << ec.message() << std::endl;
        finishCoroutineRequest(state);
        co_return std::nullopt;
    }

    if constexpr (std::is_same_v<Stream, TlsStream>) {
        if (proxyEnabled(options)) {
            const bool connected = co_await coConnectProxyTunnel(state, url, options);
            if (!connected) {
                finishCoroutineRequest(state);
                co_return std::nullopt;
            }
        }

        const bool handshaked = co_await coHandshakeTls(state, url, options);
        if (!handshaked) {
            finishCoroutineRequest(state);
            co_return std::nullopt;
        }
    }

    co_await asio::async_write(
        state->stream,
        asio::buffer(request),
        asio::redirect_error(asio::use_awaitable, ec)
    );
    if (ec) {
        std::cerr << "[AsioHttpClient] Write failed: " << ec.message() << std::endl;
        finishCoroutineRequest(state);
        co_return std::nullopt;
    }

    std::string raw_response;
    for (;;) {
        const std::size_t bytes_read = co_await state->stream.async_read_some(
            asio::buffer(state->read_buffer),
            asio::redirect_error(asio::use_awaitable, ec)
        );

        if (bytes_read > 0) {
            std::string size_error;
            if (!appendResponseChunk(raw_response, state->read_buffer.data(), bytes_read, options.max_response_bytes, size_error)) {
                std::cerr << size_error << std::endl;
                finishCoroutineRequest(state);
                co_return std::nullopt;
            }
        }

        if (!ec) {
            continue;
        }
        if (ec == asio::error::eof || ec == ssl::error::stream_truncated) {
            std::optional<HttpResponse> response = parseHttpResponse(raw_response, options.max_response_bytes);
            finishCoroutineRequest(state);
            co_return response;
        }

        if (state->timed_out.load()) {
            std::cerr << "[AsioHttpClient] Request timed out" << std::endl;
        } else {
            std::cerr << "[AsioHttpClient] Read failed: " << ec.message() << std::endl;
        }
        finishCoroutineRequest(state);
        co_return std::nullopt;
    }
}

} // namespace orchestrator::modules::crawler::infrastructure::http::client::detail
