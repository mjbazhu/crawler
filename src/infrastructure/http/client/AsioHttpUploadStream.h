#pragma once

#include "crawler/infrastructure/http/client/AsioHttpCoroutineRequest.h"

#include <boost/asio/strand.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <memory>
#include <span>
#include <string>
#include <type_traits>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::http::client::detail {

// 主动轮询只关闭本次请求的 DNS/socket；strand 串行化 timer 与连接操作。
template <typename Stream>
void pollUploadCancellation(std::shared_ptr<CoroutineRequestState<Stream>> state, std::shared_ptr<asio::steady_timer> timer, std::weak_ptr<HttpUploadBody> body, std::shared_ptr<bool> cancelled)
{
    timer->expires_after(std::chrono::milliseconds(25));
    timer->async_wait([state, timer, body, cancelled](const ErrorCode &ec) {
        if (ec || state->completed.load()) return;
        // 已取消但尚未投递的 timer 不能延长 reader/文件句柄到函数返回之后。
        const auto input = body.lock();
        if (!input) return;
        bool stop = false;
        try {
            stop = input->cancelled && input->cancelled();
        } catch (...) {
            stop = true;
        }
        if (stop) {
            *cancelled = true;
            state->resolver.cancel();
            ErrorCode ignored;
            state->stream.lowest_layer().close(ignored);
            return;
        }
        pollUploadCancellation(state, timer, body, cancelled);
    });
}

// 任何异常路径都结束本次连接和计时器，保证读回调不在返回后继续使用快照。
template <typename Stream>
struct UploadGuard {
    std::shared_ptr<CoroutineRequestState<Stream>> state; // 本次连接
    std::shared_ptr<asio::steady_timer> cancellation;     // 本次取消检查
    // 协程结束后清除尚未投递的计时器。
    ~UploadGuard()
    {
        cancellation->cancel();
        finishCoroutineRequest(state);
    }
};

// 复用 TCP/TLS/代理连接 helper，固定 64 KiB 请求体缓冲。
template <typename Stream>
asio::awaitable<HttpUploadResult> coUploadStream(ssl::context *tls, ParsedUrl url, std::string head, AsioHttpClient::Options options, HttpUploadBody input)
{
    const auto executor = co_await asio::this_coro::executor;
    auto state = std::make_shared<CoroutineRequestState<Stream>>(executor, tls);
    auto timer = std::make_shared<asio::steady_timer>(executor);
    auto body = std::make_shared<HttpUploadBody>(std::move(input));
    auto cancelled = std::make_shared<bool>(false);
    UploadGuard<Stream> guard {state, timer};
    HttpUploadResult result;
    const auto failure = [&](const char *fallback) {
        result.response.reset();
        result.result_code = *cancelled ? "cancelled" : state->timed_out.load() ? "timeout" :
                                                                                  fallback;
        return result;
    };
    try {
        if (body->cancelled && body->cancelled()) {
            *cancelled = true;
            co_return failure("cancelled");
        }
        armCoroutineTimeout(state, options);
        pollUploadCancellation(state, timer, body, cancelled);
        const auto endpoint = coroutineEndpointTarget(url, options);
        ErrorCode ec;
        const auto resolved = co_await state->resolver.async_resolve(endpoint.first, endpoint.second, asio::redirect_error(asio::use_awaitable, ec));
        if (ec) co_return failure("network_failed");
        co_await asio::async_connect(state->stream.lowest_layer(), resolved, asio::redirect_error(asio::use_awaitable, ec));
        if (ec) co_return failure("network_failed");
        if constexpr (std::is_same_v<Stream, TlsStream>) {
            if (proxyEnabled(options) && !(co_await coConnectProxyTunnel(state, url, options))) co_return failure("proxy_failed");
            if (!(co_await coHandshakeTls(state, url, options))) co_return failure("tls_failed");
        }
        result.request_started = true;
        co_await asio::async_write(state->stream, asio::buffer(head), asio::redirect_error(asio::use_awaitable, ec));
        if (ec) co_return failure("network_failed");
        const auto started = std::chrono::steady_clock::now();
        std::array<char, 64 * 1024> chunk {};
        while (result.bytes_sent < body->size_bytes) {
            if (*cancelled || state->timed_out.load()) co_return failure("cancelled");
            const auto capacity = static_cast<std::size_t>((std::min<std::uint64_t>)(chunk.size(), body->size_bytes - result.bytes_sent));
            const auto count = body->read(std::span<char>(chunk.data(), capacity));
            if (!count || *count == 0 || *count > capacity) co_return failure("upload_source_failed");
            if (body->bytes_per_second) {
                // 先等待再发送；计入 multipart 字节，且不阻塞其他连接的 I/O 线程。
                const auto seconds = static_cast<long double>(result.bytes_sent + *count) / body->bytes_per_second;
                const auto due = started + std::chrono::duration_cast<std::chrono::steady_clock::duration>(std::chrono::duration<long double>(seconds));
                while (std::chrono::steady_clock::now() < due) {
                    if (*cancelled || state->timed_out.load()) co_return failure("cancelled");
                    asio::steady_timer pacing(executor);
                    pacing.expires_at((std::min)(due, std::chrono::steady_clock::now() + std::chrono::milliseconds(25)));
                    co_await pacing.async_wait(asio::redirect_error(asio::use_awaitable, ec));
                    if (ec) co_return failure("network_failed");
                }
            }
            const auto written = co_await asio::async_write(state->stream, asio::buffer(chunk.data(), *count), asio::redirect_error(asio::use_awaitable, ec));
            result.bytes_sent += written;
            if (ec) co_return failure("network_failed");
        }
        const auto end = body->read(std::span<char>(chunk));
        if (!end || *end != 0) co_return failure("upload_source_failed");
        std::string raw;
        for (;;) {
            const auto received = co_await state->stream.async_read_some(asio::buffer(state->read_buffer), asio::redirect_error(asio::use_awaitable, ec));
            std::string ignored;
            if (!appendResponseChunk(raw, state->read_buffer.data(), received, options.max_response_bytes, ignored)) co_return failure("response_too_large");
            if (!ec) continue;
            if (ec != asio::error::eof && ec != ssl::error::stream_truncated) co_return failure("network_failed");
            if (*cancelled || state->timed_out.load()) co_return failure("network_failed");
            result.response = parseHttpResponse(raw, options.max_response_bytes);
            result.result_code = result.response ? "ok" : "invalid_response";
            co_return result;
        }
    } catch (...) {
        co_return failure("upload_failed");
    }
}

} // namespace orchestrator::modules::crawler::infrastructure::http::client::detail
