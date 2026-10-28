#include "MySqlMetadataWriteSession.h"

#include <algorithm>
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
    // 名称条目可共用；来源身份与类型只能按平台歌手 ID 更新。
    const auto common = co_await write("singers/upsert_metadata", singer.name, singer.normalized_name.empty() ? singer.name : singer.normalized_name);
    const auto id = co_await write("singer_source_refs/upsert", common, singer.source, singer.source_singer_id, static_cast<int>(singer.gender), singer.source_name_pinyin, singer.source_country);
    singer_ids_[{singer.source, singer.source_singer_id}] = id;
}

boost::asio::awaitable<void> MetadataWriteSession::saveSingerProfile(const application::model::SingerSourceProfile &profile)
{
    // 缺失端点必须显式失败，不能成功提交一条没有更新对象的声明总数。
    (void)co_await singerId(profile.source, profile.source_singer_id);
    co_await write("singer_source_refs/update_migu_singer_source_profile_counts", profile.source_song_total ? 1 : 0, profile.source_song_total.value_or(0), profile.source_album_total ? 1 : 0, profile.source_album_total.value_or(0), profile.source, profile.source_singer_id);
}

boost::asio::awaitable<void> MetadataWriteSession::saveAlbum(const application::model::AlbumSourceMetadata &album)
{
    if (album.source.empty() || album.source_album_id.empty() || album.title.empty()) throw std::invalid_argument("album source is missing identity or title");
    // 来源 ID 从列表到详情保持稳定，后到的 materialId 不得改变公共实体身份。
    const std::string canonical = album.source_album_id;
    const auto common = co_await write("albums/upsert_metadata", album.title, album.normalized_title.empty() ? album.title : album.normalized_title, album.release_date, album.source, canonical);
    std::string resource_type = album.source_resource_type;
    // 兼容显式携带旧列表类型的输入；未知摘要不能推定为普通专辑。
    if (resource_type.empty() && !album.source_res_type.empty()) resource_type = album.source_res_type == "1" ? "5" : "2003";
    const auto id = co_await write("album_source_refs/upsert", common, album.source, album.source_album_id, resource_type, album.source_album_kind, album.source_title_pinyin, album.summary, declaredCount(album.total_count), album.detail_sync_status, album.detail_sync_status, album.songs_sync ? album.songs_sync->status : 0, album.songs_sync ? album.songs_sync->received_count : 0, album.songs_sync ? album.songs_sync->error : std::string {}, album.songs_sync ? 1 : 0);
    album_ids_[{album.source, album.source_album_id}] = id;
    if (!album.source_material_id.empty()) co_await write("migu_dalbum_details/upsert", id, album.source_material_id);
}

boost::asio::awaitable<void> MetadataWriteSession::saveSong(const application::model::SongSourceMetadata &song)
{
    if (song.source.empty() || song.title.empty() || (song.source_content_id.empty() && song.source_song_id.empty())) throw std::invalid_argument("song source is missing identity or title");
    const auto common = co_await write("songs/upsert_metadata", song.title, song.normalized_title.empty() ? song.title : song.normalized_title);
    const auto id = co_await write("song_source_refs/upsert", common, song.source, song.source_song_id, song.source_content_id, song.source_resource_type, song.source_title_pinyin, song.source_duration_seconds, song.copyright_id);
    song_ids_[{song.source, song.source_song_id, song.source_content_id}] = id;
    // 同一 songId 的不同入口可能携带另一 contentId；以锁内读取的既有绑定为准。
    auto prepared = co_await statement("song_source_refs/select_metadata_identity");
    boost::mysql::results identity;
    co_await connection_->async_execute(prepared.bind(id), identity, boost::asio::use_awaitable);
    if (identity.rows().empty()) throw std::runtime_error("saved song source identity is missing");
    const application::model::PersistedSongSource saved {id, song.source, MySqlValueReader::stringAt(identity.rows().front(), 0), MySqlValueReader::stringAt(identity.rows().front(), 1)};
    const auto previous = std::find_if(saved_songs_.begin(), saved_songs_.end(), [id](const auto &value) { return value.source_ref_id == id; });
    if (previous == saved_songs_.end())
        saved_songs_.push_back(saved);
    else
        *previous = saved;
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
