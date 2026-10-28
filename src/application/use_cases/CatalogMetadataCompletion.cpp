#include "crawler/application/use_cases/CatalogMetadataCompletion.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <utility>

namespace orchestrator::modules::crawler::application::use_cases {
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

CatalogMetadataCompletion::BatchResult CatalogMetadataCompletion::response(const std::string &key, const std::function<std::optional<application::model::ProviderResponse>()> &fetch, bool refresh, const application::model::AlbumSourceMetadata *album)
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
    bool cacheable = true; // 分页失败保留已解析数据，但不能固定成后续调用的完整缓存。
    try {
        const auto fetched = fetch();
        if (fetched && fetched->status_code >= 200 && fetched->status_code < 300 && dependencies_.parser) {
            bytes = fetched->body.size();
            auto batch = dependencies_.parser->parseSources(fetched->body);
            if (album) cacheable = prepareAlbumResponse(batch, *album, bytes);
            if (!batch.singers.empty() || !batch.albums.empty() || !batch.songs.empty() || !batch.profiles.empty()) result = BatchResult::success(std::make_shared<const Batch>(std::move(batch)));
        }
    } catch (...) {
        result = BatchResult::failure(Error::ExternalApiError, "metadata response interrupted: " + key);
    }
    {
        std::lock_guard lock(mutex_);
        if (result.isSuccess() && cacheable) {
            responses_.at(key).bytes = bytes;
            cached_bytes_ += bytes;
        }
        promise->set_value(result);
        if (!result.isSuccess() || !cacheable)
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

} // namespace orchestrator::modules::crawler::application::use_cases
