#include "crawler/infrastructure/http/client/AsioHttpClient.h"

#include "crawler/infrastructure/http/client/AsioHttpCoroutineRequest.h"
#include "crawler/infrastructure/http/client/AsioHttpCoroutineStreamingRequest.h"
#include "crawler/infrastructure/http/client/AsioHttpRequestBuilder.h"
#include "crawler/infrastructure/http/client/AsioHttpSession.h"
#include "crawler/infrastructure/http/client/AsioHttpStreamingRequest.h"
#include "crawler/infrastructure/http/client/AsioHttpUrl.h"

#include <utility>
#include <boost/asio.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/ssl.hpp>
#include <future>
#include <iostream>
#include <limits>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <thread>

namespace orchestrator::modules::crawler::infrastructure::http::client {

namespace {

namespace asio = boost::asio;
namespace ssl = boost::asio::ssl;
using detail::ParsedUrl;
using detail::ErrorCode;
using detail::HttpSession;
using detail::HttpStreamingSession;
using detail::TcpSocket;
using detail::TlsStream;
using detail::buildRequest;
using detail::coRequestOnStream;
using detail::coStreamRequestOnStream;
using detail::parseUrl;
using detail::proxyEnabled;

} // namespace

class AsioHttpClient::Impl {
public:
    explicit Impl(AsioHttpClient::Options options) : options_(normalizeOptions(options)),
                                                     ssl_context_(ssl::context::tls_client)
    {
        ErrorCode ec;
        ssl_context_.set_default_verify_paths(ec);
        if (ec && options_.verify_tls) {
            std::cerr << "[AsioHttpClient] Failed to load default CA paths: " << ec.message() << std::endl;
        }

        work_guard_ = std::make_unique<asio::executor_work_guard<asio::io_context::executor_type>>(asio::make_work_guard(io_context_));
        workers_.reserve(static_cast<std::size_t>(options_.io_threads));
        for (int i = 0; i < options_.io_threads; ++i) {
            workers_.emplace_back([this] {
                io_context_.run();
            });
        }
    }

    ~Impl()
    {
        work_guard_.reset();
        io_context_.stop();
        for (std::thread &worker : workers_) {
            if (worker.joinable()) {
                worker.join();
            }
        }
    }

    std::optional<HttpResponse> request(
        std::string_view method,
        std::string_view url,
        std::string_view body,
        const std::vector<std::string> &headers,
        HttpRequestOptions request_options = {}
    )
    {
        std::optional<ParsedUrl> parsed_url = parseUrl(url);
        if (!parsed_url) {
            std::cerr << "[AsioHttpClient] Unsupported or invalid URL: " << url << std::endl;
            return std::nullopt;
        }

        std::shared_ptr<std::promise<std::optional<HttpResponse>>> promise = std::make_shared<std::promise<std::optional<HttpResponse>>>();
        std::future<std::optional<HttpResponse>> future = promise->get_future();
        AsioHttpClient::Options effective_options = options_;
        if (request_options.request_timeout.count() > 0) {
            effective_options.request_timeout = request_options.request_timeout;
        }
        if (request_options.max_response_bytes > 0) {
            effective_options.max_response_bytes = request_options.max_response_bytes;
        }

        const AsioHttpClient::ProxyOptions *proxy = proxyEnabled(effective_options) ? &*effective_options.proxy : nullptr;
        const std::string request_text = buildRequest(method, *parsed_url, body, headers, proxy);

        asio::post(io_context_, [this, parsed_url = std::move(*parsed_url), request_text, promise, effective_options]() mutable {
            if (parsed_url.tls) {
                std::make_shared<HttpSession<TlsStream>>(
                    io_context_,
                    &ssl_context_,
                    std::move(parsed_url),
                    request_text,
                    effective_options,
                    promise
                )
                    ->start();
            } else {
                std::make_shared<HttpSession<TcpSocket>>(
                    io_context_,
                    nullptr,
                    std::move(parsed_url),
                    request_text,
                    effective_options,
                    promise
                )
                    ->start();
            }
        });

        return future.get();
    }

