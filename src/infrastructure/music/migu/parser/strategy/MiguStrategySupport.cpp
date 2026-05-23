#include "MiguStrategySupport.h"

#include "crawler/infrastructure/json/JsonUtils.h"

#include <cstdint>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::migu::parser::strategy::detail {

using json::JsonUtils;

std::string firstNonEmpty(std::initializer_list<std::string> values)
{
    for (const auto &value : values) {
        if (!value.empty()) {
            return value;
        }
    }
    return {};
}

void addSongIfValid(domain::Song song, std::vector<domain::ParseResult> &results)
{
    if (song.song_id != 0 && !song.title.empty()) {
        domain::ParseResult res;
        if (song.artist.empty()) {
            res.suggested_filename = song.title + ".mp3";
        } else {
            res.suggested_filename = song.artist + " - " + song.title + ".mp3";
        }
        res.meta_info = std::move(song);
        results.push_back(std::move(res));
    }
}

bool hasSongItem(const nlohmann::json &item)
{
    const auto *song_item = JsonUtils::getObject(item, "songItem");
    return song_item && JsonUtils::getInt(*song_item, "songId") != 0 && !JsonUtils::getString(*song_item, "songName").empty();
}

domain::Song parseSongFromSingerSongItem(const nlohmann::json &item)
{
    domain::Song song;
    const auto *song_item = JsonUtils::getObject(item, "songItem");
    if (!song_item) {
        return song;
    }

    song.song_id = static_cast<std::uint64_t>(JsonUtils::getInt(*song_item, "songId"));
    song.title = JsonUtils::getString(*song_item, "songName");
    song.lrc = JsonUtils::getString(*song_item, "lrcUrl");
    song.album = JsonUtils::getString(*song_item, "album", JsonUtils::getString(item, "txt3"));
    song.album_id = static_cast<std::uint64_t>(JsonUtils::getInt(*song_item, "albumId"));
    song.artwork = firstNonEmpty({
        JsonUtils::getString(*song_item, "img3"),
        JsonUtils::getString(*song_item, "img2"),
        JsonUtils::getString(*song_item, "img1"),
        JsonUtils::getString(item, "img"),
    });

    if (const auto *singer_list = JsonUtils::getArray(*song_item, "singerList")) {
        if (const auto *singer = JsonUtils::getArrayItem(*singer_list, 0)) {
            song.artist = JsonUtils::getString(*singer, "name");
            song.singer_id = static_cast<std::uint64_t>(JsonUtils::getInt(*singer, "id"));
        }
    }
    if (song.artist.empty()) {
        song.artist = JsonUtils::getString(item, "txt2");
    }

    return song;
}

void extractUnsignedFromFirstItem(
    const nlohmann::json &item,
    const std::string &array_key,
    const std::string &name_key,
    std::string &name_value,
    const std::string &id_key,
    std::uint64_t &id_value
)
{
    if (const auto *arr = JsonUtils::getArray(item, array_key)) {
        if (const auto *first = JsonUtils::getArrayItem(*arr, 0)) {
            name_value = JsonUtils::getString(*first, name_key);
            id_value = static_cast<std::uint64_t>(JsonUtils::getInt(*first, id_key));
        }
    }
}

} // namespace orchestrator::modules::crawler::infrastructure::music::migu::parser::strategy::detail
