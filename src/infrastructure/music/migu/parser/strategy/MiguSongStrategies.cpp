#include "crawler/infrastructure/music/migu/parser/strategy/MiguStrategies.h"

#include "MiguStrategySupport.h"
#include "crawler/infrastructure/json/JsonUtils.h"

#include <cstdint>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::migu::parser::strategy {

using json::JsonUtils;

bool MiguSongListStrategy::parse(const nlohmann::json &root, std::vector<domain::ParseResult> &results) const
{
    if (const auto *data = JsonUtils::getObject(root, "songResultData")) {
        if (const auto *result_arr = JsonUtils::getArray(*data, "result")) {
            for (const auto &item : *result_arr) {
                domain::Song song;
                song.song_id = static_cast<std::uint64_t>(JsonUtils::getInt(item, "id"));
                song.title = JsonUtils::getString(item, "name");
                song.lrc = JsonUtils::getString(item, "lyricUrl");

                detail::extractUnsignedFromFirstItem(item, "singers", "name", song.artist, "id", song.singer_id);
                detail::extractUnsignedFromFirstItem(item, "albums", "name", song.album, "id", song.album_id);

                if (const auto *img_items = JsonUtils::getArray(item, "imgItems")) {
                    if (const auto *img = JsonUtils::getArrayItem(*img_items, 2)) {
                        song.artwork = JsonUtils::getString(*img, "img");
                    }
                }

                detail::addSongIfValid(std::move(song), results);
            }
            return true;
        }
    }
    return false;
}

bool MiguSearchStrategy::parse(const nlohmann::json &root, std::vector<domain::ParseResult> &results) const
{
    auto process_item = [&](const nlohmann::json &item) {
        if (detail::hasSongItem(item)) {
            detail::addSongIfValid(detail::parseSongFromSingerSongItem(item), results);
            return;
        }

        domain::Song song;
        song.song_id = static_cast<std::uint64_t>(JsonUtils::getInt(item, "songId"));
        song.title = JsonUtils::getString(item, "songName");

        if (const auto *ext = JsonUtils::getObject(item, "ext")) {
            song.lrc = JsonUtils::getString(*ext, "lrcUrl");
        }

        if (const auto *singer_list = JsonUtils::getArray(item, "singerList")) {
            if (const auto *singer = JsonUtils::getArrayItem(*singer_list, 0)) {
                song.artist = JsonUtils::getString(*singer, "name");
                song.singer_id = static_cast<std::uint64_t>(JsonUtils::getInt(*singer, "id"));
            }
        }

        song.album = JsonUtils::getString(item, "album");
        song.album_id = static_cast<std::uint64_t>(JsonUtils::getInt(item, "albumId"));
        song.artwork = JsonUtils::getString(item, "img3");

        detail::addSongIfValid(std::move(song), results);
    };

    const std::size_t initial_size = results.size();
    if (root.is_array()) {
        for (const auto &item : root) {
            process_item(item);
        }
    } else if (root.is_object()) {
        process_item(root);
    }

    return results.size() > initial_size;
}

bool MiguSingerSongStrategy::parse(const nlohmann::json &root, std::vector<domain::ParseResult> &results) const
{
    const auto *data = JsonUtils::getObject(root, "data");
    if (!data) {
        return false;
    }

    bool matched = false;
    if (const auto *header = JsonUtils::getObject(*data, "header")) {
        matched = JsonUtils::getString(*header, "title") == "歌手歌曲Tab";
    }

    const auto *contents = JsonUtils::getArray(*data, "contents");
    if (!contents) {
        return matched;
    }

    for (const auto &content_block : *contents) {
        if (JsonUtils::getString(content_block, "view") == "ZJ-Singer-Song-Scroll") {
            matched = true;
        }

        const auto *items = JsonUtils::getArray(content_block, "contents");
        if (items) {
            for (const auto &item : *items) {
                if (detail::hasSongItem(item)) {
                    matched = true;
                    detail::addSongIfValid(detail::parseSongFromSingerSongItem(item), results);
                }
            }
        }
    }

    return matched;
}

bool MiguAlbumDetailStrategy::parse(const nlohmann::json &root, std::vector<domain::ParseResult> &results) const
{
    if (const auto *resource_arr = JsonUtils::getArray(root, "resource")) {
        bool found = false;
        for (const auto &res_item : *resource_arr) {
            if (const auto *song_items = JsonUtils::getArray(res_item, "songItems")) {
                for (const auto &item : *song_items) {
                    domain::Song song;
                    song.song_id = static_cast<std::uint64_t>(JsonUtils::getInt(item, "songId"));
                    song.title = JsonUtils::getString(item, "songName");
                    song.artist = JsonUtils::getString(item, "singer");
                    song.singer_id = static_cast<std::uint64_t>(JsonUtils::getInt(item, "singerId"));
                    song.album = JsonUtils::getString(item, "album");
                    song.album_id = static_cast<std::uint64_t>(JsonUtils::getInt(item, "albumId"));
                    song.artwork = JsonUtils::getString(item, "landscapImg");
                    song.lrc = JsonUtils::getString(item, "lrcUrl");

                    detail::addSongIfValid(std::move(song), results);
                }
                found = true;
            }
        }
        return found;
    }
    return false;
}

} // namespace orchestrator::modules::crawler::infrastructure::music::migu::parser::strategy
