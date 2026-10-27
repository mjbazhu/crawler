#pragma once

#include "crawler/application/model/AlbumSourceMetadata.h"
#include "crawler/application/model/SingerSourceSync.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace orchestrator::modules::crawler::application::model {

// 分页查询参数，页码从 1 开始。
struct CatalogPageQuery {
    int page = 1;       // 页码
    int page_size = 20; // 每页数量
};

// Migu 歌手目录查询参数。
struct MiguSingerCatalogQuery : CatalogPageQuery {
    std::string source = "migu";           // 音乐平台标识
    std::string keyword;                   // 歌手名或平台歌手 ID 关键字
    std::string source_singer_id;          // 精确平台歌手 ID，空表示不限
    std::string country;                   // 平台地区过滤，空表示不限
    std::optional<int> gender;             // 来源歌手类型过滤：0 未知、1 男、2 女、3 组合
    std::vector<int> songs_sync_statuses;  // 歌曲列表同步状态过滤
    std::vector<int> albums_sync_statuses; // 专辑列表同步状态过滤
    std::optional<int> min_songs_count;    // 最小已关联歌曲数，空表示不限
    std::optional<int> max_songs_count;    // 最大已关联歌曲数，空表示不限
    std::optional<int> min_albums_count;   // 最小已关联专辑数，空表示不限
    std::optional<int> max_albums_count;   // 最大已关联专辑数，空表示不限
};

// Migu 专辑目录查询参数。
struct MiguAlbumCatalogQuery : CatalogPageQuery {
    std::string source = "migu";           // 音乐平台标识
    std::string keyword;                   // 专辑名或平台专辑 ID 关键字
    std::string artist;                    // 歌手名或平台歌手 ID 过滤
    std::optional<int> album_kind;         // 专辑类型过滤，空表示不限
    std::vector<int> detail_sync_statuses; // 详情同步状态过滤
    std::vector<int> songs_sync_statuses;  // 专辑歌曲同步状态过滤
    std::optional<int> min_songs_count;    // 最小已关联专辑歌曲数，空表示不限
    std::optional<int> max_songs_count;    // 最大已关联专辑歌曲数，空表示不限
};

// Migu 歌曲目录查询参数。
struct MiguSongCatalogQuery : CatalogPageQuery {
    std::string source = "migu";        // 音乐平台标识
    std::string keyword;                // 歌曲名、平台歌曲 ID 或内容 ID 关键字
    std::string artist;                 // 歌手名或平台歌手 ID 过滤
    std::string album;                  // 专辑名或平台专辑 ID 过滤
    std::string source_singer_id;       // 关联平台歌手 ID
    std::string source_album_id;        // 关联平台专辑 ID
    std::vector<int> resource_statuses; // 资源状态过滤
    std::string format_type;            // 音质档位过滤
};

// Migu 资源队列查询参数。
struct MiguResourceQueueQuery : CatalogPageQuery {
    std::string source = "migu"; // 音乐平台标识
    std::string keyword;         // 歌曲名、平台歌曲 ID、内容 ID 关键字
    std::vector<int> statuses;   // 资源状态过滤
    std::string resource_kind;   // 资源类型，例如 audio、cover
    std::string format_type;     // 音质档位过滤
};

// Migu 资源队列治理和处理预览参数；retry/reset/recover-stale 只调整状态，process 只用于预览。
struct MiguResourceActionQuery {
    std::string source = "migu";             // 音乐平台标识
    std::string action;                      // 动作：retry、reset、recover-stale、process
    std::vector<std::uint64_t> resource_ids; // 显式资源任务 ID 列表
    std::vector<int> statuses;               // 资源状态过滤
    std::string keyword;                     // 歌曲名、平台歌曲 ID、内容 ID 关键字
    std::string resource_kind;               // 资源类型，例如 audio、cover
    std::string format_type;                 // 音质档位过滤
    int limit = 50;                          // 最大处理数量
    int stale_after_seconds = 3600;          // 同步中超时阈值
};

