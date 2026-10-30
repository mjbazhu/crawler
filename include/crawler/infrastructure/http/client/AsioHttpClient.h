#pragma once

#include "crawler/infrastructure/http/client/HttpClient.h"

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>

#include <chrono>
#include <cstddef>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::http::client {

// 基于 Boost.Asio 的出站 HTTP 客户端。
// 线程安全：get/post 可被多个线程并发调用，实际网络 I/O 由内部 io_context 线程池执行。
class AsioHttpClient : public HttpClient {
public:
    using AwaitableResponse = HttpClient::AwaitableResponse;
    using AwaitableStreamResult = boost::asio::awaitable<HttpStreamResult>;
    using ResponseCallback = std::function<void(std::optional<HttpResponse>)>;
    using StreamCallback = std::function<void(HttpStreamResult)>;

    // 协程式流式回调；返回 false 表示下游拒绝继续接收 body。
    struct AwaitableStreamCallbacks {
        std::function<boost::asio::awaitable<bool>(const HttpStreamInfo &info)> on_response; // 响应头回调
        std::function<boost::asio::awaitable<bool>(std::string_view chunk)> on_chunk;        // body 分片回调
    };

    // HTTP 代理配置；HTTPS 请求通过 CONNECT 建立隧道。
    struct ProxyOptions {
        std::string host;     // 代理主机
        std::string port;     // 代理端口
        std::string username; // 可选用户名
        std::string password; // 可选密码

        bool enabled() const
        {
            return !host.empty() && !port.empty();
        }
    };

    // 客户端运行参数，后续可由组合根配置化。
    struct Options {
        int io_threads = 4;                                // 内部 I/O 线程数
        std::chrono::milliseconds request_timeout {30000}; // 单次请求总超时
        std::size_t max_response_bytes = 10 * 1024 * 1024; // 最大响应体大小
        bool verify_tls = true;                            // 是否校验 HTTPS 证书
        std::optional<ProxyOptions> proxy;                 // 可选 HTTP 代理
        std::function<bool()> cancelled;                   // 单次会话取消快照，默认无取消
    };

    AsioHttpClient();
    explicit AsioHttpClient(Options options);
    ~AsioHttpClient() override;

    // 发送 GET 请求，返回完整响应体。
    std::optional<HttpResponse> get(std::string_view url, const std::vector<std::string> &headers = {}) override;

    // 发送带单次超时/大小覆盖的 GET 请求。
    std::optional<HttpResponse> getWithOptions(std::string_view url, const std::vector<std::string> &headers, const HttpRequestOptions &options) override;

    // 发送带单次超时/大小覆盖的流式 GET 请求。
    HttpStreamResult streamGetWithOptions(std::string_view url, const std::vector<std::string> &headers, const HttpRequestOptions &options, HttpStreamCallbacks callbacks) override;

    // 发送 POST 请求，返回完整响应体。
    std::optional<HttpResponse> post(std::string_view url, std::string_view body, const std::vector<std::string> &headers = {}) override;
    // 单次 POST 覆盖超时与响应上限，用于已下载文件云端上传。
    std::optional<HttpResponse> postWithOptions(std::string_view url, std::string_view body, const std::vector<std::string> &headers, const HttpRequestOptions &options) override;

    // 固定缓冲流式 POST，支持单次限速、主动取消和有界响应。
    HttpUploadResult streamPostWithOptions(std::string_view url, HttpUploadBody body, const std::vector<std::string> &headers, const HttpRequestOptions &options) override;

    // 返回内部 Asio executor，供外部用 co_spawn 启动 coGet/coPost。
    boost::asio::any_io_executor executor();

    // 协程式 GET，请通过 executor() 或兼容 executor 启动。
    AwaitableResponse coGet(std::string url, std::vector<std::string> headers = {}) override;

    // 协程式 POST，请通过 executor() 或兼容 executor 启动。
    AwaitableResponse coPost(std::string url, std::string body, std::vector<std::string> headers = {}) override;

    // 协程式流式 GET；body 分片通过 awaitable callback 交付，不聚合完整响应体。
    AwaitableStreamResult coStreamGetWithOptions(std::string url, std::vector<std::string> headers, HttpRequestOptions options, AwaitableStreamCallbacks callbacks);

    // 直接在内部 io_context 上启动 GET 协程，不阻塞调用线程。
    void spawnGet(std::string url, std::vector<std::string> headers, ResponseCallback callback);

    // 直接在内部 io_context 上启动 POST 协程，不阻塞调用线程。
    void spawnPost(std::string url, std::string body, std::vector<std::string> headers, ResponseCallback callback);

    // 直接在内部 io_context 上启动流式 GET 协程，不阻塞调用线程。
    void spawnStreamGetWithOptions(std::string url, std::vector<std::string> headers, HttpRequestOptions options, AwaitableStreamCallbacks callbacks, StreamCallback callback);

private:
    // 隐藏 Boost.Asio/OpenSSL 实现细节。
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace orchestrator::modules::crawler::infrastructure::http::client
