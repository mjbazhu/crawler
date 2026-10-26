#include "MySqlMetadataWriteSession.h"

#include <string>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::detail {

boost::asio::awaitable<void> MetadataWriteSession::saveMembership(const application::model::SingerIndexMembership &membership)
{
    const auto singer = co_await singerId(membership.source, membership.source_singer_id);
    co_await write("singer_index_memberships/upsert", singer, membership.tab, membership.index_order, membership.source_tag, membership.source_metric);
}

boost::asio::awaitable<void> MetadataWriteSession::saveCover(const std::string &table, std::uint64_t owner, const application::model::MetadataResourceFile &cover)
{
    if (cover.status == 2) {
        auto find = co_await statement(table + "/find_current");
        boost::mysql::results previous;
        co_await connection_->async_execute(find.bind(owner), previous, boost::asio::use_awaitable);
        if (!previous.rows().empty()) {
            const auto path = MySqlValueReader::stringAt(previous.rows().front(), 0);
            if (!path.empty() && path != cover.local_path) replaced_cover_paths_.push_back(path);
        }
    }
    co_await write(table + "/upsert", owner, cover.local_path, cover.size_bytes, cover.checksum, cover.status, cover.error_category, cover.result_code, cover.last_error, cover.status);
}

boost::asio::awaitable<void> MetadataWriteSession::saveSingerResource(const application::model::SingerSourceMetadata &singer)
{
    if (singer.cover_resource) co_await saveCover("singer_resources", co_await singerId(singer.source, singer.source_singer_id), *singer.cover_resource);
}

boost::asio::awaitable<void> MetadataWriteSession::saveAlbumRelations(const application::model::AlbumSourceMetadata &album)
{
    const auto owner = co_await albumId(album.source, album.source_album_id);
    for (const auto &artist : album.artist_refs) {
        if (artist.source_artist_id.empty()) continue;
        const auto singer = co_await singerId(album.source, artist.source_artist_id);
        co_await write("album_artist_refs/upsert", owner, singer, artist.role.empty() ? std::string("artist") : artist.role, artist.artist_order, artist.is_primary ? 1 : 0, artist.is_seed ? 1 : 0);
    }
    if (album.cover_resource) co_await saveCover("album_resources", owner, *album.cover_resource);
}

boost::asio::awaitable<void> MetadataWriteSession::saveSongRelations(const application::model::SongSourceMetadata &song)
{
    const std::string key = song.source_content_id.empty() ? "song:" + song.source_song_id : "content:" + song.source_content_id;
    const auto owner = song_ids_.at({song.source, key});
    for (const auto &artist : song.artist_refs) {
        if (artist.source_artist_id.empty()) continue;
        const auto singer = co_await singerId(song.source, artist.source_artist_id);
        co_await write("song_artist_refs/upsert", owner, singer, artist.role.empty() ? std::string("artist") : artist.role, artist.artist_order, artist.is_primary ? 1 : 0, artist.is_seed ? 1 : 0);
    }
    if (!song.source_album_id.empty()) {
        const auto album = co_await albumId(song.source, song.source_album_id);
        co_await write("album_song_refs/upsert", album, owner, song.album_disc_no, song.album_disc_name, song.album_track_no, song.album_song_order);
    }
    bool has_audio = false;
    for (const auto &format : song.audio_formats) {
        if (format.format_type.empty()) continue;
        has_audio = true;
        co_await write("song_source_audio_formats/upsert", owner, format.format_type, format.resource_type, format.iformat, format.aformat, format.isize, format.asize);
        co_await write("song_resources/upsert", owner, std::string("audio"), format.format_type);
    }
    if (!has_audio) co_await write("song_resources/upsert_missing_audio", owner);
    for (const auto &[kind, url] : {
             std::pair<std::string, std::string> {"cover", song.cover_url}, {"lyric", song.lyric_url}, {"mrc", song.mrc_url}, {"trc", song.trc_url}
         }) {
        if (!url.empty()) co_await write("song_resources/upsert", owner, kind, std::string {});
    }
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::detail
