#pragma once

#include <optional>

namespace orchestrator::modules::crawler::application::model {

// 爬虫运行设置快照，只包含 application 可在运行期调整的策略字段。
struct CrawlerRuntimeSettings {
    int max_batch_concurrency = 4;                // 批量任务最大并发 worker 数，可运行期调整
    int max_page_concurrency = 4;                 // 单个关键词分页请求最大并发数，可运行期调整
    int max_retries = 1;                          // 单个目标失败后的重试次数，可运行期调整
    int task_timeout_ms = 600000;                 // 批量任务超时时间，可运行期调整
    int resource_download_concurrency = 3;        // 资源下载批次内并发数
    int resource_download_retries = 1;            // 资源可恢复下载错误重试次数
    int resource_download_timeout_ms = 120000;    // 单次资源内容下载超时
    int pacmtoken_refresh_interval_seconds = 300; // 资源长跑 pacmtoken 刷新间隔
    bool auto_refresh_pacmtoken = true;           // 资源任务是否自动刷新 pacmtoken
    bool refresh_pacmtoken_on_440013 = true;      // 资源任务遇到 440013 是否刷新 token 后重试
};

// 爬虫运行设置更新请求；只包含支持热更新的字段。
struct CrawlerRuntimeSettingsUpdate {
    std::optional<int> max_batch_concurrency;              // 批量任务最大并发 worker 数
    std::optional<int> max_page_concurrency;               // 单个关键词分页请求最大并发数
    std::optional<int> max_retries;                        // 单个目标失败后的重试次数
    std::optional<int> task_timeout_ms;                    // 批量任务超时时间
    std::optional<int> resource_download_concurrency;      // 资源下载批次内并发数
    std::optional<int> resource_download_retries;          // 资源可恢复下载错误重试次数
    std::optional<int> resource_download_timeout_ms;       // 单次资源内容下载超时
    std::optional<int> pacmtoken_refresh_interval_seconds; // 资源长跑 pacmtoken 刷新间隔
    std::optional<bool> auto_refresh_pacmtoken;            // 资源任务是否自动刷新 pacmtoken
    std::optional<bool> refresh_pacmtoken_on_440013;       // 资源任务遇到 440013 是否刷新 token 后重试
};

} // namespace orchestrator::modules::crawler::application::model