    HttpStreamResult streamGet(
        std::string_view url,
        const std::vector<std::string> &headers,
        HttpRequestOptions request_options,
        HttpStreamCallbacks callbacks
    )
    {
        HttpStreamResult failure;
        std::optional<ParsedUrl> parsed_url = parseUrl(url);
        if (!parsed_url) {
            failure.result_code = "invalid_url";
            failure.message = "[AsioHttpClient] Unsupported or invalid URL";
            std::cerr << failure.message << ": " << url << std::endl;
            return failure;
        }

        AsioHttpClient::Options effective_options = options_;
        if (request_options.request_timeout.count() > 0) {
            effective_options.request_timeout = request_options.request_timeout;
        }
        if (request_options.max_response_bytes > 0) {
            effective_options.max_response_bytes = request_options.max_response_bytes;
        }

        const AsioHttpClient::ProxyOptions *proxy = proxyEnabled(effective_options) ? &*effective_options.proxy : nullptr;
        const std::string request_text = buildRequest("GET", *parsed_url, "", headers, proxy);
        auto promise = std::make_shared<std::promise<HttpStreamResult>>();
        auto future = promise->get_future();

        asio::post(io_context_, [this, parsed_url = std::move(*parsed_url), request_text, effective_options, callbacks = std::move(callbacks), promise]() mutable {
            if (parsed_url.tls) {
                std::make_shared<HttpStreamingSession<TlsStream>>(
                    io_context_,
                    &ssl_context_,
                    std::move(parsed_url),
                    request_text,
                    effective_options,
                    std::move(callbacks),
                    promise
                )
                    ->start();
            } else {
                std::make_shared<HttpStreamingSession<TcpSocket>>(
                    io_context_,
                    nullptr,
                    std::move(parsed_url),
                    request_text,
                    effective_options,
                    std::move(callbacks),
                    promise
                )
                    ->start();
            }
        });

        return future.get();
    }

    asio::any_io_executor executor()
    {
        return asio::make_strand(io_context_);
    }

    asio::awaitable<std::optional<HttpResponse>> coRequest(
        std::string method,
        std::string url,
        std::string body,
        std::vector<std::string> headers
    )
    {
        std::optional<ParsedUrl> parsed_url = parseUrl(url);
        if (!parsed_url) {
            std::cerr << "[AsioHttpClient] Unsupported or invalid URL: " << url << std::endl;
            co_return std::nullopt;
        }

        const AsioHttpClient::ProxyOptions *proxy = proxyEnabled(options_) ? &*options_.proxy : nullptr;
        std::string request_text = buildRequest(method, *parsed_url, body, headers, proxy);

        if (parsed_url->tls) {
            co_return co_await coRequestOnStream<TlsStream>(
                &ssl_context_,
                std::move(*parsed_url),
                std::move(request_text),
                options_
            );
        }

        co_return co_await coRequestOnStream<TcpSocket>(
            nullptr,
            std::move(*parsed_url),
            std::move(request_text),
            options_
        );
    }

    asio::awaitable<HttpStreamResult> coStreamGet(
        std::string url,
        std::vector<std::string> headers,
        HttpRequestOptions request_options,
        AsioHttpClient::AwaitableStreamCallbacks callbacks
    )
    {
        HttpStreamResult failure;
        std::optional<ParsedUrl> parsed_url = parseUrl(url);
        if (!parsed_url) {
            failure.result_code = "invalid_url";
            failure.message = "[AsioHttpClient] Unsupported or invalid URL";
            co_return failure;
        }

        AsioHttpClient::Options effective_options = options_;
        if (request_options.request_timeout.count() > 0) {
            effective_options.request_timeout = request_options.request_timeout;
        }
        if (request_options.max_response_bytes > 0) {
            effective_options.max_response_bytes = request_options.max_response_bytes;
        }

        const AsioHttpClient::ProxyOptions *proxy = proxyEnabled(effective_options) ? &*effective_options.proxy : nullptr;
        std::string request_text = buildRequest("GET", *parsed_url, "", headers, proxy);

        if (parsed_url->tls) {
            co_return co_await coStreamRequestOnStream<TlsStream>(
                &ssl_context_,
                std::move(*parsed_url),
                std::move(request_text),
                effective_options,
                std::move(callbacks)
            );
        }

        co_return co_await coStreamRequestOnStream<TcpSocket>(
            nullptr,
            std::move(*parsed_url),
            std::move(request_text),
            effective_options,
            std::move(callbacks)
        );
    }

    void spawnRequest(
        std::string method,
        std::string url,
        std::string body,
        std::vector<std::string> headers,
        AsioHttpClient::ResponseCallback callback
    )
    {
        asio::co_spawn(
            executor(),
            coRequest(std::move(method), std::move(url), std::move(body), std::move(headers)),
            [callback = std::move(callback)](
                std::exception_ptr error,
                std::optional<HttpResponse> response
            ) mutable {
                if (error) {
                    try {
                        std::rethrow_exception(error);
                    } catch (const std::exception &e) {
                        std::cerr << "[AsioHttpClient] Async request failed: " << e.what() << std::endl;
                    } catch (...) {
                        std::cerr << "[AsioHttpClient] Async request failed with unknown error" << std::endl;
                    }
                    if (callback) {
                        callback(std::nullopt);
                    }
                    return;
                }

                if (callback) {
                    callback(std::move(response));
                }
            }
        );
    }

