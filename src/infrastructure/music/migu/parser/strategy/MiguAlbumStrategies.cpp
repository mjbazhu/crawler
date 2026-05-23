#include "crawler/infrastructure/music/migu/parser/strategy/MiguStrategies.h"

#include "crawler/infrastructure/json/JsonUtils.h"

#include <cstdint>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::migu::parser::strategy {

using json::JsonUtils;

namespace {

// 把 Migu 专辑列表项转换为领域解析结果。
void addAlbumListItem(const nlohmann::json &item, std::vector<domain::ParseResult> &results)
{
    domain::Album album;
    album.album_id = static_cast<std::uint64_t>(JsonUtils::getInt(item, "id"));
    album.title = JsonUtils::getString(item, "name");
    album.artist = JsonUtils::getString(item, "singer");
    album.type = static_cast<int>(JsonUtils::getInt(item, "resourceType"));

    if (const auto *img_items = JsonUtils::getArray(item, "imgItems")) {
        if (const auto *img = JsonUtils::getArrayItem(*img_items, 2)) {
            album.artwork = JsonUtils::getString(*img, "img");
        }
    }

    domain::ParseResult res;
    res.suggested_filename = album.artist + " - " + album.title + " (Album)";
    res.meta_info = std::move(album);
    results.push_back(std::move(res));
}

} // namespace

bool MiguAlbumListStrategy::parse(const nlohmann::json &root, std::vector<domain::ParseResult> &results) const
{
    if (const auto *data = JsonUtils::getObject(root, "albumResultData")) {
        if (const auto *result_arr = JsonUtils::getArray(*data, "result")) {
            for (const auto &item : *result_arr) {
                addAlbumListItem(item, results);
            }
            return true;
        }
    }
    return false;
}

bool MiguAlbumListSearchStrategy::parse(const nlohmann::json &root, std::vector<domain::ParseResult> &results) const
{
    if (const auto *data = JsonUtils::getObject(root, "data")) {
        if (const auto *result_arr = JsonUtils::getArray(*data, "result")) {
            for (const auto &item : *result_arr) {
                addAlbumListItem(item, results);
            }
            return true;
        }
    }
    return false;
}

} // namespace orchestrator::modules::crawler::infrastructure::music::migu::parser::strategy
