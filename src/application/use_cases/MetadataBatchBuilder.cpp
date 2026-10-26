#include "crawler/application/use_cases/MetadataBatchBuilder.h"

#include <map>
#include <string>
#include <utility>

namespace orchestrator::modules::crawler::application::use_cases {
namespace {

using SourceKey = std::pair<std::string, std::string>; // 平台内的稳定来源身份

// 单个来源 ID 不能把合唱展示文本误存成该歌手的名称。
std::string individualArtistName(const std::string &name)
{
    return name.find('|') == std::string::npos ? name : std::string {};
}

} // namespace

application::model::MetadataWriteBatch buildMetadataWriteBatch(application::model::MetadataWriteBatch input)
{
    using namespace application::model;
    std::map<SourceKey, SingerSourceMetadata> singers;
    std::map<SourceKey, AlbumSourceMetadata> albums;

    // 显式来源数据优先，歌曲中的关系片段只补充缺失的字段。
    for (auto &singer : input.singers) {
        if (!singer.source_index_tab.empty()) {
            input.memberships.push_back({singer.source, singer.source_singer_id, singer.source_index_tab, singer.source_index_order, singer.source_tag, singer.source_metric});
        }
        auto &target = singers[{singer.source, singer.source_singer_id}];
        if (target.name.empty())
            target = std::move(singer);
        else {
            if (!singer.source_name_pinyin.empty()) target.source_name_pinyin = std::move(singer.source_name_pinyin);
            if (!singer.avatar_url.empty()) target.avatar_url = std::move(singer.avatar_url);
        }
    }
    for (auto &album : input.albums) {
        albums[{album.source, album.source_album_id}] = std::move(album);
    }

    // 把来源歌手属性独立归入 singers/singer_source_refs 的输入。
    auto add_artist = [&singers](const std::string &source, const SongSourceArtistRef &artist) {
        if (artist.source_artist_id.empty()) return;
        const std::string name = individualArtistName(artist.source_artist_name);
        auto found = singers.find({source, artist.source_artist_id});
        if (found == singers.end()) {
            if (name.empty()) return; // 仓储可查询已有 ID，禁止伪造空名称歌手。
            SingerSourceMetadata singer;
            singer.source = source;
            singer.source_singer_id = artist.source_artist_id;
            singer.name = name;
            singer.normalized_name = name;
            singer.source_name_pinyin = artist.source_artist_name_pinyin;
            singers.emplace(SourceKey {source, artist.source_artist_id}, std::move(singer));
        } else if (!artist.source_artist_name_pinyin.empty()) {
            found->second.source_name_pinyin = artist.source_artist_name_pinyin;
        }
    };

    for (auto &song : input.songs) {
        if (song.artist_refs.empty() && !song.source_primary_artist_id.empty()) {
            SongSourceArtistRef artist;
            artist.source_artist_id = song.source_primary_artist_id;
            artist.source_artist_name = song.source_primary_artist_name;
            artist.is_primary = true;
            artist.artist_order = 1;
            song.artist_refs.push_back(std::move(artist));
        }
        for (const auto &artist : song.artist_refs) add_artist(song.source, artist);

        if (!song.source_album_id.empty() && !song.source_album_name.empty()) {
            auto &album = albums[{song.source, song.source_album_id}];
            if (album.source_album_id.empty()) {
                album.source = song.source;
                album.source_album_id = song.source_album_id;
                album.title = song.source_album_name;
                album.normalized_title = song.source_album_name;
                album.source_resource_type = "2003";
            }
            if (album.source_title_pinyin.empty()) album.source_title_pinyin = song.source_album_pinyin;
            if (album.cover_url.empty()) album.cover_url = song.cover_url;
            if (!song.source_album_material_id.empty() && song.source_album_material_id != album.source_album_id) {
                album.source_material_id = song.source_album_material_id;
            }
        }
    }

    // 专辑自身给出的歌手才属于专辑歌手关系，不从任意歌曲主唱推断专辑主唱。
    for (auto &[key, album] : albums) {
        if (album.artist_refs.empty() && !album.source_artist_id.empty()) {
            SongSourceArtistRef artist;
            artist.source_artist_id = album.source_artist_id;
            artist.source_artist_name = album.source_artist_name.empty() ? album.artist_name : album.source_artist_name;
            artist.artist_order = 1;
            artist.is_primary = true;
            album.artist_refs.push_back(std::move(artist));
        }
        for (const auto &artist : album.artist_refs) add_artist(album.source, artist);
    }

    MetadataWriteBatch result;
    result.songs = std::move(input.songs);
    result.memberships = std::move(input.memberships);
    for (auto &[key, singer] : singers) result.singers.push_back(std::move(singer));
    for (auto &[key, album] : albums) result.albums.push_back(std::move(album));
    return result;
}

} // namespace orchestrator::modules::crawler::application::use_cases