// 补池目标查询和治理参数。
struct MiguBackfillTargetQuery : CatalogPageQuery {
    std::string source = "migu";         // 音乐平台标识
    std::vector<std::string> source_ids; // 显式平台 ID 列表，歌手补池为 singer_id，专辑补池为 album_id
    std::vector<int> statuses;           // 同步状态过滤，空时由用例层使用 0/3/4
    std::string keyword;                 // 歌手或专辑关键字
    std::string country;                 // 歌手地区过滤
    std::optional<int> gender;           // 歌手性别过滤
    std::string artist;                  // 专辑歌手过滤
    std::optional<int> album_kind;       // 专辑类型过滤
    int limit = 50;                      // retry/reset 最大处理数量
    bool force = false;                  // retry 包含已完成目标，仍跳过正在执行的目标
};

// 补池操作目标明细，用于前端展示认领、跳过或重置原因。
struct MiguBackfillActionItem {
    std::string source_id; // 平台目标 ID
    std::string name;      // 前端展示名称
    int status = 0;        // 操作后的状态或原状态
    std::string reason;    // 操作结果原因
};

// 补池 retry 启动结果，供 API 返回 batch_id 兼容字段和认领统计。
struct MiguBackfillRetryResult {
    std::string batch_id;                      // 创建的批次 ID
    int claimed_count = 0;                     // 本次成功认领数量
    int skipped_count = 0;                     // 本次跳过数量
    std::vector<MiguBackfillActionItem> items; // 本次可明确返回的目标结果
};

// Migu 歌手目录记录。
struct MiguSingerCatalogRecord {
    std::uint64_t singer_db_id = 0;        // singers.id
    std::string name;                      // 公共歌手名
    std::string normalized_name;           // 标准化歌手名
    int gender = 0;                        // 来源歌手类型，不从公共名称池推断
    int status = 0;                        // 公共歌手状态
    std::uint64_t source_ref_id = 0;       // singer_source_refs.id
    std::string source = "migu";           // 音乐平台标识
    std::string source_item_key;           // 平台来源唯一键
    std::string source_singer_id;          // 平台歌手 ID
    std::string source_name_pinyin;        // 平台歌手拼音或排序名
    std::string source_country;            // 平台地区
    std::string source_tag;                // 平台标签
    std::optional<int> source_song_total;  // 平台声明歌曲总数，空表示未知
    std::optional<int> source_album_total; // 平台声明专辑总数，空表示未知
    std::string avatar_url;                // 平台头像
    int songs_sync_status = 0;             // 歌手歌曲同步状态
    std::uint64_t songs_synced_at = 0;     // 歌曲同步时间
    int songs_sync_count = 0;              // 歌曲同步数量
    std::string songs_sync_error;          // 歌曲同步错误
    std::string songs_last_batch_id;       // 歌曲同步最近批次 ID
    std::string songs_running_batch_id;    // 歌曲同步运行中批次 ID
    int albums_sync_status = 0;            // 歌手专辑同步状态
    std::uint64_t albums_synced_at = 0;    // 专辑同步时间
    int albums_sync_count = 0;             // 专辑同步数量
    std::string albums_sync_error;         // 专辑同步错误
    std::string albums_last_batch_id;      // 专辑同步最近批次 ID
    std::string albums_running_batch_id;   // 专辑同步运行中批次 ID
    std::uint64_t created_at = 0;          // 创建时间
    std::uint64_t updated_at = 0;          // 更新时间
};

