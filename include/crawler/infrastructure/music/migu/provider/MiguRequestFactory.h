#pragma once

#include "crawler/application/model/SongResourceFetch.h"
#include "crawler/domain/music/model/Album.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::music::migu::provider {

// Migu 请求工厂，只负责把领域/平台参数转换为 Migu API URL 和默认 headers。
class MiguRequestFactory {
public:
    // 返回 Migu HTTP 请求默认 headers。
    static std::vector<std::string> defaultHeaders();

    // 返回 Migu listen 资源接口 headers，cookie 为空时不写 Cookie header。
    static std::vector<std::string> songResourceListenHeaders(std::string cookie);

    // 构造歌曲搜索 URL。
    static std::string songSearchUrl(std::string keyword, int page, int rows);

    // 构造歌曲资源 listen URL。
    static std::string songResourceListenUrl(const application::model::SongResourceFetchRequest &request);

    // 构造专辑搜索 URL。
    static std::string albumSearchUrl(std::string keyword, int page, int rows);

    // 构造专辑歌曲 URL。
    static std::string albumSongsUrl(const domain::Album &album);

    // 构造歌手专辑分页 URL。
    static std::string singerAlbumsUrl(std::uint64_t singer_id, int page);

    // 构造歌手主要信息 URL。
    static std::string singerInfoUrl(std::uint64_t singer_id);

    // 构造专辑详情 URL。
    static std::string albumDetailUrl(std::uint64_t album_id, std::string_view source_res_type);

    // 详情只含曲目前缀时，构造独立曲目分页接口的 URL。
    static std::string albumSongPageUrl(std::uint64_t album_id, int page, int page_size);

    // 构造歌手歌曲分页 URL。
    static std::string singerSongsUrl(std::uint64_t singer_id, int page);

    // 构造歌手分类 URL。
    static std::string singerIndexUrl(std::string tab);

    // 返回 Migu 支持的歌手分类 tab 组合。
    static const std::vector<std::string> &singerIndexTabs();

    // 判断 tab 是否属于 Migu 支持范围。
    static bool isSingerIndexTab(std::string_view tab);

private:
    static constexpr const char *URL_SEARCH_SONGS = "https://app.u.nf.migu.cn/pc/resource/song/item/search/v1.0?";
    static constexpr const char *URL_LISTEN_RESOURCE = "http://app.c.nf.migu.cn/MIGUM3.0/strategy/pc/listen/v1.0?";
    static constexpr const char *URL_RESOURCE_INFO = "https://app.c.nf.migu.cn/v1.0/content/resourceinfo.do?";
    static constexpr const char *URL_ALBUM_SONG_PAGE = "https://app.c.nf.migu.cn/MIGUM3.0/resource/album/song/v2.0?";
    static constexpr const char *URL_SEARCH_ALBUM = "https://app.u.nf.migu.cn/pc/bmw/album/search/v1.0?";
    static constexpr const char *URL_SINGER_INFO = "https://app.c.nf.migu.cn/pc/bmw/singer/info/v1.1?";
    static constexpr const char *URL_SINGER_SONGS = "https://app.c.nf.migu.cn/pc/bmw/singer/song/v1.0?";
    static constexpr const char *URL_SINGER_ALBUMS = "https://app.c.nf.migu.cn/pc/bmw/singer/album/v1.0?";
    static constexpr const char *URL_SINGER_INDEX = "https://app.c.nf.migu.cn/pc/bmw/singer-index/list/v1.0?";
};

} // namespace orchestrator::modules::crawler::infrastructure::music::migu::provider
