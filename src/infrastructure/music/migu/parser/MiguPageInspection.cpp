#include "MiguParserSupportInternal.h"

#include "crawler/application/ports/parsing/PageInspector.h"
#include "crawler/infrastructure/json/JsonUtils.h"
#include "crawler/infrastructure/music/migu/parser/MiguParserSupport.h"

namespace orchestrator::modules::crawler::infrastructure::music::migu::parser::detail {

using json::JsonUtils;

namespace {

constexpr std::size_t kSingerSongPageSize = 50;
constexpr std::size_t kSingerAlbumPageSize = 50;

} // namespace

application::ports::SingerSongPageInfo inspectSingerSongPageRaw(const std::vector<char> &raw_data)
{
    application::ports::SingerSongPageInfo info;
    auto root_opt = json::JsonUtils::parse(raw_data);
    if (!root_opt) {
        return info;
    }

    const auto *data = JsonUtils::getObject(*root_opt, "data");
    if (!data) {
        return info;
    }

    const auto *contents = JsonUtils::getArray(*data, "contents");
    if (!contents) {
        return info;
    }

    if (const auto *first = JsonUtils::getArrayItem(*contents, 0)) {
        info.first_content_song_count = countSingerSongsInContentBlock(*first);
    }
    if (const auto *second = JsonUtils::getArrayItem(*contents, 1)) {
        info.second_content_song_count = countSingerSongsInContentBlock(*second);
    }

    info.should_stop_paging = info.first_content_song_count < kSingerSongPageSize && info.second_content_song_count < kSingerSongPageSize;
    return info;
}

application::ports::SingerAlbumPageInfo inspectSingerAlbumPageRaw(const std::vector<char> &raw_data)
{
    application::ports::SingerAlbumPageInfo info;
    auto root_opt = json::JsonUtils::parse(raw_data);
    if (!root_opt) {
        return info;
    }

    const auto *data = JsonUtils::getObject(*root_opt, "data");
    if (!data) {
        return info;
    }

    const auto *contents = JsonUtils::getArray(*data, "contents");
    info.album_count = countSingerAlbums(contents ? *contents : nlohmann::json::array());
    info.should_stop_paging = info.album_count < kSingerAlbumPageSize;
    return info;
}

} // namespace orchestrator::modules::crawler::infrastructure::music::migu::parser::detail