// Migu 专辑目录记录。
struct MiguAlbumCatalogRecord {
    std::uint64_t album_db_id = 0;       // albums.id
    std::string title;                   // 公共专辑标题
    std::string normalized_title;        // 标准化专辑标题
    std::string primary_artist_name;     // 公共主艺人名
    std::string release_date;            // 公共发行日期
    std::uint64_t source_ref_id = 0;     // album_source_refs.id
    std::string source = "migu";         // 音乐平台标识
    std::string source_item_key;         // 平台来源唯一键
    std::string source_album_id;         // 平台专辑 ID
    std::string source_resource_type;    // 详情资源类型
    std::string source_res_type;         // 列表资源类型
    int source_album_kind = 0;           // 平台专辑类型
    std::string source_title;            // 平台专辑标题
    std::string source_title_pinyin;     // 来源专辑拼音，缺失时为空
    std::string summary;                 // 来源专辑简介，缺失时为空
    std::string source_artist_id;        // 平台歌手 ID
    std::string source_artist_name;      // 平台歌手名
    std::string cover_url;               // 平台封面
    std::string total_count;             // 曲目总数原始值
    std::string publish_date;            // 平台发布日期
    std::uint64_t play_num = 0;          // 平台播放数
    std::string play_num_desc;           // 平台播放数展示
    int detail_sync_status = 0;          // 详情同步状态
    std::uint64_t detail_synced_at = 0;  // 详情同步时间
    std::string detail_sync_error;       // 详情同步错误
    std::string detail_last_batch_id;    // 详情同步最近批次 ID
    std::string detail_running_batch_id; // 详情同步运行中批次 ID
    int songs_sync_status = 0;           // 专辑歌曲同步状态
    std::uint64_t songs_synced_at = 0;   // 专辑歌曲同步时间
    int songs_sync_count = 0;            // 最近一次专辑歌曲采集成功处理条数
    int album_songs_count = 0;           // 当前已保存的去重专辑歌曲关系数
    std::string songs_sync_error;        // 专辑歌曲同步错误
    std::string songs_last_batch_id;     // 专辑歌曲同步最近批次 ID
    std::string songs_running_batch_id;  // 专辑歌曲同步运行中批次 ID
    std::uint64_t created_at = 0;        // 创建时间
    std::uint64_t updated_at = 0;        // 更新时间
};

// Migu 歌曲目录记录。
struct MiguSongCatalogRecord {
    std::uint64_t song_db_id = 0;              // songs.id
    std::string title;                         // 公共歌曲标题
    std::string normalized_title;              // 标准化歌曲标题
    std::uint32_t source_duration_seconds = 0; // 平台返回的歌曲时长
    std::uint64_t source_ref_id = 0;           // song_source_refs.id
    std::string source = "migu";               // 音乐平台标识
    std::string source_item_key;               // 平台来源唯一键
    std::string source_song_id;                // 平台歌曲 ID
    std::string source_content_id;             // 平台内容 ID
    std::string source_resource_type;          // 平台资源类型
    std::string source_title_pinyin;           // 平台歌曲拼音或排序名
    std::string source_album_id;               // 平台专辑 ID
    std::string source_album_name;             // 平台专辑名
    std::string primary_artist_id;             // 从 song_artist_refs 派生的主展示歌手 ID
    std::string primary_artist_name;           // 从 song_artist_refs 派生的主展示歌手名
    std::string cover_url;                     // 封面地址
    std::string lyric_url;                     // 歌词地址
    std::string mrc_url;                       // 逐字歌词地址
    std::string trc_url;                       // 翻译歌词或扩展歌词地址
    std::string copyright_id;                  // 版权 ID
    int metadata_status = 0;                   // 元数据状态
    std::string format_types;                  // 已知音质档位，逗号分隔
    int resource_count = 0;                    // 资源任务数量
    int failed_resource_count = 0;             // 失败资源数量
    std::uint64_t created_at = 0;              // 创建时间
    std::uint64_t updated_at = 0;              // 更新时间
};

