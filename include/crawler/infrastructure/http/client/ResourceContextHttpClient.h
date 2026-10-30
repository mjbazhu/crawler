#pragma once

#include "crawler/application/model/ResourceTransferExecution.h"
#include "crawler/infrastructure/http/client/HttpClient.h"

#include <atomic>
#include <memory>
#include <thread>

namespace orchestrator::modules::crawler::infrastructure::http::client {

// 每次来源快照独占的同步 HTTP 装饰器；后台检查取消，I/O strand 只读原子标志。
class ResourceContextHttpClient final : public HttpClient {
public:
    // 固定本项截止时间，不修改共享底层客户端配置。
    ResourceContextHttpClient(std::shared_ptr<HttpClient> inner, application::model::ResourceTransferContext context);
    ~ResourceContextHttpClient() override;
    // 分片与本地摘要只读取后台缓存，禁止在 I/O 回调内查询数据库。
    application::model::ResourceTransferContext executionContext() const;
    std::optional<HttpResponse> get(std::string_view url, const std::vector<std::string> &headers = {}) override;
    std::optional<HttpResponse> getWithOptions(std::string_view url, const std::vector<std::string> &headers, const HttpRequestOptions &options) override;
    HttpStreamResult streamGetWithOptions(std::string_view url, const std::vector<std::string> &headers, const HttpRequestOptions &options, HttpStreamCallbacks callbacks) override;
    std::optional<HttpResponse> post(std::string_view url, std::string_view body, const std::vector<std::string> &headers = {}) override;
    std::optional<HttpResponse> postWithOptions(std::string_view url, std::string_view body, const std::vector<std::string> &headers, const HttpRequestOptions &options) override;
    // 同步快照不使用协程端口；拒绝意外绕过本项预算。
    AwaitableResponse coGet(std::string url, std::vector<std::string> headers = {}) override;
    AwaitableResponse coPost(std::string url, std::string body, std::vector<std::string> headers = {}) override;

private:
    HttpRequestOptions bounded(HttpRequestOptions options) const; // 每阶段重新取剩余时间
    std::shared_ptr<HttpClient> inner_;                           // 本目标独立执行器
    application::model::ResourceTransferContext context_;         // 当前项截止和取消
    std::shared_ptr<std::atomic_bool> stopped_;                   // 请求可安全持有至回调清理完成
    std::jthread monitor_;                                        // 只在本项获取期间存在；持久轮询不占 I/O strand
};

} // namespace orchestrator::modules::crawler::infrastructure::http::client
