#include "crawler/infrastructure/http/client/ResourceContextHttpClient.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::http::client {

ResourceContextHttpClient::ResourceContextHttpClient(std::shared_ptr<HttpClient> inner, application::model::ResourceTransferContext context) : inner_(std::move(inner)), context_(std::move(context)), stopped_(std::make_shared<std::atomic_bool>(false))
{
    if (!inner_) throw std::invalid_argument("resource HTTP dependency required");
    monitor_ = std::jthread([context = context_, stopped = stopped_](std::stop_token stop) {
        while (!stop.stop_requested()) {
            if (context.stopped()) {
                stopped->store(true);
                break;
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(25));
        }
    });
}
ResourceContextHttpClient::~ResourceContextHttpClient()
{
    monitor_.request_stop();
}
application::model::ResourceTransferContext ResourceContextHttpClient::executionContext() const
{
    return {context_.deadline, [stopped = stopped_] { return stopped->load(); }};
}
HttpRequestOptions ResourceContextHttpClient::bounded(HttpRequestOptions options) const
{
    const auto remaining = std::chrono::milliseconds((std::max)(1, context_.remaining()));
    options.request_timeout = options.request_timeout.count() > 0 ? (std::min)(options.request_timeout, remaining) : remaining;
    const auto previous = options.cancelled;
    options.cancelled = [stopped = stopped_, previous] { return stopped->load() || (previous && previous()); };
    return options;
}
std::optional<HttpResponse> ResourceContextHttpClient::get(std::string_view url, const std::vector<std::string> &headers)
{
    return getWithOptions(url, headers, {});
}
std::optional<HttpResponse> ResourceContextHttpClient::getWithOptions(std::string_view url, const std::vector<std::string> &headers, const HttpRequestOptions &options)
{
    if (context_.stopped()) return std::nullopt;
    const auto value = inner_->getWithOptions(url, headers, bounded(options));
    return context_.stopped() ? std::nullopt : value;
}
HttpStreamResult ResourceContextHttpClient::streamGetWithOptions(std::string_view url, const std::vector<std::string> &headers, const HttpRequestOptions &options, HttpStreamCallbacks callbacks)
{
    if (context_.stopped()) {
        HttpStreamResult value;
        value.result_code = "cancelled";
        return value;
    }
    return inner_->streamGetWithOptions(url, headers, bounded(options), std::move(callbacks));
}
std::optional<HttpResponse> ResourceContextHttpClient::post(std::string_view url, std::string_view body, const std::vector<std::string> &headers)
{
    return postWithOptions(url, body, headers, {});
}
std::optional<HttpResponse> ResourceContextHttpClient::postWithOptions(std::string_view url, std::string_view body, const std::vector<std::string> &headers, const HttpRequestOptions &options)
{
    if (context_.stopped()) return std::nullopt;
    const auto value = inner_->postWithOptions(url, body, headers, bounded(options));
    return context_.stopped() ? std::nullopt : value;
}
HttpClient::AwaitableResponse ResourceContextHttpClient::coGet(std::string, std::vector<std::string>)
{
    co_return std::nullopt;
}
HttpClient::AwaitableResponse ResourceContextHttpClient::coPost(std::string, std::string, std::vector<std::string>)
{
    co_return std::nullopt;
}

} // namespace orchestrator::modules::crawler::infrastructure::http::client