// Migu 资源队列记录。
struct MiguResourceQueueRecord {
    std::uint64_t resource_id = 0;        // song_resources.id
    std::uint64_t song_source_ref_id = 0; // song_source_refs.id
    std::string source = "migu";          // 音乐平台标识
    std::string source_song_id;           // 平台歌曲 ID
    std::string source_content_id;        // 平台内容 ID
    std::string title;                    // 歌曲标题
    std::string artist;                   // 主歌手名
    std::string album;                    // 专辑名
    std::string resource_kind;            // 资源类型
    std::string format_type;              // 音质档位
    std::string remote_url;               // 远程 URL
    std::string local_path;               // 本地路径
    std::string cloud_backend;            // 云端上传后端
    std::string cloud_path;               // 云端保存路径
    std::uint64_t size_bytes = 0;         // 文件大小
    std::string checksum;                 // 校验值
    int status = 0;                       // 资源状态
    int retry_count = 0;                  // 重试次数
    std::string last_error;               // 最近错误
    std::string error_category;           // 失败分类
    std::string result_code;              // 结果编码
    std::string last_batch_id;            // 最近处理批次 ID
    std::string running_batch_id;         // 正在处理批次 ID
    std::uint64_t last_processed_at = 0;  // 最近治理或处理时间
    std::uint64_t fetched_at = 0;         // 获取时间
    std::uint64_t cloud_uploaded_at = 0;  // 云端上传时间
    std::uint64_t created_at = 0;         // 创建时间
    std::uint64_t updated_at = 0;         // 更新时间
};

// Migu 资源队列治理明细，用于前端展示每个资源任务的操作结果。
struct MiguResourceActionItem {
    std::uint64_t resource_id = 0; // song_resources.id
    std::string source_id;         // 平台歌曲 ID 或内容 ID
    std::string name;              // 歌曲名或资源展示名
    int status = 0;                // 操作前或操作后状态
    std::string reason;            // 操作原因，例如 retry、reset、not_found
};

// Migu 资源治理预览明细。
struct MiguResourcePreviewItem {
    std::uint64_t resource_id = 0; // song_resources.id
    std::string target_id;         // 平台歌曲 ID 或内容 ID
    std::string target_name;       // 歌曲名
    std::string resource_kind;     // 资源类型
    std::string format_type;       // 音质档位
    int sync_status = 0;           // 当前资源状态
    bool eligible = false;         // 是否可被当前动作治理
    std::string reason;            // 可治理或不可治理原因
};

// Migu 资源治理预览结果。
struct MiguResourcePreviewResult {
    int matched_count = 0;                      // 筛选命中数量
    int eligible_count = 0;                     // 可治理数量
    int ineligible_count = 0;                   // 不可治理数量
    int missing_count = 0;                      // 显式 ID 不存在数量
    std::vector<MiguResourcePreviewItem> items; // 样例明细
};

// Migu 资源队列治理结果。
struct MiguResourceActionResult {
    int updated_count = 0;                     // 实际更新数量
    int success_count = 0;                     // 成功数量
    int failed_count = 0;                      // 失败数量
    int skipped_count = 0;                     // 跳过数量
    std::vector<MiguResourceActionItem> items; // 样例或显式 ID 明细
};

// 治理审计事件查询参数。
struct MiguGovernanceEventQuery : CatalogPageQuery {
    std::string source = "migu"; // 音乐平台标识
    std::string target_type;     // 目标类型过滤
    std::string target_id;       // 目标 ID 过滤
    std::string action;          // 治理动作过滤
    std::string batch_id;        // 批次 ID 过滤
};

// 治理审计事件记录。
struct MiguGovernanceEventRecord {
    std::uint64_t event_id = 0;    // governance_events.id
    std::string source = "migu";   // 音乐平台标识
    std::string action;            // 治理动作
    std::string target_type;       // 目标类型
    std::string target_id;         // 目标 ID
    std::string target_name;       // 目标主展示名
    std::string target_summary;    // 目标次级说明
    std::string target_source_id;  // 平台对象 ID
    std::string target_owner_name; // 目标归属展示名
    std::string batch_id;          // 关联批次 ID
    std::string before_status;     // 治理前状态
    std::string after_status;      // 治理后状态
    std::string result_code;       // 结果编码
    std::string error_category;    // 失败分类
    std::string reason;            // 原因
    std::string payload;           // JSON 脱敏上下文
    std::uint64_t created_at = 0;  // 事件时间
};

