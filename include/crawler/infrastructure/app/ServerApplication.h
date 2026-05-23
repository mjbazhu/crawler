#pragma once

#include "crawler/infrastructure/music/persistence/mysql/MySqlConfig.h"

#include <cstddef>
#include <memory>
#include <string>

namespace orchestrator::modules::crawler::infrastructure::app {

/**
 * @brief 服务器应用程序类
 * 负责组装所有组件（DB, Service, Controller, HTTP Server）并管理生命周期。
 *
 * 线程模型设计：
 * - Main Thread: 负责初始化和启动流程。
 * - IO Threads (DrogonHttpServer): 负责网络连接接入与 HTTP 协议解析。
 * - Handler Threads (ThreadPool): 负责执行 HTTP handler，避免阻塞 Drogon I/O 线程。
 * - Batch/Page Threads (ThreadPool): 负责批量任务编排和分页抓取并发。
 * - Resource Threads (ThreadPool): 负责资源下载批次内有限并发。
 * - Parse Threads (ThreadPool): 负责 JSON 解析，避免占用网络 I/O 协程线程。
 * - MySQL Threads (MySqlConnectionPool): 负责 Boost.MySQL 原生异步 I/O 和连接池维护。
 * - Persistence Threads (ThreadPool): 仅在 sqlite-debug 后端启用，桥接同步 SQLite DAO。
 *
 * 线程安全约束：
 * - Controller/Service/Repository 层必须是线程安全的（无状态或内部同步）。
 * - MySqlConnectionPool 保证异步写入连接隔离；SQLite ConnectionPool 仅用于同步调试。
 * - HttpClient 保证网络请求的并发隔离。
 */
class ServerApplication {
public:
    struct Config {
        std::string host = "0.0.0.0";
        int port = 8080;
        int http_worker_threads = 4;
        int parse_worker_threads = 2;
        int persistence_worker_threads = 2;
        int async_worker_threads = 4;
        int resource_worker_threads = 8;
        int max_batch_concurrency = 4;
        int max_page_concurrency = 4;
        int max_retries = 1;
        int task_timeout_ms = 600000;
        int mysql_startup_timeout_ms = 30000; // 启动期等待 MySQL schema 初始化的最长时间

        std::string persistence_backend = "mysql";                // 默认 mysql；可显式设为 sqlite-debug 使用同步调试存储
        std::string db_path = "song.db";                          // SQLite 调试数据库路径
        std::string sqlite_schema_path = "sql/sqlite/schema.sql"; // SQLite 调试 schema SQL
        int db_pool_size = 4;
        music::persistence::mysql::MySqlConfig mysql; // MySQL 异步持久化配置

        std::string storage_path = "./data";

        int outbound_http_io_threads = 4;                                // Asio 出站 I/O 线程数
        int outbound_http_timeout_ms = 30000;                            // 出站请求超时时间
        std::size_t outbound_http_max_response_bytes = 10 * 1024 * 1024; // 最大响应体大小
        bool outbound_http_verify_tls = true;                            // Asio HTTPS 证书校验
        std::string outbound_proxy_host;                                 // 可选 HTTP 代理主机
        std::string outbound_proxy_port;                                 // 可选 HTTP 代理端口
        std::string outbound_proxy_username;                             // 可选代理用户名
        std::string outbound_proxy_password;                             // 可选代理密码
    };

    explicit ServerApplication(const Config &config);
    ~ServerApplication();

    // 禁止拷贝与移动
    ServerApplication(const ServerApplication &) = delete;
    ServerApplication &operator=(const ServerApplication &) = delete;

    // 初始化组件栈
    // 返回 true 表示初始化成功
    bool initialize();

    // 启动服务器 (阻塞直到收到停止信号)
    void run();

    // 停止服务器
    void stop();

    // CLI 模式：直接执行爬取任务
    // 返回 true 表示执行成功
    bool executeCrawl(const std::string &url);

private:
    // 使用 PIMPL 模式隐藏组件依赖细节
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace orchestrator::modules::crawler::infrastructure::app
