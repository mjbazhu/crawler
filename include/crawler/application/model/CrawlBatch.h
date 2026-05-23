#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace orchestrator::modules::crawler::application::model {

// 批次分页查询参数。
struct CrawlBatchQuery {
    int page = 1;                  // 页码，从 1 开始
    int page_size = 20;            // 每页数量
    std::string state;             // 批次状态过滤，空表示不过滤
    std::string task_type;         // 任务类型过滤，空表示不过滤
    std::string keyword;           // batch_id、任务类型、目标和错误信息模糊搜索
    std::string display_name;      // 批次展示名过滤，空表示不过滤
    std::string target_summary;    // 目标摘要过滤，空表示不过滤
    std::string target_keywords;   // 目标关键词过滤，空表示不过滤
    std::string last_error;        // 最近错误模糊过滤，空表示不过滤
    std::string error_category;    // item 失败分类过滤，空表示不过滤
    std::string result_code;       // item 结果编码过滤，空表示不过滤
    bool include_archived = false; // 是否包含已归档批次
};

// 批次目标分页查询参数。
struct CrawlBatchItemQuery {
    int page = 1;               // 页码，从 1 开始
    int page_size = 50;         // 每页数量
    std::string state;          // 目标状态过滤，空表示不过滤
    std::string error_category; // 失败分类过滤，空表示不过滤
    std::string result_code;    // 结果编码过滤，空表示不过滤
};

// 批次事件分页查询参数。
struct CrawlBatchEventQuery {
    int page = 1;        // 页码，从 1 开始
    int page_size = 100; // 每页数量
};

// 采集批次主记录。
struct CrawlBatchRecord {
    std::string batch_id;          // 批次业务 ID
    std::string source = "migu";   // 平台来源
    std::string task_type;         // 任务类型
    std::string state = "Pending"; // 批次状态
    std::string request_params;    // JSON 请求参数快照
    std::string settings_snapshot; // JSON 设置快照
    int limit_count = 0;           // 每目标或补池 limit
    int target_count = 0;          // 目标数量
    int processed_count = 0;       // 已处理数量
    int success_count = 0;         // 成功数量
    int failure_count = 0;         // 失败数量
    bool cancel_requested = false; // 是否请求取消
    std::string last_error;        // 最近错误
    std::string display_name;      // 批次展示名，优先取首个目标名称
    std::string target_summary;    // 目标摘要，用于列表页展示命中的歌手、专辑或资源
    std::string target_keywords;   // 目标关键词聚合，用于前端回显搜索上下文
    std::string error_category;    // item 失败分类聚合，多个值用逗号分隔
    std::string result_code;       // item 结果编码聚合，多个值用逗号分隔
    std::uint64_t created_at = 0;  // 创建时间
    std::uint64_t started_at = 0;  // 开始时间
    std::uint64_t finished_at = 0; // 结束时间
    std::uint64_t updated_at = 0;  // 更新时间
    std::uint64_t archived_at = 0; // 归档时间，0 表示未归档
    std::string archived_reason;   // 归档原因
    std::string parent_batch_id;   // 二次治理来源批次 ID
};

// 采集批次目标记录。
struct CrawlBatchItemRecord {
    std::string batch_id;          // 批次业务 ID
    int item_index = 0;            // 批次内目标序号
    std::string item_key;          // 目标业务键
    std::string item_type;         // 目标类型
    std::string item_label;        // 展示名称
    std::string source = "migu";   // 平台来源
    std::string source_singer_id;  // 平台歌手 ID
    std::string source_album_id;   // 平台专辑 ID
    std::string source_song_id;    // 平台歌曲 ID
    std::string state = "Pending"; // 目标状态
    int retry_count = 0;           // 重试次数
    int written_count = 0;         // 写入数量
    std::string error_category;    // 失败分类：network、parse、persistence、copyright、resource、invalid_target、timeout、unknown
    std::string result_code;       // 结果编码：ok、network_failed、parse_failed、persist_failed、partial_completed 等
    std::uint64_t started_at = 0;  // 开始时间
    std::uint64_t finished_at = 0; // 结束时间
    std::string last_error;        // 最近错误
    std::string payload;           // JSON 扩展上下文
    std::uint64_t created_at = 0;  // 创建时间
    std::uint64_t updated_at = 0;  // 更新时间
};

// 采集批次事件记录。
struct CrawlBatchEventRecord {
    std::string batch_id;          // 批次业务 ID
    std::optional<int> item_index; // 目标序号，批次级事件为空
    std::string event_type;        // 事件类型
    std::string message;           // 事件描述
    std::string payload;           // JSON 扩展上下文
    std::uint64_t created_at = 0;  // 事件时间
};

// 批次分页结果。
struct CrawlBatchPage {
    std::vector<CrawlBatchRecord> items; // 当前页批次
    int page = 1;                        // 当前页码
    int page_size = 20;                  // 每页数量
    std::uint64_t total = 0;             // 总数
};

// 批次目标分页结果。
struct CrawlBatchItemPage {
    std::vector<CrawlBatchItemRecord> items; // 当前页目标
    int page = 1;                            // 当前页码
    int page_size = 50;                      // 每页数量
    std::uint64_t total = 0;                 // 总数
};

// 批次事件分页结果。
struct CrawlBatchEventPage {
    std::vector<CrawlBatchEventRecord> items; // 当前页事件
    int page = 1;                             // 当前页码
    int page_size = 100;                      // 每页数量
    std::uint64_t total = 0;                  // 总数
};

// 批次全局摘要。
struct CrawlBatchSummary {
    std::vector<std::pair<std::string, std::uint64_t>> state_distribution; // 批次状态分布
    std::vector<std::pair<std::string, std::uint64_t>> failure_task_types; // 失败最多的任务类型
    std::optional<CrawlBatchRecord> latest_success;                        // 最近成功批次
    std::optional<CrawlBatchRecord> latest_failed;                         // 最近失败批次
    std::uint64_t running_count = 0;                                       // 当前运行中批次数
    std::uint64_t oldest_stale_running_at = 0;                             // 最老超时运行批次更新时间
    std::uint64_t generated_at = 0;                                        // 生成时间
};

// 批次二次治理请求。
struct CrawlBatchGovernanceRequest {
    std::string mode = "failed";     // clone 模式：all 或 failed
    std::vector<std::string> states; // item 状态过滤
    std::string error_category;      // item 失败分类过滤
    std::string result_code;         // item 结果编码过滤
    int limit = 50;                  // 最大目标数量
    int limit_per_target = 50;       // 新批次单目标处理上限
    std::string reason;              // 归档或治理原因
};

// 批次二次治理明细。
struct CrawlBatchGovernanceItem {
    std::string source_id; // 平台目标 ID
    std::string name;      // 展示名称
    std::string status;    // 旧 item 状态
    std::string reason;    // 操作结果
};

// 批次二次治理响应。
struct CrawlBatchGovernanceResult {
    std::string batch_id;                        // 新批次 ID，归档时为空
    int claimed_count = 0;                       // 创建新批次目标数量
    int skipped_count = 0;                       // 跳过数量
    std::vector<CrawlBatchGovernanceItem> items; // 明细
};

} // namespace orchestrator::modules::crawler::application::model
