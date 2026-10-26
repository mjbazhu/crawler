#include "crawler/infrastructure/app/ServerApplication.h"
#include "crawler/infrastructure/observability/logging/Logger.h"
#include "crawler/infrastructure/platform/Environment.h"
#include "crawler/infrastructure/platform/PlatformUtils.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <cctype>
#include <iostream>
#include <string>
#include <string_view>
#include <thread>
#include <utility>

std::atomic<bool> g_running {true};

void signalHandler(int)
{
    g_running = false;
}

std::string lowerAscii(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

orchestrator::modules::crawler::infrastructure::observability::logging::LogLevel parseLogLevel(std::string_view value)
{
    namespace logging = orchestrator::modules::crawler::infrastructure::observability::logging;
    const std::string normalized = lowerAscii(std::string(value));
    if (normalized == "debug") return logging::LogLevel::L_DEBUG;
    if (normalized == "warn" || normalized == "warning") return logging::LogLevel::L_WARN;
    if (normalized == "error") return logging::LogLevel::L_ERROR;
    return logging::LogLevel::L_INFO;
}

int main(int argc, char *argv[])
{
    // 1. 禁用 Windows 控制台的快速编辑模式
    namespace env = ::orchestrator::modules::crawler::infrastructure::platform;
    env::setupConsole();

    // 本地调试可通过 CRAWLER_ENV_FILE 指定 .env.test；未指定时默认读取 .env。
    const auto env_file = env::Environment::getString("CRAWLER_ENV_FILE");
    if (env_file) {
        env::Environment::loadDotEnvFile(*env_file);
    } else {
        env::Environment::loadDotEnvFile(".env");
    }

    namespace logging = ::orchestrator::modules::crawler::infrastructure::observability::logging;
    logging::Logger::Options log_options;
    log_options.level = parseLogLevel(env::Environment::getStringOr("CRAWLER_LOG_LEVEL", "info"));
    log_options.console_enabled = env::Environment::getBoolOr("CRAWLER_LOG_CONSOLE", true);
    log_options.text_file_enabled = env::Environment::getBoolOr("CRAWLER_LOG_TEXT_FILE_ENABLED", true);
    log_options.text_file_path = env::Environment::getStringOr("CRAWLER_LOG_TEXT_FILE", "logs/crawler.log");
    log_options.json_file_enabled = env::Environment::getBoolOr("CRAWLER_LOG_JSON_FILE_ENABLED", false);
    log_options.json_file_path = env::Environment::getStringOr("CRAWLER_LOG_JSON_FILE", "logs/crawler.jsonl");
    logging::Logger::instance().configure(std::move(log_options));

    std::signal(SIGINT, signalHandler);

    orchestrator::modules::crawler::infrastructure::app::ServerApplication::Config config;
    config.host = env::Environment::getStringOr("CRAWLER_HOST", "127.0.0.1");
    config.port = env::Environment::getIntOr("CRAWLER_PORT", 8080);
    config.http_worker_threads = env::Environment::getIntOr("CRAWLER_HTTP_WORKER_THREADS", config.http_worker_threads);
    config.async_worker_threads = env::Environment::getIntOr("CRAWLER_ASYNC_WORKER_THREADS", config.async_worker_threads);
    config.resource_worker_threads = env::Environment::getIntOr("CRAWLER_RESOURCE_WORKER_THREADS", config.resource_worker_threads);
    config.parse_worker_threads = env::Environment::getIntOr("CRAWLER_PARSE_WORKER_THREADS", config.parse_worker_threads);
    config.persistence_worker_threads = env::Environment::getIntOr("CRAWLER_PERSISTENCE_WORKER_THREADS", config.persistence_worker_threads);
    config.mysql_startup_timeout_ms = env::Environment::getIntOr("CRAWLER_MYSQL_STARTUP_TIMEOUT_MS", config.mysql_startup_timeout_ms);
    config.persistence_backend = env::Environment::getStringOr("CRAWLER_PERSISTENCE_BACKEND", config.persistence_backend);
    config.db_path = env::Environment::getStringOr("CRAWLER_SQLITE_DB_PATH", "./data/song.db");
    config.storage_path = env::Environment::getStringOr("CRAWLER_STORAGE_PATH", config.storage_path);
    config.resource_storage_backend = env::Environment::getStringOr("CRAWLER_RESOURCE_STORAGE_BACKEND", config.resource_storage_backend);
    config.resource_cloud_upload_enabled = env::Environment::getBoolOr("CRAWLER_RESOURCE_CLOUD_UPLOAD_ENABLED", config.resource_cloud_upload_enabled);
    config.resource_cloud_upload_backend = env::Environment::getStringOr("CRAWLER_RESOURCE_CLOUD_UPLOAD_BACKEND", config.resource_cloud_upload_backend);
    // 当前采集主流程只落本地并回写 SQL，网盘上传由人工上传和对账脚本处理。
    config.baidupcs.enabled = false;
    config.baidupcs.local_root = config.storage_path;
    config.baidupcs.cloud_root = env::Environment::getStringOr("CRAWLER_RESOURCE_CLOUD_ROOT", config.baidupcs.cloud_root);
    config.baidupcs.executable_path = env::Environment::getStringOr("CRAWLER_BAIDUPCS_GO_PATH", config.baidupcs.executable_path);
    config.baidupcs.upload_parallel = env::Environment::getIntOr("CRAWLER_BAIDUPCS_UPLOAD_PARALLEL", config.baidupcs.upload_parallel);
    config.baidupcs.upload_load = env::Environment::getIntOr("CRAWLER_BAIDUPCS_UPLOAD_LOAD", config.baidupcs.upload_load);
    config.baidupcs.upload_retries = env::Environment::getIntOr("CRAWLER_BAIDUPCS_UPLOAD_RETRIES", config.baidupcs.upload_retries);
    config.baidupcs.upload_policy = env::Environment::getStringOr("CRAWLER_BAIDUPCS_UPLOAD_POLICY", config.baidupcs.upload_policy);
    config.rclone.rc_url = env::Environment::getStringOr("CRAWLER_RCLONE_RC_URL", config.rclone.rc_url);
    config.rclone.rc_user = env::Environment::getStringOr("CRAWLER_RCLONE_RC_USER", config.rclone.rc_user);
    config.rclone.rc_password = env::Environment::getStringOr("CRAWLER_RCLONE_RC_PASSWORD", config.rclone.rc_password);
    config.rclone.target_fs = env::Environment::getStringOr("CRAWLER_RCLONE_TARGET_FS", config.rclone.target_fs);
    config.rclone.api_timeout_ms = env::Environment::getIntOr("CRAWLER_RCLONE_API_TIMEOUT_MS", config.rclone.api_timeout_ms);
    config.rclone.job_timeout_ms = env::Environment::getIntOr("CRAWLER_RCLONE_JOB_TIMEOUT_MS", config.rclone.job_timeout_ms);
    config.rclone.job_poll_interval_ms = env::Environment::getIntOr("CRAWLER_RCLONE_JOB_POLL_INTERVAL_MS", config.rclone.job_poll_interval_ms);
    config.rclone.api_max_response_bytes = static_cast<std::size_t>(env::Environment::getIntOr("CRAWLER_RCLONE_API_MAX_RESPONSE_BYTES", static_cast<int>(config.rclone.api_max_response_bytes)));
    config.outbound_http_io_threads = env::Environment::getIntOr("CRAWLER_OUTBOUND_HTTP_IO_THREADS", config.outbound_http_io_threads);
    config.outbound_http_timeout_ms = env::Environment::getIntOr("CRAWLER_OUTBOUND_HTTP_TIMEOUT_MS", config.outbound_http_timeout_ms);
    config.outbound_http_max_response_bytes = static_cast<std::size_t>(env::Environment::getIntOr(
        "CRAWLER_OUTBOUND_MAX_RESPONSE_BYTES",
        env::Environment::getIntOr("CRAWLER_RESOURCE_MAX_RESPONSE_BYTES", static_cast<int>(config.outbound_http_max_response_bytes))
    ));
    config.outbound_http_verify_tls = env::Environment::getBoolOr("CRAWLER_OUTBOUND_VERIFY_TLS", config.outbound_http_verify_tls);

    config.mysql.host = env::Environment::getStringOr("CRAWLER_MYSQL_HOST", config.mysql.host);
    config.mysql.port = env::Environment::getPortOr("CRAWLER_MYSQL_PORT", config.mysql.port);
    config.mysql.username = env::Environment::getStringOr("CRAWLER_MYSQL_USER", config.mysql.username);
    config.mysql.password = env::Environment::getStringOr("CRAWLER_MYSQL_PASSWORD", config.mysql.password);
    config.mysql.database = env::Environment::getStringOr("CRAWLER_MYSQL_DATABASE", config.mysql.database);
    config.mysql.migrations_path = env::Environment::getStringOr("CRAWLER_MYSQL_MIGRATIONS_PATH", config.mysql.migrations_path);
    config.mysql.initialize_schema = env::Environment::getBoolOr("CRAWLER_MYSQL_INIT_SCHEMA", config.mysql.initialize_schema);
    config.mysql.io_threads = env::Environment::getIntOr("CRAWLER_MYSQL_IO_THREADS", config.mysql.io_threads);
    config.mysql.max_pool_size = static_cast<std::size_t>(env::Environment::getIntOr("CRAWLER_MYSQL_MAX_POOL_SIZE", static_cast<int>(config.mysql.max_pool_size)));
    config.mysql.connect_timeout = std::chrono::seconds(env::Environment::getIntOr("CRAWLER_MYSQL_CONNECT_TIMEOUT_SECONDS", static_cast<int>(config.mysql.connect_timeout.count())));
    config.mysql.retry_interval = std::chrono::seconds(env::Environment::getIntOr("CRAWLER_MYSQL_RETRY_INTERVAL_SECONDS", static_cast<int>(config.mysql.retry_interval.count())));
    config.mysql.ping_timeout = std::chrono::seconds(env::Environment::getIntOr("CRAWLER_MYSQL_PING_TIMEOUT_SECONDS", static_cast<int>(config.mysql.ping_timeout.count())));

    orchestrator::modules::crawler::infrastructure::app::ServerApplication app(config);

    if (!app.initialize()) {
        return 1;
    }

    // 简单参数解析
    std::string crawl_target;
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--crawl" && i + 1 < argc) {
            crawl_target = argv[++i];
        }
    }

    if (!crawl_target.empty()) {
        app.executeCrawl(crawl_target);
    } else {
        // 在独立线程中启动服务器，防止阻塞主线程处理信号
        std::atomic<bool> server_finished {false}; // Drogon 收到信号并退出后通知主线程结束等待。
        std::thread server_thread([&app, &server_finished]() {
            app.run();
            server_finished = true;
        });

        // 主线程等待停止信号
        while (g_running && !server_finished) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        app.stop();
        if (server_thread.joinable()) {
            server_thread.join();
        }
    }
    return 0;
}
