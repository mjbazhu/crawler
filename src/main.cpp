#include "crawler/infrastructure/app/ServerApplication.h"
#include "crawler/infrastructure/platform/Environment.h"
#include "crawler/infrastructure/platform/PlatformUtils.h"

#include <atomic>
#include <chrono>
#include <csignal>
#include <cstddef>
#include <iostream>
#include <string>
#include <thread>

std::atomic<bool> g_running {true};

void signalHandler(int)
{
    g_running = false;
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
        std::thread server_thread([&app]() {
            app.run();
        });

        // 主线程等待停止信号
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
        }

        app.stop();
        if (server_thread.joinable()) {
            server_thread.join();
        }
    }
    return 0;
}
