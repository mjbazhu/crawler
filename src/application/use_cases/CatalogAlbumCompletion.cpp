#include "crawler/application/use_cases/CatalogMetadataCompletion.h"

#include "AlbumCollectionCompletion.h"
#include "AlbumTrackCollection.h"
#include "crawler/application/use_cases/MetadataBatchBuilder.h"

#include <algorithm>
#include <set>
#include <utility>

namespace orchestrator::modules::crawler::application::use_cases {

bool CatalogMetadataCompletion::prepareAlbumResponse(Batch &batch, const application::model::AlbumSourceMetadata &album, std::size_t &bytes)
{
    const auto found = std::find_if(batch.albums.begin(), batch.albums.end(), [&](const auto &item) {
        return item.source == album.source && item.detail_sync_status == 2 && (item.source_album_id == album.source_album_id || item.source_material_id == album.source_album_id);
    });
    if (found == batch.albums.end()) return false;
    Batch related;
    related.albums.push_back(*found);
    auto &target = related.albums.front();
    for (auto &song : batch.songs) {
        // 目标专辑已返回的曲目属于本次写入；只排除其他专辑的推荐内容。
        if (song.source != target.source || (song.source_album_id != target.source_album_id && song.source_album_id != target.source_material_id)) continue;
        if (song.source_album_id != target.source_album_id) song.source_album_material_id = song.source_album_id;
        song.source_album_id = target.source_album_id;
        if (song.source_album_name.empty()) song.source_album_name = target.title;
        related.songs.push_back(std::move(song));
    }
    std::string error;
    if (dependencies_.albums)
        error = collectRemainingAlbumTracks(target, related, *dependencies_.albums, *dependencies_.parser, &bytes);
    else if (const auto expected = declaredAlbumSongCount(target.total_count); expected && albumSongSyncResult(target, related.songs).received_count < *expected)
        error = "album track provider unavailable";
    target.songs_sync = albumSongSyncResult(target, related.songs);
    if (!error.empty()) {
        target.songs_sync->status = 4;
        target.songs_sync->error = error;
    }
    related = buildMetadataWriteBatch(std::move(related));
    // 保留目标专辑及曲目确实引用的完整歌手资料，不启动关联歌手目录采集。
    std::set<std::pair<std::string, std::string>> artists;
    for (const auto &item : related.albums) {
        if (!item.source_artist_id.empty()) artists.emplace(item.source, item.source_artist_id);
        for (const auto &artist : item.artist_refs) artists.emplace(item.source, artist.source_artist_id);
    }
    for (const auto &song : related.songs) {
        if (!song.source_primary_artist_id.empty()) artists.emplace(song.source, song.source_primary_artist_id);
        for (const auto &artist : song.artist_refs) artists.emplace(song.source, artist.source_artist_id);
    }
    const auto mentioned = [&](const auto &singer) {
        return artists.contains({singer.source, singer.source_singer_id});
    };
    for (auto &singer : batch.singers)
        if (mentioned(singer)) related.singers.push_back(std::move(singer));
    for (auto &profile : batch.profiles)
        if (mentioned(profile)) related.profiles.push_back(std::move(profile));
    batch = buildMetadataWriteBatch(std::move(related));
    return error.empty();
}

application::result::ServiceResult<application::model::MetadataWriteBatch> CatalogMetadataCompletion::completeAlbums(application::model::MetadataWriteBatch batch)
{
    using Result = application::result::ServiceResult<Batch>;
    batch = buildMetadataWriteBatch(std::move(batch));
    const auto requested = batch.albums;
    std::set<std::pair<std::string, std::string>> reused;                 // 复用旧详情不能冒充一次资料刷新
    std::map<std::pair<std::string, std::string>, std::string> canonical; // 只接受明确的来源身份映射
    for (const auto &album : requested) {
        // 共享归档先于缓存和旧库复用判断，避免把其他歌手的曲目带入本批次。
        if (isSharedReleaseArchive(album)) continue;
        std::optional<BatchResult> fetched;
        if (album.detail_sync_status == 2) {
            // 上游已返回详情时直接复用，只有曲目缺页才发请求。
            Batch provided = batch;
            provided.albums = {album};
            std::size_t bytes = 0;
            (void)prepareAlbumResponse(provided, album, bytes);
            fetched = BatchResult::success(std::make_shared<const Batch>(std::move(provided)));
        }
        const auto key = "album:" + album.source + ":" + album.source_album_id;
        if (!fetched) fetched = cached(key);
        if (dependencies_.catalog) {
            const auto stored = dependencies_.catalog->findAlbum(album.source, album.source_album_id);
            if (stored && stored->detail_sync_status == 2 && stored->songs_sync_status == 2 && album.detail_sync_status != 2) {
                reused.emplace(stored->source, stored->source_album_id);
                auto retained = album;
                retained.source_album_id = stored->source_album_id;
                if (retained.source_album_id != album.source_album_id) {
                    retained.source_material_id = album.source_album_id;
                    canonical[{album.source, album.source_album_id}] = retained.source_album_id;
                }
                retained.title = stored->title;
                retained.normalized_title = stored->normalized_title;
                retained.release_date = stored->release_date;
                retained.source_resource_type = stored->source_resource_type;
                retained.source_album_kind = stored->source_album_kind;
                retained.source_title_pinyin = stored->source_title_pinyin;
                retained.summary = stored->summary;
                retained.total_count = stored->total_count;
                retained.detail_sync_status = 2; // 同批合并保护详情字段，提交前恢复为复用语义
                batch.albums.push_back(std::move(retained));
                // 仅两项均已核实才复用；不让旧缓存改写其他任务已完成的曲目状态。
                continue;
            }
        }
        if (!fetched) {
            if (!dependencies_.albums) return Result::failure(application::result::ServiceErrorType::InternalError, "album detail provider unavailable");
            fetched = response(key, [&] { return dependencies_.albums->fetchAlbumDetail(album.source_album_id, album.source_res_type); }, false, &album);
        }
        if (!fetched->isSuccess()) return Result::failure(fetched->error(), fetched->message());
        const auto &details = **fetched->data();
        const auto target = std::find_if(details.albums.begin(), details.albums.end(), [&](const auto &item) {
            return item.source == album.source && item.detail_sync_status == 2 && (item.source_album_id == album.source_album_id || item.source_material_id == album.source_album_id);
        });
        if (target == details.albums.end()) {
            forget(key, *fetched->data());
            return Result::failure(application::result::ServiceErrorType::ExternalApiError, "album detail identity missing or mismatched: " + album.source_album_id);
        }
        if (target->source_album_id != album.source_album_id) canonical[{album.source, album.source_album_id}] = target->source_album_id;
        batch.albums.push_back(*target);
        batch.songs.insert(batch.songs.end(), details.songs.begin(), details.songs.end());
        batch.singers.insert(batch.singers.end(), details.singers.begin(), details.singers.end());
        batch.profiles.insert(batch.profiles.end(), details.profiles.begin(), details.profiles.end());
    }
    for (auto &album : batch.albums) {
        if (const auto found = canonical.find({album.source, album.source_album_id}); found != canonical.end()) {
            album.source_material_id = album.source_album_id;
            album.source_album_id = found->second;
        }
    }
    for (auto &song : batch.songs) {
        if (const auto found = canonical.find({song.source, song.source_album_id}); found != canonical.end()) {
            song.source_album_material_id = song.source_album_id;
            song.source_album_id = found->second;
        }
    }
    batch = buildMetadataWriteBatch(std::move(batch));
    for (auto &album : batch.albums) {
        if (reused.contains({album.source, album.source_album_id})) album.detail_sync_status = 0;
    }
    return Result::success(std::move(batch));
}

} // namespace orchestrator::modules::crawler::application::use_cases
