#include "crawler/infrastructure/music/migu/provider/MiguAsyncSongResourceProvider.h"

#include "crawler/application/async/Task.h"
#include "crawler/infrastructure/music/migu/provider/MiguRequestFactory.h"

#include <optional>
#include <string>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::migu::provider {

namespace {

// 判断歌曲资源请求是否有足够的 Migu listen 参数。
bool validSongResourceRequest(const application::model::SongResourceFetchRequest &request)
{
    return !request.content_id.empty() && !request.resource_type.empty() && !request.copyright_id.empty() && !request.tone_flag.empty();
}

// 解析资源接口 Cookie，只使用运行期 pacmtoken；未设置时不发送 Cookie。
std::string listenCookie(const std::shared_ptr<application::ports::MiguPacmtokenStore> &store)
{
    if (!store) {
        return {};
    }
    auto token = store->currentPacmtoken();
    if (!token || token->empty()) {
        return {};
    }
    return "pacmtoken=" + *token;
}

// 转换 HTTP 响应为 provider 响应。
std::optional<application::model::ProviderResponse> toProviderResponse(std::optional<http::client::HttpResponse> response)
{
    if (!response) {
        return std::nullopt;
    }
    application::model::ProviderResponse converted;
    converted.status_code = response->status_code;
    converted.body = std::move(response->body);
    converted.content_type = std::move(response->content_type);
    return converted;
}

} // namespace

MiguAsyncSongResourceProvider::MiguAsyncSongResourceProvider(
    std::shared_ptr<orchestrator::modules::crawler::infrastructure::http::client::AsioHttpClient> client,
    std::shared_ptr<application::ports::MiguPacmtokenStore> pacmtoken_store
) : client_(std::move(client)),
    pacmtoken_store_(std::move(pacmtoken_store))
{}

application::async::Task<std::optional<MiguAsyncSongResourceProvider::Response>> MiguAsyncSongResourceProvider::fetchSongResource(application::model::SongResourceFetchRequest request)
{
    if (!client_ || !validSongResourceRequest(request)) {
        co_return std::nullopt;
    }

    application::async::TaskCompletionSource<std::optional<Response>> source;
    auto task = source.task();
    auto headers = MiguRequestFactory::songResourceListenHeaders(listenCookie(pacmtoken_store_));
    auto url = MiguRequestFactory::songResourceListenUrl(request);
    client_->spawnGet(std::move(url), std::move(headers), [source](std::optional<http::client::HttpResponse> response) mutable {
        source.set_value(toProviderResponse(std::move(response)));
    });
    co_return co_await task;
}

} // namespace orchestrator::modules::crawler::infrastructure::music::migu::provider