// Migu 元数据一致性检查结果。
struct MiguIntegritySummary {
    std::uint64_t singers_without_songs = 0;        // 已有歌手但歌曲未完成数量
    std::uint64_t singers_without_albums = 0;       // 已有歌手但专辑未完成数量
    std::uint64_t albums_without_details = 0;       // 已有专辑但详情未完成数量
    std::uint64_t albums_without_songs = 0;         // 已有专辑详情但歌曲未完成数量
    std::uint64_t songs_without_resource_tasks = 0; // 已有歌曲来源但没有资源任务数量
    std::uint64_t resources_failed = 0;             // 失败资源任务数量
    std::uint64_t resources_running_stale = 0;      // 长时间获取中的资源任务数量
    std::uint64_t singers_songs_running_stale = 0;  // 歌手歌曲同步中超时数量
    std::uint64_t singers_albums_running_stale = 0; // 歌手专辑同步中超时数量
    std::uint64_t albums_details_running_stale = 0; // 专辑详情同步中超时数量
    std::uint64_t albums_songs_running_stale = 0;   // 专辑歌曲同步中超时数量
    std::uint64_t running_batches_stale = 0;        // 运行中超时批次数量
    std::uint64_t generated_at = 0;                 // 生成时间
};

// Migu stale running 状态恢复请求。
struct MiguStaleRecoveryRequest {
    std::string source = "migu";    // 音乐平台标识
    int stale_after_seconds = 3600; // 判定同步中超时的秒数
    std::vector<std::string> types; // 恢复类型，空表示所有元数据链路、资源和批次
};

// Migu stale running 状态恢复结果。
struct MiguStaleRecoveryResult {
    int singer_songs_recovered = 0;  // 歌手歌曲状态恢复数量
    int singer_albums_recovered = 0; // 歌手专辑状态恢复数量
    int album_details_recovered = 0; // 专辑详情状态恢复数量
    int album_songs_recovered = 0;   // 专辑歌曲状态恢复数量
    int resources_recovered = 0;     // 资源任务状态恢复数量
    int batches_recovered = 0;       // 批次恢复数量
    std::uint64_t recovered_at = 0;  // 恢复执行时间
};

// MySQL schema 检查结果，供前端判断本地库是否缺表或缺关键列。
struct MiguSchemaStatus {
    std::uint64_t schema_version = 0;             // 当前 crawler_mysql schema 版本号
    std::uint64_t latest_applied_version = 0;     // schema_migrations 中最新已执行版本
    std::uint64_t latest_available_version = 0;   // 本服务要求的最新 migration 版本
    std::string schema_name;                      // 当前 schema 名称或说明
    bool ok = false;                              // 是否满足当前服务要求
    std::vector<std::string> pending_migrations;  // 待执行 migration 版本
    std::vector<std::string> checksum_mismatches; // checksum 不一致的 migration 版本
    std::vector<std::string> missing_tables;      // 缺失的核心表
    std::vector<std::string> missing_columns;     // 缺失的关键列
    std::uint64_t checked_at = 0;                 // 检查时间，Unix 秒时间戳
};

// 通用分页结果。
template <typename T>
struct CatalogPage {
    std::vector<T> items;    // 当前页记录
    int page = 1;            // 当前页码
    int page_size = 20;      // 每页数量
    std::uint64_t total = 0; // 总数
};

using MiguSingerCatalogPage = CatalogPage<MiguSingerCatalogRecord>;
using MiguAlbumCatalogPage = CatalogPage<MiguAlbumCatalogRecord>;
using MiguSongCatalogPage = CatalogPage<MiguSongCatalogRecord>;
using MiguResourceQueuePage = CatalogPage<MiguResourceQueueRecord>;
using MiguGovernanceEventPage = CatalogPage<MiguGovernanceEventRecord>;

// 补池数据库认领结果，retry 只会为成功认领的目标创建批次 item。
struct MiguBackfillClaimResult {
    std::vector<MiguSingerCatalogRecord> singers; // 已认领的歌手目标
    std::vector<MiguAlbumCatalogRecord> albums;   // 已认领的专辑目标
    int requested_count = 0;                      // 显式 ID 请求数量；筛选认领时为候选数量
    int claimed_count = 0;                        // 成功置为同步中的数量
    int skipped_count = 0;                        // 不存在、状态不允许或竞争失败的数量
};

} // namespace orchestrator::modules::crawler::application::model