    void spawnStreamRequest(
        std::string url,
        std::vector<std::string> headers,
        HttpRequestOptions options,
        AsioHttpClient::AwaitableStreamCallbacks callbacks,
        AsioHttpClient::StreamCallback callback
    )
    {
        asio::co_spawn(
            executor(),
            coStreamGet(std::move(url), std::move(headers), options, std::move(callbacks)),
            [callback = std::move(callback)](
                std::exception_ptr error,
                HttpStreamResult result
            ) mutable {
                if (error) {
                    try {
                        std::rethrow_exception(error);
                    } catch (const std::exception &e) {
                        std::cerr << "[AsioHttpClient] Async stream request failed: " << e.what() << std::endl;
                    } catch (...) {
                        std::cerr << "[AsioHttpClient] Async stream request failed with unknown error" << std::endl;
                    }
                    result.ok = false;
                    result.result_code = "network_failed";
                    result.message = "async stream request failed";
                }
                if (callback) {
                    callback(std::move(result));
                }
            }
        );
    }

private:
    static AsioHttpClient::Options normalizeOptions(AsioHttpClient::Options options)
    {
        if (options.io_threads <= 0) {
            options.io_threads = 1;
        }
        if (options.request_timeout.count() <= 0) {
            options.request_timeout = std::chrono::milliseconds(30000);
        }
        if (options.max_response_bytes == 0 || options.max_response_bytes > static_cast<std::size_t>(std::numeric_limits<int>::max())) {
            options.max_response_bytes = 10 * 1024 * 1024;
        }
        return options;
    }

    AsioHttpClient::Options options_;                                                        // 客户端参数
    asio::io_context io_context_;                                                            // 内部 I/O 上下文
    ssl::context ssl_context_;                                                               // HTTPS TLS 上下文
    std::unique_ptr<asio::executor_work_guard<asio::io_context::executor_type>> work_guard_; // 保持 io_context 运行
    std::vector<std::thread> workers_;                                                       // I/O 工作线程
};

AsioHttpClient::AsioHttpClient() : AsioHttpClient(Options {})
{}

AsioHttpClient::AsioHttpClient(Options options) : impl_(std::make_unique<Impl>(options))
{}

AsioHttpClient::~AsioHttpClient() = default;

std::optional<HttpResponse> AsioHttpClient::get(std::string_view url, const std::vector<std::string> &headers)
{
    return impl_->request("GET", url, "", headers);
}

std::optional<HttpResponse> AsioHttpClient::getWithOptions(std::string_view url, const std::vector<std::string> &headers, const HttpRequestOptions &options)
{
    return impl_->request("GET", url, "", headers, options);
}

HttpStreamResult AsioHttpClient::streamGetWithOptions(std::string_view url, const std::vector<std::string> &headers, const HttpRequestOptions &options, HttpStreamCallbacks callbacks)
{
    return impl_->streamGet(url, headers, options, std::move(callbacks));
}

std::optional<HttpResponse> AsioHttpClient::post(std::string_view url, std::string_view body, const std::vector<std::string> &headers)
{
    return impl_->request("POST", url, body, headers);
}

boost::asio::any_io_executor AsioHttpClient::executor()
{
    return impl_->executor();
}

AsioHttpClient::AwaitableResponse AsioHttpClient::coGet(std::string url, std::vector<std::string> headers)
{
    return impl_->coRequest("GET", std::move(url), "", std::move(headers));
}

AsioHttpClient::AwaitableResponse AsioHttpClient::coPost(std::string url, std::string body, std::vector<std::string> headers)
{
    return impl_->coRequest("POST", std::move(url), std::move(body), std::move(headers));
}

AsioHttpClient::AwaitableStreamResult AsioHttpClient::coStreamGetWithOptions(std::string url, std::vector<std::string> headers, HttpRequestOptions options, AwaitableStreamCallbacks callbacks)
{
    return impl_->coStreamGet(std::move(url), std::move(headers), options, std::move(callbacks));
}

void AsioHttpClient::spawnGet(std::string url, std::vector<std::string> headers, ResponseCallback callback)
{
    impl_->spawnRequest("GET", std::move(url), "", std::move(headers), std::move(callback));
}

void AsioHttpClient::spawnPost(std::string url, std::string body, std::vector<std::string> headers, ResponseCallback callback)
{
    impl_->spawnRequest("POST", std::move(url), std::move(body), std::move(headers), std::move(callback));
}

void AsioHttpClient::spawnStreamGetWithOptions(std::string url, std::vector<std::string> headers, HttpRequestOptions options, AwaitableStreamCallbacks callbacks, StreamCallback callback)
{
    impl_->spawnStreamRequest(std::move(url), std::move(headers), options, std::move(callbacks), std::move(callback));
}

} // namespace orchestrator::modules::crawler::infrastructure::http::client
