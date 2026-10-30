#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace orchestrator::modules::crawler::application::model {

// 歌曲资源处理运行选项，支持全局默认和单次 resources/process 覆盖。
struct SongResourceProcessOptions {
    std::size_t resource_download_concurrency = 3; // 批次内资源下载并发数
    int resource_download_retries = 1;             // 可恢复下载错误重试次数
    int resource_download_timeout_ms = 120000;     // 单次资源内容下载超时
    int pacmtoken_refresh_interval_seconds = 300;  // 运行中 pacmtoken 刷新间隔；0 表示关闭
    bool auto_refresh_pacmtoken = true;            // 批次开始和运行中是否自动刷新 pacmtoken
    bool refresh_pacmtoken_on_440013 = true;       // 遇到 440013 后是否刷新 token 并重试一次
    bool process_related_resources = true;         // audio 成功后是否继续处理响应带出的 cover/lrc/mrc/trc
};

// 歌曲资源处理运行选项覆盖项，只表达调用方显式传入的字段。
struct SongResourceProcessOptionOverrides {
    std::optional<int> resource_download_concurrency;      // 覆盖资源下载并发数
    std::optional<int> resource_download_retries;          // 覆盖可恢复下载错误重试次数
    std::optional<int> resource_download_timeout_ms;       // 覆盖单次资源内容下载超时
    std::optional<int> pacmtoken_refresh_interval_seconds; // 覆盖 pacmtoken 刷新间隔
    std::optional<bool> auto_refresh_pacmtoken;            // 覆盖自动刷新开关
    std::optional<bool> refresh_pacmtoken_on_440013;       // 覆盖 440013 刷新重试开关
    std::optional<bool> process_related_resources;         // 覆盖关联资源处理开关
};

// 歌曲资源队列认领查询。
struct SongResourceClaimQuery {
    std::string source = "migu";                         // 音乐平台标识
    std::string resource_kind;                           // 资源类型过滤，空字符串表示不过滤
    std::string source_content_id;                       // 指定平台 contentId，空字符串表示不过滤
    std::string format_type;                             // 指定音质档位，空字符串表示不过滤
    std::vector<std::uint64_t> resource_ids;             // 显式资源任务 ID；非空时优先按 ID 认领
    std::vector<int> statuses {0, 3};                    // 可认领状态，默认待获取和失败；4 跳过不再认领
    std::size_t limit = 20;                              // 最大认领数量
    std::size_t offset = 0;                              // 候选偏移
    std::string batch_id;                                // 当前处理批次 ID
    SongResourceProcessOptions options;                  // 本次资源处理的有效运行选项
    SongResourceProcessOptionOverrides option_overrides; // 前端请求传入的选项覆盖项
};

// 歌曲资源队列目标，包含 provider、下载和路径规划所需参数。
struct SongResourceQueueTarget {
    std::uint64_t resource_id = 0;          // song_resources.id
    std::uint64_t song_source_ref_id = 0;   // song_source_refs.id
    std::string source = "migu";            // 音乐平台标识
    std::string source_song_id;             // 平台歌曲 ID
    std::string source_content_id;          // 平台 contentId
    std::string source_resource_type;       // 平台来源资源类型
    std::string audio_resource_type;        // 音质行资源类型，缺失时使用 source_resource_type
    std::string resource_fetch_format_type; // 非音频资源补链时使用的音质档位
    std::string copyright_id;               // 平台版权 ID
    std::string title;                      // 歌曲标题，仅用于诊断
    std::string artist;                     // 主歌手，仅用于诊断
    std::string album;                      // 专辑名，仅用于诊断
    int source_album_kind = 0;              // 平台专辑类型：0 未知，1 普通专辑，2 数字专辑
    std::string resource_kind;              // 资源类型
    std::string format_type;                // 音质档位
    std::string remote_url;                 // 当前远程 URL
    std::string local_path;                 // 当前本地路径
    int status = 0;                         // 当前状态
    int retry_count = 0;                    // 重试次数
    std::uint64_t claim_generation = 0;     // 认领代次，拒绝过期 worker 的回写
};

// 关联资源认领请求，用于 audio 响应带出的 cover/lrc/mrc/trc URL。
struct SongResourceRelatedClaimRequest {
    std::uint64_t song_source_ref_id = 0; // 同一首歌的平台来源 ID
    std::string resource_kind;            // cover、lyric、mrc 或 trc
    std::string remote_url;               // 本次解析出的远程 URL
    std::string batch_id;                 // 当前处理批次 ID
};

