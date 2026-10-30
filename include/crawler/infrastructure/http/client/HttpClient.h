#pragma once

#include <boost/asio/awaitable.hpp>

#include <cstdint>
#include <chrono>
#include <functional>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::http::client {

// HTTP 响应头条目，保留重复头以支持 Set-Cookie 等场景。
struct HttpHeader {
    std::string name;  // 响应头名称
    std::string value; // 响应头值
};

struct HttpResponse {
    int status_code;                 // HTTP 状态码
    std::vector<char> body;          // 响应体字节
    std::string content_type;        // Content-Type 响应头值
    std::vector<HttpHeader> headers; // 原始响应头列表，允许重复名称
};

struct HttpRequestOptions {
    std::chrono::milliseconds request_timeout {0}; // 单次请求超时；0 表示使用客户端默认值
    std::size_t max_response_bytes = 0;            // 最大响应体大小；0 表示使用客户端默认值
    std::function<bool()> cancelled;               // 单请求非阻塞取消检查，不能在其中访问数据库
};

// HTTP 流式响应头信息，body 分片开始前回调。
struct HttpStreamInfo {
    int status_code = 0;             // HTTP 状态码
    std::string content_type;        // Content-Type 响应头值
    std::vector<HttpHeader> headers; // 原始响应头列表
};

// HTTP 流式回调；返回 false 表示调用方拒绝继续接收 body。
struct HttpStreamCallbacks {
    std::function<bool(const HttpStreamInfo &info)> on_response; // 响应头回调
    std::function<bool(std::string_view chunk)> on_chunk;        // 响应体分片回调
};

// HTTP 流式请求结果，不携带完整响应体。
struct HttpStreamResult {
    bool ok = false;                  // HTTP 请求和 body 流是否完整成功
    int status_code = 0;              // HTTP 状态码；网络失败时为 0
    std::string content_type;         // Content-Type 响应头值
    std::vector<HttpHeader> headers;  // 原始响应头列表
    std::uint64_t bytes_received = 0; // 已交付给调用方的 body 字节数
    std::string result_code;          // 结果编码，例如 ok、network_failed、timeout
    std::string message;              // 脱敏后的诊断说明
};

// 请求体按需供给；read 返回空表示失败，0 表示结束，每次不超过给定缓冲。
struct HttpUploadBody {
    std::uint64_t size_bytes = 0;                                    // 已知完整长度，包含 multipart 头尾
    std::function<std::optional<std::size_t>(std::span<char>)> read; // 不聚合整个文件
    std::function<bool()> cancelled;                                 // 只停止本次连接
    std::uint64_t bytes_per_second = 0;                              // 单次发送限速，0 无额外限制
};

// 保留是否已进入远端副作用边界，不将网络中断当作确定未上传。
struct HttpUploadResult {
    std::optional<HttpResponse> response; // 仅完整、有界响应可用
    std::uint64_t bytes_sent = 0;         // 实际发送的请求体字节
    bool request_started = false;         // 开始写请求头后即可能产生副作用
    std::string result_code;              // 固定错误码，不携带 URL 或认证
};

class HttpClient {
public:
    using AwaitableResponse = boost::asio::awaitable<std::optional<HttpResponse>>;

    virtual ~HttpClient() = default;

    // 发送 GET 请求
    // 返回值：成功返回 HttpResponse，失败返回 std::nullopt
    // 线程安全说明：实现类必须保证该方法可被多线程并发调用
    virtual std::optional<HttpResponse> get(std::string_view url, const std::vector<std::string> &headers = {}) = 0;

    // 发送带单次覆盖选项的 GET 请求；默认实现保持旧客户端兼容。
    virtual std::optional<HttpResponse> getWithOptions(std::string_view url, const std::vector<std::string> &headers, const HttpRequestOptions &options)
    {
        (void)options;
        return get(url, headers);
    }

    // 流式 GET；默认实现回退到完整响应体，支持旧测试替身。
    virtual HttpStreamResult streamGetWithOptions(std::string_view url, const std::vector<std::string> &headers, const HttpRequestOptions &options, HttpStreamCallbacks callbacks);

    // 发送 POST 请求
    // body: 请求体内容
    // 线程安全说明：实现类必须保证该方法可被多线程并发调用
    virtual std::optional<HttpResponse> post(std::string_view url, std::string_view body, const std::vector<std::string> &headers = {}) = 0;

    // 云端文件上传需要独立的总超时；旧客户端保留兼容回退。
    virtual std::optional<HttpResponse> postWithOptions(std::string_view url, std::string_view body, const std::vector<std::string> &headers, const HttpRequestOptions &options)
    {
        (void)options;
        return post(url, body, headers);
    }

    // 新能力默认失败关闭，旧客户端不得回退为全量聚合大文件。
    virtual HttpUploadResult streamPostWithOptions(std::string_view, HttpUploadBody, const std::vector<std::string> &, const HttpRequestOptions &)
    {
        return {{}, 0, false, "stream_upload_unsupported"};
    }

    // 协程式 GET，请在调用方选择的 Asio executor 上 co_spawn。
    // 线程安全说明：实现类必须保证该方法可被多个协程并发调用。
    virtual AwaitableResponse coGet(std::string url, std::vector<std::string> headers = {}) = 0;

    // 协程式 POST，请在调用方选择的 Asio executor 上 co_spawn。
    virtual AwaitableResponse coPost(std::string url, std::string body, std::vector<std::string> headers = {}) = 0;
};

inline HttpStreamResult HttpClient::streamGetWithOptions(std::string_view url, const std::vector<std::string> &headers, const HttpRequestOptions &options, HttpStreamCallbacks callbacks)
{
    HttpStreamResult result;
    auto response = getWithOptions(url, headers, options);
    if (!response) {
        result.result_code = "network_failed";
        result.message = "http stream fallback request failed";
        return result;
    }

    result.status_code = response->status_code;
    result.content_type = response->content_type;
    result.headers = response->headers;

    HttpStreamInfo info;
    info.status_code = response->status_code;
    info.content_type = response->content_type;
    info.headers = response->headers;
    if (callbacks.on_response && !callbacks.on_response(info)) {
        result.result_code = "stream_consumer_failed";
        result.message = "http stream response was rejected by consumer";
        return result;
    }

    if (!response->body.empty()) {
        if (callbacks.on_chunk && !callbacks.on_chunk(std::string_view(response->body.data(), response->body.size()))) {
            result.result_code = "stream_consumer_failed";
            result.message = "http stream chunk was rejected by consumer";
            return result;
        }
    }

    result.ok = true;
    result.bytes_received = static_cast<std::uint64_t>(response->body.size());
    result.result_code = "ok";
    return result;
}

} // namespace orchestrator::modules::crawler::infrastructure::http::client
