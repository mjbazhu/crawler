#include "crawler/infrastructure/music/migu/parser/MiguSongSourceParser.h"

#include "MiguParserSupportInternal.h"
#include "crawler/infrastructure/json/JsonUtils.h"
#include "crawler/infrastructure/music/migu/parser/MiguParserSupport.h"

#include <charconv>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::migu::parser {

std::vector<application::model::SongSourceMetadata> MiguSongSourceParser::parseSongSources(const std::vector<char> &raw_data) const
{
    return detail::parseSongSourcesRaw(raw_data);
}

application::model::AlbumSongPage MiguSongSourceParser::parseAlbumSongPage(const std::vector<char> &raw_data) const
{
    application::model::AlbumSongPage result;
    const auto root = json::JsonUtils::parse(raw_data);
    if (!root) return result;
    const auto code = json::JsonUtils::getString(*root, "code");
    if (!code.empty() && code != "000000") return result;
    const auto *data = json::JsonUtils::getObject(*root, "data");
    if (!data) return result;
    const auto *songs = json::JsonUtils::getArray(*data, "songList");
    if (!songs) return result;
    const auto field = data->find("totalCount");
    const auto text = field != data->end() && (field->is_string() || field->is_number_integer()) ? json::JsonUtils::getString(*data, "totalCount") : std::string {};
    int total = -1;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), total);
    if (parsed.ec == std::errc {} && parsed.ptr == text.data() + text.size() && total >= 0) result.source_song_total = total;
    result.returned_count = songs->size();
    std::uint32_t position = 0;
    for (const auto &song : *songs) {
        ++position;
        if (auto metadata = detail::parseSongSourceFromSongObject(song, nullptr, nullptr)) {
            metadata->album_song_order = position;
            result.songs.push_back(std::move(*metadata));
        }
    }
    return result;
}

} // namespace orchestrator::modules::crawler::infrastructure::music::migu::parser
