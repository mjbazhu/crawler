#include "MiguParserSupportInternal.h"

#include "crawler/infrastructure/json/JsonUtils.h"
#include "crawler/infrastructure/music/migu/parser/MiguParserSupport.h"

#include <cctype>
#include <cstdint>
#include <string_view>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::migu::parser::detail {

using json::JsonUtils;

namespace {

constexpr std::string_view kMiguResourcePrefix = "https://d.musicapp.migu.cn";

// 从平台 Disc 文本中提取第一个正整数，提取不到时返回 0。
std::uint32_t parseDiscNo(const std::string &disc_name)
{
    std::uint32_t value = 0;
    bool found = false;
    for (unsigned char ch : disc_name) {
        if (std::isdigit(ch) != 0) {
            found = true;
            value = value * 10 + static_cast<std::uint32_t>(ch - '0');
        } else if (found) {
            break;
        }
    }
    return found ? value : 0;
}

// 将 Migu 图片相对路径补齐为可直接请求的资源地址。
std::string normalizeMiguResourceUrl(std::string resource_url)
{
    if (resource_url.empty() || resource_url.starts_with("http://") || resource_url.starts_with("https://")) {
        return resource_url;
    }
    if (resource_url.starts_with("//")) {
        return "https:" + resource_url;
    }
    if (resource_url.starts_with('/')) {
        return std::string(kMiguResourcePrefix) + resource_url;
    }
    if (resource_url.starts_with("data/oss/")) {
        return std::string(kMiguResourcePrefix) + "/" + resource_url;
    }
    return resource_url;
}

} // namespace

std::optional<application::model::SongSourceMetadata> parseSongSourceFromSongObject(
    const nlohmann::json &song_item,
    const nlohmann::json *wrapper,
    const nlohmann::json *album_context,
    std::uint32_t song_order
)
{
    application::model::SongSourceMetadata metadata;
    metadata.source = "migu";
    metadata.source_song_id = JsonUtils::getString(song_item, "songId");
    metadata.source_content_id = firstNonEmpty({
        JsonUtils::getString(song_item, "contentId"),
        JsonUtils::getString(song_item, "refId"),
        wrapper ? JsonUtils::getString(*wrapper, "resId") : std::string {},
        wrapper ? extractActionId(JsonUtils::getString(*wrapper, "action")) : std::string {},
    });
    metadata.source_resource_type = JsonUtils::getString(song_item, "resourceType");

    metadata.title = firstNonEmpty({
        JsonUtils::getString(song_item, "songName"),
        wrapper ? JsonUtils::getString(*wrapper, "txt") : std::string {},
    });
    if (metadata.title.empty() || (metadata.source_song_id.empty() && metadata.source_content_id.empty())) {
        return std::nullopt;
    }
    metadata.normalized_title = metadata.title;
    metadata.source_duration_seconds = static_cast<std::uint32_t>(JsonUtils::getInt(song_item, "duration"));

    metadata.source_title_pinyin = JsonUtils::getString(song_item, "songPinyin");
    metadata.source_album_id = firstNonEmpty({
        album_context ? JsonUtils::getString(*album_context, "contentId") : std::string {},
        album_context ? JsonUtils::getString(*album_context, "albumId") : std::string {},
        JsonUtils::getString(song_item, "albumId"),
    });
    metadata.source_album_name = firstNonEmpty({
        JsonUtils::getString(song_item, "album"),
        album_context ? JsonUtils::getString(*album_context, "title") : std::string {},
        wrapper ? JsonUtils::getString(*wrapper, "txt3") : std::string {},
    });
    metadata.source_album_pinyin = JsonUtils::getString(song_item, "albumPinyin");
    metadata.cover_url = normalizeMiguResourceUrl(
        firstNonEmpty({
            JsonUtils::getString(song_item, "img3"),
            JsonUtils::getString(song_item, "img2"),
            JsonUtils::getString(song_item, "img1"),
            JsonUtils::getString(song_item, "landscapImg"),
            JsonUtils::getString(song_item, "img"),
            album_context ? selectImageUrl(JsonUtils::getArray(*album_context, "imgItem")) : std::string {},
            album_context ? selectImageUrl(JsonUtils::getArray(*album_context, "imgItems")) : std::string {},
            wrapper ? JsonUtils::getString(*wrapper, "img") : std::string {},
        })
    );
    metadata.lyric_url = JsonUtils::getString(song_item, "lrcUrl");
    metadata.mrc_url = JsonUtils::getString(song_item, "mrcUrl");
    metadata.trc_url = JsonUtils::getString(song_item, "trcUrl");
    metadata.copyright_id = JsonUtils::getString(song_item, "copyrightId");
    metadata.album_song_order = song_order;
    metadata.album_track_no = song_order;
    if (const auto *ext = JsonUtils::getObject(song_item, "ext")) {
        metadata.album_disc_name = JsonUtils::getString(*ext, "disc");
        metadata.album_disc_no = parseDiscNo(metadata.album_disc_name);
    }

    if (const auto *singers = JsonUtils::getArray(song_item, "singerList")) {
        metadata.artist_refs.reserve(singers->size());
        std::uint32_t artist_order = 1;
        for (const auto &singer : *singers) {
            application::model::SongSourceArtistRef artist_ref;
            artist_ref.source_artist_id = JsonUtils::getString(singer, "id");
            artist_ref.source_artist_name = JsonUtils::getString(singer, "name");
            artist_ref.artist_order = artist_order;
            artist_ref.is_primary = artist_order == 1;
            if (!artist_ref.source_artist_id.empty() || !artist_ref.source_artist_name.empty()) {
                metadata.artist_refs.push_back(std::move(artist_ref));
            }
            ++artist_order;
        }
        if (!metadata.artist_refs.empty()) {
            metadata.source_primary_artist_id = metadata.artist_refs.front().source_artist_id;
            metadata.source_primary_artist_name = metadata.artist_refs.front().source_artist_name;
        }
    }
    if (metadata.source_primary_artist_id.empty()) {
        metadata.source_primary_artist_id = firstNonEmpty({
            JsonUtils::getString(song_item, "singerId"),
            album_context ? JsonUtils::getString(*album_context, "singerId") : std::string {},
        });
    }
    if (metadata.source_primary_artist_name.empty()) {
        metadata.source_primary_artist_name = firstNonEmpty({
            JsonUtils::getString(song_item, "singer"),
            album_context ? JsonUtils::getString(*album_context, "singer") : std::string {},
            wrapper ? JsonUtils::getString(*wrapper, "txt2") : std::string {},
        });
    }
    if (metadata.artist_refs.empty() && (!metadata.source_primary_artist_id.empty() || !metadata.source_primary_artist_name.empty())) {
        application::model::SongSourceArtistRef artist_ref;
        artist_ref.source_artist_id = metadata.source_primary_artist_id;
        artist_ref.source_artist_name = metadata.source_primary_artist_name;
        artist_ref.artist_order = 1;
        artist_ref.is_primary = true;
        metadata.artist_refs.push_back(std::move(artist_ref));
    }

    if (const auto *formats = JsonUtils::getArray(song_item, "audioFormats")) {
        metadata.audio_formats.reserve(formats->size());
        for (const auto &format : *formats) {
            application::model::SongSourceAudioFormat audio_format;
            audio_format.format_type = JsonUtils::getString(format, "formatType");
            if (!audio_format.format_type.empty()) {
                audio_format.resource_type = JsonUtils::getString(format, "resourceType");
                audio_format.iformat = JsonUtils::getString(format, "iformat");
                audio_format.aformat = JsonUtils::getString(format, "aformat");
                audio_format.isize = static_cast<std::uint64_t>(JsonUtils::getInt(format, "isize"));
                audio_format.asize = static_cast<std::uint64_t>(JsonUtils::getInt(format, "asize"));
                metadata.audio_formats.push_back(std::move(audio_format));
            }
        }
    }

    return metadata;
}

