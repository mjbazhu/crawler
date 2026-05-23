#include "crawler/infrastructure/music/migu/provider/MiguProvider.h"

#include "crawler/infrastructure/music/migu/provider/MiguRequestFactory.h"
#include "crawler/infrastructure/music/migu/provider/MiguUrlResolver.h"
#include "crawler/infrastructure/observability/logging/Logger.h"

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::migu::provider {

namespace {

// 转换基础 HTTP 响应为 application 可编排的原始 provider 响应。
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

// 判断歌曲资源请求是否有足够的 Migu listen 参数。
bool isValidSongResourceRequest(const application::model::SongResourceFetchRequest &request)
{
    return !request.content_id.empty() && !request.resource_type.empty() && !request.copyright_id.empty() && !request.tone_flag.empty();
}

// 解析资源接口 Cookie，只使用运行期 pacmtoken；未设置时不发送 Cookie。
std::string resolveMiguListenCookie(const std::shared_ptr<application::ports::MiguPacmtokenStore> &store)
{
    if (store) {
        if (auto pacmtoken = store->currentPacmtoken(); pacmtoken && !pacmtoken->empty()) {
            return "pacmtoken=" + *pacmtoken;
        }
    }
    return {};
}

} // namespace

MiguProvider::MiguProvider(std::shared_ptr<http::client::HttpClient> client) : MiguProvider(std::move(client), nullptr)
{}

MiguProvider::MiguProvider(
    std::shared_ptr<http::client::HttpClient> client,
    std::shared_ptr<application::ports::MiguPacmtokenStore> pacmtoken_store
) : client_(std::move(client)),
    pacmtoken_store_(std::move(pacmtoken_store))
{
    headers_ = MiguRequestFactory::defaultHeaders();
}

std::optional<MiguProvider::Response> MiguProvider::fetchSongSearchPage(std::string keyword, int page, int rows)
{
    if (!client_) {
        return std::nullopt;
    }

    return toProviderResponse(client_->get(MiguRequestFactory::songSearchUrl(std::move(keyword), page, rows), headers_));
}

std::optional<MiguProvider::Response> MiguProvider::fetchSongResource(application::model::SongResourceFetchRequest request)
{
    if (!client_ || !isValidSongResourceRequest(request)) {
        return std::nullopt;
    }

    auto headers = MiguRequestFactory::songResourceListenHeaders(resolveMiguListenCookie(pacmtoken_store_));
    return toProviderResponse(client_->get(MiguRequestFactory::songResourceListenUrl(request), headers));
}

std::optional<MiguProvider::Response> MiguProvider::fetchAlbumSearchPage(std::string keyword, int page, int rows)
{
    if (!client_) {
        return std::nullopt;
    }

    return toProviderResponse(client_->get(MiguRequestFactory::albumSearchUrl(std::move(keyword), page, rows), headers_));
}

std::optional<MiguProvider::Response> MiguProvider::fetchAlbumSongs(const domain::Album &album)
{
    if (!client_) {
        return std::nullopt;
    }

    return toProviderResponse(client_->get(MiguRequestFactory::albumSongsUrl(album), headers_));
}

std::optional<MiguProvider::Response> MiguProvider::fetchSingerAlbumsPage(std::uint64_t singer_id, int page)
{
    if (!client_ || singer_id == 0 || page <= 0) {
        return std::nullopt;
    }

    return toProviderResponse(client_->get(MiguRequestFactory::singerAlbumsUrl(singer_id, page), headers_));
}

std::optional<MiguProvider::Response> MiguProvider::fetchAlbumDetail(std::string album_id, std::string source_res_type)
{
    if (!client_ || album_id.empty()) {
        return std::nullopt;
    }

    std::uint64_t parsed_album_id = 0;
    try {
        parsed_album_id = std::stoull(album_id);
    } catch (...) {
        LOG_ERROR("[MiguProvider] Failed to parse album detail ID: " + album_id);
        return std::nullopt;
    }

    return toProviderResponse(client_->get(MiguRequestFactory::albumDetailUrl(parsed_album_id, source_res_type), headers_));
}

std::optional<MiguProvider::Response> MiguProvider::fetchSingerSongsPage(std::uint64_t singer_id, int page)
{
    if (!client_ || singer_id == 0 || page <= 0) {
        return std::nullopt;
    }

    return toProviderResponse(client_->get(MiguRequestFactory::singerSongsUrl(singer_id, page), headers_));
}

std::optional<MiguProvider::Response> MiguProvider::fetchSingerIndexPage(std::string tab)
{
    if (!client_ || !MiguRequestFactory::isSingerIndexTab(tab)) {
        return std::nullopt;
    }

    return toProviderResponse(client_->get(MiguRequestFactory::singerIndexUrl(std::move(tab)), headers_));
}

std::optional<MiguProvider::Response> MiguProvider::fetchUrl(std::string url)
{
    if (!client_) {
        return std::nullopt;
    }

    auto target_url = MiguUrlResolver::resolve(std::move(url));
    if (!target_url) {
        return std::nullopt;
    }

    return toProviderResponse(client_->get(*target_url, headers_));
}

const std::vector<std::string> &MiguProvider::singerIndexTabs()
{
    return MiguRequestFactory::singerIndexTabs();
}

std::optional<MiguProvider::Response> MiguProvider::fetchSongSearchPageSync(std::string keyword, int page, int rows)
{
    return fetchSongSearchPage(std::move(keyword), page, rows);
}

std::optional<MiguProvider::Response> MiguProvider::fetchSongResourceSync(application::model::SongResourceFetchRequest request)
{
    return fetchSongResource(std::move(request));
}

std::optional<MiguProvider::Response> MiguProvider::fetchAlbumSearchPageSync(std::string keyword, int page, int rows)
{
    return fetchAlbumSearchPage(std::move(keyword), page, rows);
}

std::optional<MiguProvider::Response> MiguProvider::fetchAlbumSongsSync(const domain::Album &album)
{
    return fetchAlbumSongs(album);
}

std::optional<MiguProvider::Response> MiguProvider::fetchSingerAlbumsPageSync(std::uint64_t singer_id, int page)
{
    return fetchSingerAlbumsPage(singer_id, page);
}

std::optional<MiguProvider::Response> MiguProvider::fetchAlbumDetailSync(std::string album_id, std::string source_res_type)
{
    return fetchAlbumDetail(std::move(album_id), std::move(source_res_type));
}

std::optional<MiguProvider::Response> MiguProvider::fetchSingerSongsPageSync(std::uint64_t singer_id, int page)
{
    return fetchSingerSongsPage(singer_id, page);
}

std::optional<MiguProvider::Response> MiguProvider::fetchSingerIndexPageSync(std::string tab)
{
    return fetchSingerIndexPage(std::move(tab));
}

std::optional<MiguProvider::Response> MiguProvider::fetchUrlSync(std::string url)
{
    return fetchUrl(std::move(url));
}

} // namespace orchestrator::modules::crawler::infrastructure::music::migu::provider
