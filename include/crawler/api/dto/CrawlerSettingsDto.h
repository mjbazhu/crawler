#pragma once

#include <cstddef>
#include <string>

namespace orchestrator::modules::crawler::api::dto {

// 爬虫设置只读快照，用于保持 settings API 的启动期字段兼容输出。
struct CrawlerSettingsReadonlySnapshot {
    int http_worker_threads = 0;         // 入站 HTTP worker 数
    int async_worker_threads = 0;        // HTTP handler 调度线程数
    int resource_worker_threads = 0;     // 资源下载 worker 数
    int parse_worker_threads = 0;        // 解析线程数
    int persistence_worker_threads = 0;  // 调试持久化线程数
    int outbound_http_io_threads = 0;    // 出站 HTTP I/O 线程数
    int mysql_io_threads = 0;            // MySQL I/O 线程数
    std::size_t mysql_max_pool_size = 0; // MySQL 最大连接数
    std::string persistence_backend;     // 当前持久化后端
};

} // namespace orchestrator::modules::crawler::api::dto
