#pragma once
#include <vector>

#include "crawler/application/use_cases/SingerCatalogWorkflow.h"

#include "crawler/application/use_cases/WorkflowPaging.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <future>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace orchestrator::modules::crawler::application::use_cases {
namespace singer_detail {

// 歌手歌曲分页采集结果。
struct SingerSongPageResult {
    int page = 0;                                                     // 页码
    application::model::MetadataWriteBatch batch;                    // 本页实体及资料
    application::ports::SingerSongPageInfo page_info;                 // 分页摘要
    bool fetched = false;                                             // 是否成功获取响应
};

// 歌手专辑分页采集结果。
struct SingerAlbumPageResult {
    int page = 0;                                                       // 页码
    application::model::MetadataWriteBatch batch;                      // 本页实体及资料
    application::ports::SingerAlbumPageInfo page_info;                  // 分页摘要
    bool fetched = false;                                               // 是否成功获取响应
};

// 将本次按歌手入口采集的歌手标记到歌曲歌手关系里。
inline void markSeedSinger(std::vector<application::model::SongSourceMetadata> &songs, std::uint64_t singer_id)
{
    const std::string seed_source_singer_id = std::to_string(singer_id);
    for (auto &song : songs) {
        bool matched = false;
        bool has_primary = false;
        std::uint32_t max_artist_order = 0;
        for (auto &artist_ref : song.artist_refs) {
            has_primary = has_primary || artist_ref.is_primary;
            max_artist_order = (std::max)(max_artist_order, artist_ref.artist_order);
            if (artist_ref.source_artist_id == seed_source_singer_id) {
                artist_ref.is_seed = true;
                matched = true;
            }
        }

        if (!matched) {
            application::model::SongSourceArtistRef seed_ref;
            seed_ref.source_artist_id = seed_source_singer_id;
            if (song.source_primary_artist_id.empty() || song.source_primary_artist_id == seed_source_singer_id) {
                seed_ref.source_artist_name = song.source_primary_artist_name;
            }
            seed_ref.artist_order = max_artist_order + 1;
            seed_ref.is_primary = !has_primary;
            seed_ref.is_seed = true;
            song.artist_refs.push_back(std::move(seed_ref));
        }
    }
}

// 计算歌手分类本次会写入的唯一平台来源数量，避免接口重复项干扰批次进度。
inline int singerIndexUniqueCount(const std::vector<application::model::SingerSourceMetadata> &singers, int limit)
{
    const int effective_limit = limit < 0 ? kWorkflowDefaultLimit : limit;
    const std::size_t max_count = effective_limit == 0 ? singers.size() : (std::min)(singers.size(), static_cast<std::size_t>(effective_limit));
    std::set<std::string> unique_keys;
    for (std::size_t index = 0; index < max_count; ++index) {
        const auto &singer = singers[index];
        if (!singer.source.empty() && !singer.source_singer_id.empty() && !singer.name.empty()) {
            unique_keys.insert(singer.source + "\n" + singer.source_singer_id);
        }
    }
    return static_cast<int>(unique_keys.size());
}

// 歌手歌曲/专辑采集 limit=0 表示分页保护范围内全部获取，正数仍限制到 500。
inline int normalizeSingerIdsWorkflowLimit(int limit)
{
    if (limit < 0) {
        return kWorkflowDefaultLimit;
    }
    if (limit == 0) {
        return 0;
    }
    return (std::min)(limit, 500);
}

// 判断歌手概览是否包含可保存的声明总数。
inline bool hasSingerSourceProfileCounts(const application::model::SingerSourceProfile &profile)
{
    return profile.source_song_total || profile.source_album_total;
}

// 已知声明总数尚未满足时，非空短页不能证明目录结束；空页及既有页数上限仍终止采集。
inline bool shouldStopSingerPage(bool suggested_stop, std::size_t page_count, std::size_t collected, std::optional<int> declared_total)
{
    if (page_count == 0) return true;
    if (declared_total && *declared_total > 0 && collected < static_cast<std::size_t>(*declared_total)) return false;
    return suggested_stop;
}

// 平台总数已知时，把受 limit 截断的成功结果暴露为部分完成。
inline void applyDeclaredTotalCompletion(application::model::SingerCollectionResult &result, std::optional<int> declared_total, std::string_view item_name)
{
    if (!result.success || !declared_total || *declared_total <= result.written_count) {
        return;
    }

    result.success = false;
    result.partial = result.written_count > 0;
    result.error = std::string(item_name) + " incomplete: fetched " + std::to_string(result.written_count) + " of " + std::to_string(*declared_total);
}

}
} // namespace orchestrator::modules::crawler::application::use_cases::singer_detail
