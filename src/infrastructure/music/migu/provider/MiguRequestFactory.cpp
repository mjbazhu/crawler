#include "crawler/infrastructure/music/migu/provider/MiguRequestFactory.h"

#include "crawler/infrastructure/music/migu/provider/MiguParamsBuilder.h"

#include <algorithm>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::migu::provider {

namespace {

// 判断歌手专辑列表项是否为数字专辑；Migu 列表 resType=1 对应 dAlbumId 详情接口。
bool isDigitalAlbum(std::string_view source_res_type)
{
    return source_res_type == "1";
}

// 将歌手专辑列表资源类型映射为 Migu resourceinfo.do 的资源类型。
int albumDetailResourceType(std::string_view source_res_type)
{
    return isDigitalAlbum(source_res_type) ? 5 : 2003;
}

} // namespace

std::vector<std::string> MiguRequestFactory::defaultHeaders()
{
    return {
        "Accept: application/json, text/plain, */*",
        "Referer: https://music.migu.cn/",
        "User-Agent: Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 (KHTML, like Gecko) Chrome/120.0.0.0 Safari/537.36",
        "Origin: https://music.migu.cn"
    };
}

std::vector<std::string> MiguRequestFactory::songResourceListenHeaders(std::string cookie)
{
    std::vector<std::string> headers {
        "User-Agent: Mozilla/5.0 (Linux; U; Android 9; zh-cn; MI 6 Build/PKQ1.190118.001) AppleWebKit/533.1 (KHTML, like Gecko) Version/5.0 Mobile Safari/533.1",
        "channel: 0146921"
    };
    if (!cookie.empty()) {
        headers.push_back("Cookie: " + std::move(cookie));
    }
    return headers;
}

std::string MiguRequestFactory::songSearchUrl(std::string keyword, int page, int rows)
{
    Params params {std::move(keyword), page, rows};
    return std::string(URL_SEARCH_SONGS) + toQueryString(params);
}

std::string MiguRequestFactory::songResourceListenUrl(const application::model::SongResourceFetchRequest &request)
{
    Params params;
    params.contentId = request.content_id;
    params.netType = request.net_type.empty() ? std::string {"01"} : request.net_type;
    params.resourceTypeText = request.resource_type;
    params.copyrightId = request.copyright_id;
    params.toneFlag = request.tone_flag;
    return std::string(URL_LISTEN_RESOURCE) + toQueryString(params);
}

std::string MiguRequestFactory::albumSearchUrl(std::string keyword, int page, int rows)
{
    Params params {std::move(keyword), page, rows};
    return std::string(URL_SEARCH_ALBUM) + toQueryString(params);
}

std::string MiguRequestFactory::albumSongsUrl(const domain::Album &album)
{
    Params params = {
        .needSimple = "01",
        .resourceId = album.album_id,
        .resourceType = album.type
    };
    return std::string(URL_RESOURCE_INFO) + toQueryString(params);
}

std::string MiguRequestFactory::singerAlbumsUrl(std::uint64_t singer_id, int page)
{
    Params params = {
        .pageNo = page,
        .singerId = singer_id
    };
    return std::string(URL_SINGER_ALBUMS) + toQueryString(params);
}

std::string MiguRequestFactory::singerInfoUrl(std::uint64_t singer_id)
{
    Params params = {
        .singerId = singer_id
    };
    return std::string(URL_SINGER_INFO) + toQueryString(params);
}

std::string MiguRequestFactory::albumDetailUrl(std::uint64_t album_id, std::string_view source_res_type)
{
    Params params = {
        .needSimple = "01",
        .resourceId = album_id,
        .resourceType = albumDetailResourceType(source_res_type)
    };
    return std::string(URL_RESOURCE_INFO) + toQueryString(params);
}

std::string MiguRequestFactory::albumSongPageUrl(std::uint64_t album_id, int page, int page_size)
{
    Params params {.pageNo = page, .pageSize = page_size, .albumId = album_id};
    return std::string(URL_ALBUM_SONG_PAGE) + toQueryString(params);
}

std::string MiguRequestFactory::singerSongsUrl(std::uint64_t singer_id, int page)
{
    Params params = {
        .pageNo = page,
        .singerId = singer_id,
        .type = 1
    };
    return std::string(URL_SINGER_SONGS) + toQueryString(params);
}

std::string MiguRequestFactory::singerIndexUrl(std::string tab)
{
    Params params = {
        .tab = std::move(tab)
    };
    return std::string(URL_SINGER_INDEX) + toQueryString(params);
}

const std::vector<std::string> &MiguRequestFactory::singerIndexTabs()
{
    static const std::vector<std::string> tabs {
        "huayu-nan",
        "huayu-nv",
        "huayu-group",
        "oumei-nan",
        "oumei-nv",
        "oumei-group",
        "rihan-nan",
        "rihan-nv",
        "rihan-group"
    };
    return tabs;
}

bool MiguRequestFactory::isSingerIndexTab(std::string_view tab)
{
    const auto &tabs = singerIndexTabs();
    return std::any_of(tabs.begin(), tabs.end(), [tab](const std::string &candidate) {
        return std::string_view(candidate) == tab;
    });
}

} // namespace orchestrator::modules::crawler::infrastructure::music::migu::provider