// 歌曲资源成功回写请求。
struct SongResourceSuccessUpdate {
    std::uint64_t resource_id = 0;      // song_resources.id
    std::string local_path;             // 保存后的相对路径
    std::string cloud_backend;          // 云端上传后端；未启用云端上传时为空
    std::string cloud_path;             // 云端保存路径；未启用云端上传时为空
    std::uint64_t size_bytes = 0;       // 保存文件大小
    std::string checksum;               // 存储校验值
    std::string result_code = "ok";     // 结果编码
    std::string batch_id;               // 当前处理批次 ID
    std::uint64_t claim_generation = 0; // 认领代次，拒绝过期 worker 的回写
    bool remote_only = false;           // 旧copyurl仅有远端对象，不声明本轮filesystem确认
};

// 歌曲资源失败回写请求。
struct SongResourceFailureUpdate {
    std::uint64_t resource_id = 0;          // song_resources.id
    std::string error_category = "unknown"; // 失败分类
    std::string result_code = "failed";     // 结果编码
    std::string message;                    // 脱敏错误说明
    std::string batch_id;                   // 当前处理批次 ID
    std::uint64_t claim_generation = 0;     // 认领代次，拒绝过期 worker 的回写
};

// 歌曲资源跳过回写请求，用于版权、数字专辑等无需继续重试的永久限制。
struct SongResourceSkipUpdate {
    std::uint64_t resource_id = 0;           // 当前 song_resources.id，兜底单条跳过
    std::uint64_t song_source_ref_id = 0;    // 同一首歌的平台来源 ID；非 0 时可批量跳过
    std::string resource_kind;               // 跳过资源类型；空字符串表示该歌曲全部资源
    std::string format_type;                 // 指定音质档位；空字符串表示匹配类型全部跳过
    std::string error_category = "resource"; // 跳过分类
    std::string result_code = "skipped";     // 结果编码
    std::string message;                     // 脱敏跳过原因
    std::string batch_id;                    // 当前处理批次 ID
    std::uint64_t claim_generation = 0;      // 认领代次，拒绝过期 worker 的回写
};

// 单条资源处理结果。
struct SongResourceProcessItem {
    std::uint64_t resource_id = 0;        // song_resources.id；关联资源无可认领任务时为 0
    std::uint64_t song_source_ref_id = 0; // song_source_refs.id，用于批次追踪上下文
    std::string source = "migu";          // 音乐平台标识
    std::string source_song_id;           // 平台歌曲 ID
    std::string source_content_id;        // 平台 contentId
    std::string title;                    // 展示标题
    std::string resource_kind;            // 资源类型
    std::string format_type;              // 音质档位
    bool ok = false;                      // 是否处理成功
    bool skipped = false;                 // 是否因永久限制、无目标或已被处理而跳过
    bool related = false;                 // 是否为 audio 响应带出的关联资源
    bool synthetic = false;               // 是否为无真实资源行的合成明细
    std::string local_path;               // 成功保存后的相对或对象路径
    std::string cloud_backend;            // 云端上传后端；未启用云端上传时为空
    std::string cloud_path;               // 云端保存路径；未启用云端上传时为空
    std::uint64_t size_bytes = 0;         // 成功保存后的字节数
    std::string checksum;                 // 成功保存后的校验值
    std::string error_category;           // 失败或跳过分类，成功时为空
    std::string result_code;              // 结果编码
    std::string message;                  // 诊断说明
    std::uint64_t claim_generation = 0;   // 内部回写围栏，不改变旧响应字段
};

// 批量资源处理结果。
struct SongResourceProcessResult {
    std::string batch_id;                       // 本次资源处理批次 ID，用于回写和诊断追踪
    int claimed_count = 0;                      // 主资源认领数量
    int succeeded_count = 0;                    // 主资源成功数量
    int failed_count = 0;                       // 主资源失败数量
    int skipped_count = 0;                      // 主资源跳过数量
    int related_succeeded_count = 0;            // 关联资源成功数量
    int related_failed_count = 0;               // 关联资源失败数量
    int related_skipped_count = 0;              // 关联资源跳过数量
    int synthetic_related_skipped_count = 0;    // 无资源任务可认领的合成关联跳过数量
    std::vector<SongResourceProcessItem> items; // 处理明细
};

} // namespace orchestrator::modules::crawler::application::model