namespace {

// 从 Migu 歌曲卡片提取可直接写入新来源表的元数据。
std::optional<application::model::SongSourceMetadata> parseSongSourceFromItem(const nlohmann::json &item, std::uint32_t song_order)
{
    if (const auto *song_item = JsonUtils::getObject(item, "songItem")) {
        return parseSongSourceFromSongObject(*song_item, &item, nullptr, song_order);
    }

    if (!JsonUtils::getString(item, "songName").empty()) {
        return parseSongSourceFromSongObject(item, nullptr, nullptr, song_order);
    }

    return std::nullopt;
}

// 递归收集歌曲来源，sibling_order 用于保留当前数组里的平台返回顺序。
void collectSongSources(const nlohmann::json &node, std::vector<application::model::SongSourceMetadata> &songs, std::uint32_t sibling_order)
{
    if (node.is_array()) {
        std::uint32_t item_order = 1;
        for (const auto &item : node) {
            collectSongSources(item, songs, item_order);
            ++item_order;
        }
        return;
    }

    if (!node.is_object()) {
        return;
    }

    if (auto metadata = parseSongSourceFromItem(node, sibling_order)) {
        songs.push_back(std::move(*metadata));
        return;
    }

    if (const auto *song_items = JsonUtils::getArray(node, "songItems")) {
        std::uint32_t track_order = 1;
        for (const auto &song_item : *song_items) {
            if (auto metadata = parseSongSourceFromSongObject(song_item, nullptr, &node, track_order)) {
                songs.push_back(std::move(*metadata));
            }
            ++track_order;
        }
    }

    if (const auto *data = JsonUtils::getObject(node, "data")) {
        collectSongSources(*data, songs, 0);
    }
    if (const auto *resource = JsonUtils::getArray(node, "resource")) {
        collectSongSources(*resource, songs, 0);
    }
    if (const auto *contents = JsonUtils::getArray(node, "contents")) {
        collectSongSources(*contents, songs, 0);
    }
}

} // namespace

void collectSongSources(const nlohmann::json &node, std::vector<application::model::SongSourceMetadata> &songs)
{
    collectSongSources(node, songs, 0);
}

std::vector<application::model::SongSourceMetadata> parseSongSourcesRaw(const std::vector<char> &raw_data)
{
    std::vector<application::model::SongSourceMetadata> songs;
    auto root_opt = json::JsonUtils::parse(raw_data);
    if (!root_opt) {
        return songs;
    }

    collectSongSources(*root_opt, songs);
    return songs;
}

} // namespace orchestrator::modules::crawler::infrastructure::music::migu::parser::detail
