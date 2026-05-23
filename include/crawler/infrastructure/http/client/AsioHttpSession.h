#pragma once

#include "crawler/infrastructure/http/client/AsioHttpClient.h"
#include "crawler/infrastructure/http/client/AsioHttpConnectionHelpers.h"
#include "crawler/infrastructure/http/client/AsioHttpRequestBuilder.h"
#include "crawler/infrastructure/http/client/AsioHttpResponseParser.h"
#include "crawler/infrastructure/http/client/AsioHttpSessionTypes.h"
#include "crawler/infrastructure/http/client/AsioHttpUrl.h"

#include <array>
#include <future>
#include <iostream>
#include <memory>
#include <optional>
#include <string>
#include <type_traits>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::http::client::detail {

template <typename Stream>
Stream makeSessionStream(IoStrand &strand, ssl::context *ssl_context);

template <>
inline TcpSocket makeSessionStream<TcpSocket>(IoStrand &strand, ssl::context *)
{
    return TcpSocket(strand);
}

template <>
inline TlsStream makeSessionStream<TlsStream>(IoStrand &strand, ssl::context *ssl_context)
{
    return TlsStream(strand, *ssl_context);
}

// 基于回调的单次 HTTP 请求会话，负责 DNS、连接、TLS、读写和超时。
template <typename Stream>
class HttpSession : public std::enable_shared_from_this<HttpSession<Stream>> {
public:
    HttpSession(
        asio::io_context &io_context,
        ssl::context *ssl_context,
        ParsedUrl url,
        std::string request,
        AsioHttpClient::Options options,
        std::shared_ptr<std::promise<std::optional<HttpResponse>>> promise
    ) : strand_(asio::make_strand(io_context)),
        resolver_(strand_),
        timer_(strand_),
        stream_(makeSessionStream<Stream>(strand_, ssl_context)),
        url_(std::move(url)),
        request_(std::move(request)),
        options_(options),
        promise_(std::move(promise))
    {}

    // 启动完整请求生命周期。
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
                self->fail("[AsioHttpClient] Request timed out");
            }
        }));
    }

    void onResolve(const ErrorCode &ec, const asio::ip::tcp::resolver::results_type &results)
    {
        if (ec) {
            fail("[AsioHttpClient] Resolve failed: " + ec.message());
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
        if (ec) {
            fail("[AsioHttpClient] Connect failed: " + ec.message());
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
        if (ec) {
            fail("[AsioHttpClient] Proxy CONNECT write failed: " + ec.message());
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
        if (ec) {
            fail("[AsioHttpClient] Proxy CONNECT read failed: " + ec.message());
            return;
        }

        proxy_response_.append(read_buffer_.data(), bytes_read);
        const ProxyConnectValidation validation = validateProxyConnectResponse(proxy_response_);
        if (!validation.complete) {
            readConnectResponse();
            return;
        }
        if (!validation.success) {
            fail(validation.error);
            return;
        }

        startTlsHandshake();
    }

    void startTlsHandshake()
    {
        if constexpr (std::is_same_v<Stream, TlsStream>) {
            std::string error;
            if (!configureTlsStream(stream_, url_, options_, error)) {
                fail(error);
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
        if (ec) {
            fail("[AsioHttpClient] TLS handshake failed: " + ec.message());
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
        if (ec) {
            fail("[AsioHttpClient] Write failed: " + ec.message());
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
        if (bytes_read > 0) {
            std::string error;
            if (!appendResponseChunk(raw_response_, read_buffer_.data(), bytes_read, options_.max_response_bytes, error)) {
                fail(error);
                return;
            }
        }

        if (!ec) {
            readMore();
            return;
        }

        if (ec == asio::error::eof || ec == ssl::error::stream_truncated) {
            complete(parseHttpResponse(raw_response_, options_.max_response_bytes));
            return;
        }

        fail("[AsioHttpClient] Read failed: " + ec.message());
    }

    void fail(const std::string &message)
    {
        std::cerr << message << std::endl;
        complete(std::nullopt);
    }

    void complete(std::optional<HttpResponse> response)
    {
        if (completed_) {
            return;
        }
        completed_ = true;

        ErrorCode ignored;
        timer_.cancel();
        stream_.lowest_layer().close(ignored);

        promise_->set_value(std::move(response));
    }

    IoStrand strand_;                                                    // 串行化单个请求的异步回调
    asio::ip::tcp::resolver resolver_;                                   // DNS 解析器
    asio::steady_timer timer_;                                           // 请求总超时控制
    Stream stream_;                                                      // TCP 或 TLS 流
    ParsedUrl url_;                                                      // 已解析 URL
    std::string request_;                                                // 完整 HTTP 请求报文
    AsioHttpClient::Options options_;                                    // 请求选项快照
    std::shared_ptr<std::promise<std::optional<HttpResponse>>> promise_; // 同步接口返回桥接
    std::array<char, kReadBufferSize> read_buffer_ {};                   // 单次读取缓冲区
    std::string connect_request_;                                        // HTTPS 代理 CONNECT 请求
    std::string proxy_response_;                                         // HTTPS 代理 CONNECT 响应头
    std::string raw_response_;                                           // 完整原始响应
    bool completed_ = false;                                             // 防止重复设置 promise
};

} // namespace orchestrator::modules::crawler::infrastructure::http::client::detail
