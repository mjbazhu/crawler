#include "crawler/application/use_cases/CatalogMetadataCompletion.h"

#include "crawler/application/use_cases/MetadataBatchBuilder.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <set>
#include <utility>
#include <vector>

namespace orchestrator::modules::crawler::application::use_cases {
namespace {

// 不同内容版本不能仅因 songId 相同而互相补字段。
bool sameSong(const application::model::SongSourceMetadata &left, const application::model::SongSourceMetadata &right)
{
    if (left.source != right.source) return false;
    if (!left.source_content_id.empty() && !right.source_content_id.empty()) return left.source_content_id == right.source_content_id;
    return !left.source_song_id.empty() && left.source_song_id == right.source_song_id;
}

} // namespace

CatalogMetadataCompletion::CatalogMetadataCompletion(CatalogMetadataCompletionDependencies dependencies) : dependencies_(std::move(dependencies))
{}

void CatalogMetadataCompletion::forget(const std::string &key, const std::shared_ptr<const Batch> &invalid)
{
    std::lock_guard lock(mutex_);
    if (const auto found = responses_.find(key); found != responses_.end()) {
        if (found->second.result.wait_for(std::chrono::seconds(0)) != std::future_status::ready) return;
        const auto current = found->second.result.get();
        if (current.isSuccess() && *current.data() != invalid) return;
        cached_bytes_ -= found->second.bytes;
        responses_.erase(found);
    }
}

std::optional<CatalogMetadataCompletion::BatchResult> CatalogMetadataCompletion::cached(const std::string &key)
{
    std::shared_future<BatchResult> pending;
    {
        std::lock_guard lock(mutex_);
        const auto found = responses_.find(key);
        if (found == responses_.end()) return std::nullopt;
        pending = found->second.result;
    }
    return pending.get();
}

CatalogMetadataCompletion::BatchResult CatalogMetadataCompletion::response(const std::string &key, const std::function<std::optional<application::model::ProviderResponse>()> &fetch, bool refresh)
{
    auto promise = std::make_shared<std::promise<BatchResult>>();
    std::shared_future<BatchResult> pending;
    bool owner = false;
    {
        std::lock_guard lock(mutex_);
        auto found = responses_.find(key);
        if (refresh && found != responses_.end() && found->second.result.wait_for(std::chrono::seconds(0)) == std::future_status::ready) {
            cached_bytes_ -= found->second.bytes;
            responses_.erase(found);
            found = responses_.end();
        }
        if (found != responses_.end())
            pending = found->second.result;
        else {
            pending = promise->get_future().share();
            responses_.emplace(key, CachedResponse {pending, 0});
            owner = true;
        }
    }
    if (!owner) return pending.get();
    using Error = application::result::ServiceErrorType;
    auto result = BatchResult::failure(Error::ExternalApiError, "metadata response fetch failed: " + key);
    std::size_t bytes = 0;
    try {
        const auto fetched = fetch();
        if (fetched && fetched->status_code >= 200 && fetched->status_code < 300 && dependencies_.parser) {
            bytes = fetched->body.size();
            auto batch = dependencies_.parser->parseSources(fetched->body);
            if (!batch.singers.empty() || !batch.albums.empty() || !batch.songs.empty() || !batch.profiles.empty()) result = BatchResult::success(std::make_shared<const Batch>(std::move(batch)));
        }
    } catch (...) {
        result = BatchResult::failure(Error::ExternalApiError, "metadata response interrupted: " + key);
    }
    {
        std::lock_guard lock(mutex_);
        if (result.isSuccess()) {
            responses_.at(key).bytes = bytes;
            cached_bytes_ += bytes;
        }
        promise->set_value(result);
        if (!result.isSuccess())
            responses_.erase(key);
        else {
            // 只淘汰已完成响应，等待者持有 future，不受缓存淘汰影响。
            while (responses_.size() > 64 || cached_bytes_ > 8 * 1024 * 1024) {
                auto found = std::find_if(responses_.begin(), responses_.end(), [](const auto &entry) { return entry.second.result.wait_for(std::chrono::seconds(0)) == std::future_status::ready; });
                if (found == responses_.end()) break;
                cached_bytes_ -= found->second.bytes;
                responses_.erase(found);
            }
        }
    }
    return result;
}

application::result::ServiceResult<application::model::SingerSourceProfile> CatalogMetadataCompletion::singerContext(std::uint64_t singer_id, std::optional<int> songs, std::optional<int> albums, bool refresh)
{
    using Result = application::result::ServiceResult<application::model::SingerSourceProfile>;
    application::model::SingerSourceProfile profile;
    profile.source_singer_id = std::to_string(singer_id);
    profile.source_song_total = songs;
    profile.source_album_total = albums;
    if (!refresh && profile.source_song_total && profile.source_album_total) return Result::success(std::move(profile));
    if (!refresh && dependencies_.catalog) {
        if (const auto stored = dependencies_.catalog->findSinger("migu", profile.source_singer_id)) {
            if (stored->source_song_total) profile.source_song_total = stored->source_song_total;
            if (stored->source_album_total) profile.source_album_total = stored->source_album_total;
        }
    }
    if (!refresh && profile.source_song_total && profile.source_album_total) return Result::success(std::move(profile));
    if (!dependencies_.singers) return Result::failure(application::result::ServiceErrorType::InternalError, "singer profile provider unavailable");
    const auto fetched = response("singer:migu:" + profile.source_singer_id, [&] { return dependencies_.singers->fetchSingerInfo(singer_id); }, refresh);
    if (!fetched.isSuccess()) return Result::failure(fetched.error(), fetched.message());
    const auto &profiles = (*fetched.data())->profiles;
    const auto found = std::find_if(profiles.begin(), profiles.end(), [&](const auto &item) { return item.source == "migu" && item.source_singer_id == profile.source_singer_id; });
    if (found == profiles.end()) {
        forget("singer:migu:" + profile.source_singer_id, *fetched.data());
        return Result::failure(application::result::ServiceErrorType::ExternalApiError, "singer profile identity missing or mismatched");
    }
    if (found->source_song_total) profile.source_song_total = found->source_song_total;
    if (found->source_album_total) profile.source_album_total = found->source_album_total;
    profile.metadata = found->metadata;
    return Result::success(std::move(profile));
}

application::result::ServiceResult<application::model::MetadataWriteBatch> CatalogMetadataCompletion::completeAlbums(application::model::MetadataWriteBatch batch)
{
    using Result = application::result::ServiceResult<Batch>;
    batch = buildMetadataWriteBatch(std::move(batch));
    const auto requested = batch.albums;
    const auto selected_songs = batch.songs;
    std::set<std::pair<std::string, std::string>> reused;                 // 复用旧详情不能冒充新一次同步
    std::map<std::pair<std::string, std::string>, std::string> canonical; // 仅接受来源详情或数据库明确给出的别名映射
    for (const auto &album : requested) {
        if (album.detail_sync_status == 2) continue;
        const auto key = "album:" + album.source + ":" + album.source_album_id;
        auto fetched = cached(key);
        bool retained_details = false;
        if (dependencies_.catalog) {
            const auto stored = dependencies_.catalog->findAlbum(album.source, album.source_album_id);
            if (stored && stored->detail_sync_status == 2) {
                reused.emplace(stored->source, stored->source_album_id);
                {
                    // 服务重启或缓存淘汰后复用数据库资料，列表摘要不能降低详情字段。
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
                    retained.detail_sync_status = 2; // 合并时详情字段仍优先，提交前恢复为复用语义
                    batch.albums.push_back(std::move(retained));
                    retained_details = true;
                }
                if (!fetched) continue;
            }
        }
        if (!fetched) {
            if (!dependencies_.albums) return Result::failure(application::result::ServiceErrorType::InternalError, "album detail provider unavailable");
            fetched = response(key, [&] { return dependencies_.albums->fetchAlbumDetail(album.source_album_id, album.source_res_type); });
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
        if (!retained_details) batch.albums.push_back(*target);
        for (const auto &song : details.songs) {
            // 采集顺序和 limit 由入口控制，详情不引入无关曲目或推荐歌手。
            if (song.source != target->source || (song.source_album_id != target->source_album_id && song.source_album_id != target->source_material_id)) continue;
            if (std::any_of(selected_songs.begin(), selected_songs.end(), [&](const auto &selected) { return sameSong(selected, song); })) batch.songs.push_back(song);
        }
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
