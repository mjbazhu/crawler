#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace orchestrator::modules::crawler::application::model {

// 歌手来源同步队列查询参数，空 country/q/gender 表示不过滤。
struct SingerSourceSyncQuery {
    std::string source = "migu";        // 音乐平台标识
    std::vector<int> sync_statuses;     // 需要认领的同步状态；为空时由用例层使用 0/3/4
    std::string country;                // 平台地区过滤，例如 华语；空表示不限
    std::optional<int> gender;          // 歌手性别过滤；空表示不限
    std::string keyword;                // 歌手名或平台歌手 ID 模糊搜索；空表示不限
    std::size_t limit = 20;             // 最大认领数量
    std::size_t offset = 0;             // 查询偏移量
    bool include_stale_running = false; // 是否包含长时间卡在同步中的目标
    int stale_after_seconds = 3600;     // 同步中超过该秒数可重新认领
};

// 歌手来源同步目标，来自 singer_source_refs。
struct SingerSourceSyncTarget {
    std::uint64_t id = 0;           // singer_source_refs.id
    std::uint64_t singer_db_id = 0; // 公共歌手 ID
    std::string source = "migu";    // 音乐平台标识
    std::string source_item_key;    // 平台来源唯一键
    std::string source_singer_id;   // 平台歌手 ID
    std::string name;               // 公共歌手名
    std::string source_country;     // 平台地区
    int gender = 0;                 // 公共歌手性别
    std::string avatar_url;         // 平台头像
    std::optional<int> source_song_total;  // 平台声明歌曲总数，空表示未知
    std::optional<int> source_album_total; // 平台声明专辑总数，空表示未知
    int sync_status = 0;            // 当前同步状态
    std::uint64_t synced_at = 0;    // 最近同步完成时间
    int sync_count = 0;             // 最近同步数量
    std::string sync_error;         // 最近同步错误
    std::uint64_t updated_at = 0;   // 更新时间
};

// 歌手来源同步预览结果；只读返回候选数量和样例，不修改同步状态。
struct SingerSourceSyncPreviewResult {
    std::uint64_t matched_count = 0;           // 基础筛选命中数量，不限定同步状态
    std::uint64_t eligible_count = 0;          // 可被当前请求认领的数量
    std::vector<SingerSourceSyncTarget> items; // 可认领目标样例，数量受 query.limit 控制
};

} // namespace orchestrator::modules::crawler::application::model
