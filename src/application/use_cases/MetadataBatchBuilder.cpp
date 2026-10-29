#include "crawler/application/use_cases/MetadataBatchBuilder.h"

#include <algorithm>
#include <map>
#include <string>
#include <utility>
#include <tuple>

namespace orchestrator::modules::crawler::application::use_cases {
namespace {

using SourceKey = std::pair<std::string, std::string>; // 平台内的稳定来源身份

// 单个来源 ID 不能把合唱展示文本误存成该歌手的名称。
std::string individualArtistName(const std::string &name)
{
    return name.find('|') == std::string::npos ? name : std::string {};
}

// 按来源与角色合并关系，保留不同入口的 seed 和主歌手信息。
void mergeArtists(std::vector<application::model::SongSourceArtistRef> &target, const std::vector<application::model::SongSourceArtistRef> &incoming)
{
    for (const auto &artist : incoming) {
        auto found = std::find_if(target.begin(), target.end(), [&](const auto &value) {
            return value.source_artist_id == artist.source_artist_id && value.role == artist.role;
        });
        if (found == target.end())
            target.push_back(artist);
        else {
            if (found->source_artist_name.empty()) found->source_artist_name = artist.source_artist_name;
            if (found->source_artist_name_pinyin.empty()) found->source_artist_name_pinyin = artist.source_artist_name_pinyin;
            if (found->source_artist_image.empty()) found->source_artist_image = artist.source_artist_image;
            if (found->artist_order == 0) found->artist_order = artist.artist_order;
            found->is_primary = found->is_primary || artist.is_primary;
            found->is_seed = found->is_seed || artist.is_seed;
        }
    }
}

// 详情字段优先于列表，空值始终不能覆盖同批次的真实资料。
void mergeAlbum(application::model::AlbumSourceMetadata &target, const application::model::AlbumSourceMetadata &incoming)
{
    if (target.source_album_id.empty()) {
        target = incoming;
        return;
    }
    const bool prefer = incoming.detail_sync_status == 2 && target.detail_sync_status != 2;
    auto text = [prefer](std::string &current, const std::string &value) {
        if (!value.empty() && (current.empty() || prefer)) current = value;
    };
    text(target.title, incoming.title);
    text(target.normalized_title, incoming.normalized_title);
    text(target.source_material_id, incoming.source_material_id);
    text(target.source_resource_type, incoming.source_resource_type);
    text(target.source_res_type, incoming.source_res_type);
    text(target.source_title_pinyin, incoming.source_title_pinyin);
    text(target.summary, incoming.summary);
    text(target.total_count, incoming.total_count);
    text(target.release_date, incoming.release_date);
    text(target.cover_url, incoming.cover_url);
    text(target.source_artist_id, incoming.source_artist_id);
    text(target.source_artist_name, incoming.source_artist_name);
    text(target.artist_name, incoming.artist_name);
    if (incoming.source_album_kind != 0 && (target.source_album_kind == 0 || prefer)) target.source_album_kind = incoming.source_album_kind;
    if (incoming.detail_sync_status == 2) target.detail_sync_status = 2;
    if (incoming.songs_sync && (!target.songs_sync || prefer)) target.songs_sync = incoming.songs_sync;
    if (incoming.cover_resource && (!target.cover_resource || prefer)) target.cover_resource = incoming.cover_resource;
    mergeArtists(target.artist_refs, incoming.artist_refs);
}

// 两组平台身份一致的重复片段补充真实非空字段，冲突身份留给 writer 核验稳定绑定。
void mergeSong(application::model::SongSourceMetadata &target, const application::model::SongSourceMetadata &incoming)
{
    auto text = [](std::string &value, const std::string &candidate) { if (value.empty()) value = candidate; };
    text(target.source_song_id, incoming.source_song_id);
    text(target.source_content_id, incoming.source_content_id);
    text(target.source_resource_type, incoming.source_resource_type);
    text(target.source_title_pinyin, incoming.source_title_pinyin);
    text(target.source_album_name, incoming.source_album_name);
    text(target.source_album_material_id, incoming.source_album_material_id);
    text(target.source_album_pinyin, incoming.source_album_pinyin);
    text(target.source_album_cover_url, incoming.source_album_cover_url);
    text(target.source_primary_artist_id, incoming.source_primary_artist_id);
    text(target.source_primary_artist_name, incoming.source_primary_artist_name);
    text(target.cover_url, incoming.cover_url);
    text(target.lyric_url, incoming.lyric_url);
    text(target.mrc_url, incoming.mrc_url);
    text(target.trc_url, incoming.trc_url);
    text(target.copyright_id, incoming.copyright_id);
    text(target.album_disc_name, incoming.album_disc_name);
    if (target.source_duration_seconds == 0) target.source_duration_seconds = incoming.source_duration_seconds;
    if (target.album_disc_no == 0) target.album_disc_no = incoming.album_disc_no;
    if (target.album_track_no == 0) target.album_track_no = incoming.album_track_no;
    if (target.album_song_order == 0) target.album_song_order = incoming.album_song_order;
    mergeArtists(target.artist_refs, incoming.artist_refs);
    for (const auto &format : incoming.audio_formats) {
        auto found = std::find_if(target.audio_formats.begin(), target.audio_formats.end(), [&](const auto &value) { return value.format_type == format.format_type; });
        if (found == target.audio_formats.end())
            target.audio_formats.push_back(format);
        else {
            text(found->resource_type, format.resource_type);
            text(found->iformat, format.iformat);
            text(found->aformat, format.aformat);
            if (found->isize == 0) found->isize = format.isize;
            if (found->asize == 0) found->asize = format.asize;
        }
    }
}

} // namespace

application::model::MetadataWriteBatch buildMetadataWriteBatch(application::model::MetadataWriteBatch input)
{
    using namespace application::model;
    std::map<SourceKey, SingerSourceMetadata> singers;
    std::map<SourceKey, AlbumSourceMetadata> albums;

    // 不同专辑上下文保留独立关系，不能仅按歌曲身份丢弃第二个专辑。
    std::vector<SongSourceMetadata> songs;
    std::map<std::tuple<std::string, std::string, std::string, std::string>, std::size_t> song_positions;
    for (auto &song : input.songs) {
        const auto key = std::make_tuple(song.source, song.source_song_id, song.source_content_id, song.source_album_id);
        auto [position, inserted] = song_positions.emplace(key, songs.size());
        if (inserted)
            songs.push_back(std::move(song));
        else
            mergeSong(songs[position->second], song);
    }
    input.songs = std::move(songs);

    // 同响应的资料字段随实体一起提交，不能留给另一个逐字段补齐入口。
    for (const auto &profile : input.profiles) {
        if (profile.metadata) input.singers.push_back(*profile.metadata);
    }
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
            if (!singer.source_country.empty()) target.source_country = std::move(singer.source_country);
            if (singer.gender != domain::Gender::Unknown) target.gender = singer.gender;
            if (singer.cover_resource) target.cover_resource = std::move(singer.cover_resource);
        }
    }
    for (auto &album : input.albums) {
        mergeAlbum(albums[{album.source, album.source_album_id}], album);
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
            singer.avatar_url = artist.source_artist_image;
            singers.emplace(SourceKey {source, artist.source_artist_id}, std::move(singer));
        } else {
            if (found->second.source_name_pinyin.empty()) found->second.source_name_pinyin = artist.source_artist_name_pinyin;
            if (found->second.avatar_url.empty()) found->second.avatar_url = artist.source_artist_image;
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
            }
            if (album.source_title_pinyin.empty()) album.source_title_pinyin = song.source_album_pinyin;
            if (album.cover_url.empty()) album.cover_url = song.source_album_cover_url;
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
    result.profiles = std::move(input.profiles);
    result.memberships = std::move(input.memberships);
    for (auto &[key, singer] : singers) result.singers.push_back(std::move(singer));
    for (auto &[key, album] : albums) result.albums.push_back(std::move(album));
    return result;
}

} // namespace orchestrator::modules::crawler::application::use_cases
