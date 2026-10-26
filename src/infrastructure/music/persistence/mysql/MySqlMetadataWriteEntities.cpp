#include "MySqlMetadataWriteSession.h"

#include <charconv>
#include <limits>
#include <stdexcept>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::detail {
namespace {

// 数量未知时保留 NULL，禁止把无法解析的总数标记为零。
int declaredCount(const std::string &value)
{
    int count = -1;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), count);
    return parsed.ec == std::errc {} && parsed.ptr == value.data() + value.size() && count >= 0 ? count : -1;
}

} // namespace

MetadataWriteSession::MetadataWriteSession(boost::mysql::pooled_connection &connection, const std::map<std::string, std::string> &scripts) : connection_(connection), scripts_(scripts)
{}

boost::asio::awaitable<boost::mysql::statement> MetadataWriteSession::statement(const std::string &key)
{
    auto found = statements_.find(key);
    if (found != statements_.end()) co_return found->second;
    auto prepared = co_await connection_->async_prepare_statement(scripts_.at(key), boost::asio::use_awaitable);
    statements_.emplace(key, prepared);
    co_return prepared;
}

boost::asio::awaitable<void> MetadataWriteSession::saveSinger(const application::model::SingerSourceMetadata &singer)
{
    if (singer.source.empty() || singer.source_singer_id.empty() || singer.name.empty()) throw std::invalid_argument("singer source is missing identity or name");
    const auto common = co_await write("singers/upsert_metadata", singer.name, singer.normalized_name.empty() ? singer.name : singer.normalized_name, static_cast<int>(singer.gender));
    const auto id = co_await write("singer_source_refs/upsert", common, singer.source, singer.source_singer_id, singer.source_name_pinyin, singer.source_country);
    singer_ids_[{singer.source, singer.source_singer_id}] = id;
}

boost::asio::awaitable<void> MetadataWriteSession::saveAlbum(const application::model::AlbumSourceMetadata &album)
{
    if (album.source.empty() || album.source_album_id.empty() || album.title.empty()) throw std::invalid_argument("album source is missing identity or title");
    // 来源 ID 从列表到详情保持稳定，后到的 materialId 不得改变公共实体身份。
    const std::string canonical = album.source_album_id;
    const auto common = co_await write("albums/upsert_metadata", album.title, album.normalized_title.empty() ? album.title : album.normalized_title, album.release_date, album.source, canonical);
    const std::string resource_type = album.source_resource_type.empty() ? (album.source_res_type == "1" ? "5" : "2003") : album.source_resource_type;
    const auto id = co_await write("album_source_refs/upsert", common, album.source, album.source_album_id, resource_type, album.source_album_kind, album.source_title_pinyin, album.summary, declaredCount(album.total_count), album.detail_sync_status, album.detail_sync_status);
    album_ids_[{album.source, album.source_album_id}] = id;
    if (!album.source_material_id.empty()) co_await write("migu_dalbum_details/upsert", id, album.source_material_id);
}

boost::asio::awaitable<void> MetadataWriteSession::saveSong(const application::model::SongSourceMetadata &song)
{
    if (song.source.empty() || song.title.empty() || (song.source_content_id.empty() && song.source_song_id.empty())) throw std::invalid_argument("song source is missing identity or title");
    const auto common = co_await write("songs/upsert_metadata", song.title, song.normalized_title.empty() ? song.title : song.normalized_title);
    const std::string key = song.source_content_id.empty() ? "song:" + song.source_song_id : "content:" + song.source_content_id;
    const auto id = co_await write("song_source_refs/upsert", common, song.source, song.source_song_id, song.source_content_id, song.source_resource_type, song.source_title_pinyin, song.source_duration_seconds, song.copyright_id);
    song_ids_[{song.source, key}] = id;
}

boost::asio::awaitable<std::uint64_t> MetadataWriteSession::singerId(const std::string &source, const std::string &id)
{
    const auto key = SourceKey {source, id};
    if (const auto found = singer_ids_.find(key); found != singer_ids_.end()) co_return found->second;
    auto prepared = co_await statement("singer_source_refs/find_source_id");
    boost::mysql::results result;
    co_await connection_->async_execute(prepared.bind(source, id), result, boost::asio::use_awaitable);
    if (result.rows().empty()) throw std::runtime_error("singer relation endpoint is missing");
    const auto value = MySqlValueReader::uint64At(result.rows().front(), 0);
    singer_ids_[key] = value;
    co_return value;
}

boost::asio::awaitable<std::uint64_t> MetadataWriteSession::albumId(const std::string &source, const std::string &id)
{
    const auto key = SourceKey {source, id};
    if (const auto found = album_ids_.find(key); found != album_ids_.end()) co_return found->second;
    auto prepared = co_await statement("album_source_refs/find_source_id");
    boost::mysql::results result;
    co_await connection_->async_execute(prepared.bind(source, id), result, boost::asio::use_awaitable);
    if (result.rows().empty()) throw std::runtime_error("album relation endpoint is missing");
    const auto value = MySqlValueReader::uint64At(result.rows().front(), 0);
    album_ids_[key] = value;
    co_return value;
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::detail
