#include "AlbumTrackCollection.h"

#include "AlbumCollectionCompletion.h"

#include <set>
#include <utility>

namespace orchestrator::modules::crawler::application::use_cases {
namespace {

constexpr int kPageSize = 50;  // 有界曲目页，与详情返回的完整首段对齐
constexpr int kMaxPages = 400; // 防止异常声明总数导致无限请求

// 在同一采集任务内按歌曲来源身份去重，不从历史关系推算完成数。
std::string songIdentity(const application::model::SongSourceMetadata &song)
{
    return song.source_content_id.empty() ? "song:" + song.source_song_id : "content:" + song.source_content_id;
}

} // namespace

std::string collectRemainingAlbumTracks(const application::model::AlbumSourceMetadata &album, application::model::MetadataWriteBatch &batch, application::ports::AlbumDetailProvider &provider, ParserDispatch &parser, std::size_t *response_bytes)
{
    const auto expected = declaredAlbumSongCount(album.total_count);
    const auto initial = albumSongSyncResult(album, batch.songs);
    if (!expected || initial.received_count >= *expected) return {};
    if (*expected > kMaxPages * kPageSize) return "album song count exceeds bounded pagination";
    std::set<std::string> seen;
    for (const auto &song : batch.songs) {
        if (song.source == album.source && song.source_album_id == album.source_album_id) seen.insert(songIdentity(song));
    }
    // 数字专辑只使用详情已明确返回的物料身份，不靠名称或 ID 外观猜测。
    const auto page_album_id = album.source_material_id.empty() ? album.source_album_id : album.source_material_id;
    const int first_page = initial.received_count / kPageSize + 1;
    for (int number = first_page; seen.size() < static_cast<std::size_t>(*expected) && number <= kMaxPages; ++number) {
        auto response = provider.fetchAlbumSongPage(page_album_id, number, kPageSize);
        if (!response || response->status_code < 200 || response->status_code >= 300) return "album song page " + std::to_string(number) + " fetch failed";
        if (response_bytes) *response_bytes += response->body.size();
        auto page = parser.parseAlbumSongPage(std::move(response->body));
        if (!page.source_song_total || *page.source_song_total != *expected) return "album song page total missing or changed";
        if (page.returned_count > kPageSize) return "album song page exceeds requested size";
        for (const auto &song : page.songs) {
            if (song.source != album.source || (song.source_album_id != album.source_album_id && song.source_album_id != page_album_id)) return "album song page identity mismatch";
        }
        const auto before = seen.size();
        for (auto &song : page.songs) {
            if (!seen.insert(songIdentity(song)).second) continue;
            song.source_album_id = album.source_album_id;
            song.source_album_name = album.title;
            song.album_song_order += static_cast<std::uint32_t>((number - 1) * kPageSize);
            if (song.album_track_no == 0) song.album_track_no = song.album_song_order;
            batch.songs.push_back(std::move(song));
        }
        if (seen.size() == static_cast<std::size_t>(*expected)) return {};
        if (seen.size() > static_cast<std::size_t>(*expected)) return "album song page exceeds declared total";
        if (seen.size() == before) return "album song pagination made no progress";
        if (page.returned_count < kPageSize) return "album song pages ended before declared total";
    }
    return "album song pagination limit reached";
}

} // namespace orchestrator::modules::crawler::application::use_cases
