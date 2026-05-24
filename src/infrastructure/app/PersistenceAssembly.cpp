#include "crawler/infrastructure/app/PersistenceAssembly.h"

#include "crawler/infrastructure/music/persistence/mysql/MySqlAlbumRepository.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlAlbumSourceRepository.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlBatchTaskRepository.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMusicCatalogQueryRepository.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlSingerRepository.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlSingerSourceRepository.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlSingerSourceSyncQueue.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlSongResourceQueue.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlSongRepository.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlSongSourceRepository.h"
#include "crawler/infrastructure/music/persistence/sqlite/AlbumDao.h"
#include "crawler/infrastructure/music/persistence/sqlite/SingerDao.h"
#include "crawler/infrastructure/music/persistence/sqlite/SongDao.h"
#include "crawler/infrastructure/observability/logging/Logger.h"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/use_future.hpp>

#include <algorithm>
#include <chrono>
#include <cctype>
#include <exception>
#include <future>
#include <string>
#include <string_view>

namespace orchestrator::modules::crawler::infrastructure::app {

namespace {

namespace logging = orchestrator::modules::crawler::infrastructure::observability::logging;

// 记录组合根持久化装配日志，统一走运行日志出口。
void logApp(logging::LogLevel level, std::string_view message)
{
    logging::Logger::instance().log(level, "App", message);
}

// 将后端名称规整为小写，避免配置大小写差异导致误判。
std::string normalizeBackend(std::string_view value)
{
    std::string normalized(value);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return normalized;
}

// 判断是否启用 SQLite 同步调试存储。
bool isSqliteDebugBackend(std::string_view value)
{
    const std::string backend = normalizeBackend(value);
    return backend == "sqlite-debug" || backend == "sqlite" || backend == "debug-sqlite";
}

// 根据配置构造 MySQL 歌曲仓储 SQL 路径。
music::persistence::mysql::MySqlSongRepository::SqlPaths makeSongSqlPaths(const music::persistence::mysql::MySqlConfig &config)
{
    return {
        config.song_upsert_path,
        config.song_delete_path,
        config.song_find_path,
        config.song_find_all_path,
    };
}

// 根据配置构造 MySQL 专辑仓储 SQL 路径。
music::persistence::mysql::MySqlAlbumRepository::SqlPaths makeAlbumSqlPaths(const music::persistence::mysql::MySqlConfig &config)
{
    return {
        config.album_upsert_path,
        config.album_delete_path,
        config.album_find_path,
        config.album_find_all_path,
    };
}

// 根据配置构造 MySQL 歌手仓储 SQL 路径。
music::persistence::mysql::MySqlSingerRepository::SqlPaths makeSingerSqlPaths(const music::persistence::mysql::MySqlConfig &config)
{
    return {
        config.singer_upsert_path,
        config.singer_delete_path,
        config.singer_find_path,
        config.singer_find_all_path,
    };
}

// 启动阶段阻塞等待 MySQL schema 初始化完成；运行期写入仍然是原生协程异步。
bool initializeMySqlSchema(
    const std::shared_ptr<music::persistence::mysql::MySqlConnectionPool> &pool,
    std::chrono::milliseconds timeout
)
{
    if (!pool) {
        return false;
    }

    auto future = boost::asio::co_spawn(pool->executor(), pool->coInitializeSchema(), boost::asio::use_future);
    if (future.wait_for(timeout) != std::future_status::ready) {
        logApp(logging::LogLevel::L_ERROR, "MySQL schema initialization timed out after " + std::to_string(timeout.count()) + " ms.");
        pool->cancel();
        return false;
    }

    try {
        return future.get();
    } catch (const std::exception &e) {
        logApp(logging::LogLevel::L_ERROR, std::string("MySQL schema initialization failed: ") + e.what());
        return false;
    }
}

} // namespace

std::optional<PersistenceComponents> assemblePersistence(const ServerApplication::Config &config)
{
    PersistenceComponents components;
    if (isSqliteDebugBackend(config.persistence_backend)) {
        const int persistence_worker_count = (std::max)(1, config.persistence_worker_threads);
        components.persistence_worker_pool = std::make_shared<execution::threading::ThreadPool>(persistence_worker_count);
        components.sqlite_pool = std::make_unique<music::persistence::sqlite::ConnectionPool>(
            config.db_path,
            config.db_pool_size,
            config.sqlite_schema_path
        );
        components.song_repository = std::make_shared<music::persistence::sqlite::SongDao>(*components.sqlite_pool);
        components.album_repository = std::make_shared<music::persistence::sqlite::AlbumDao>(*components.sqlite_pool);
        components.singer_repository = std::make_shared<music::persistence::sqlite::SingerDao>(*components.sqlite_pool);
        components.persistence_executor = components.persistence_worker_pool;
        logApp(logging::LogLevel::L_INFO, "Persistence backend: sqlite-debug (sync DAO through worker pool).");
        return components;
    }

    components.mysql_pool = std::make_shared<music::persistence::mysql::MySqlConnectionPool>(config.mysql);
    if (!initializeMySqlSchema(components.mysql_pool, std::chrono::milliseconds(config.mysql_startup_timeout_ms))) {
        logApp(logging::LogLevel::L_ERROR, "MySQL schema initialization failed.");
        return std::nullopt;
    }
    components.song_repository = std::make_shared<music::persistence::mysql::MySqlSongRepository>(components.mysql_pool, makeSongSqlPaths(config.mysql));
    components.song_source_repository = std::make_shared<music::persistence::mysql::MySqlSongSourceRepository>(components.mysql_pool);
    components.singer_source_repository = std::make_shared<music::persistence::mysql::MySqlSingerSourceRepository>(components.mysql_pool);
    components.singer_source_sync_queue = std::make_shared<music::persistence::mysql::MySqlSingerSourceSyncQueue>(components.mysql_pool);
    components.album_source_repository = std::make_shared<music::persistence::mysql::MySqlAlbumSourceRepository>(components.mysql_pool);
    components.album_repository = std::make_shared<music::persistence::mysql::MySqlAlbumRepository>(components.mysql_pool, makeAlbumSqlPaths(config.mysql));
    components.singer_repository = std::make_shared<music::persistence::mysql::MySqlSingerRepository>(components.mysql_pool, makeSingerSqlPaths(config.mysql));
    components.batch_task_repository = std::make_shared<music::persistence::mysql::MySqlBatchTaskRepository>(components.mysql_pool);
    components.catalog_query_repository = std::make_shared<music::persistence::mysql::MySqlMusicCatalogQueryRepository>(components.mysql_pool);
    components.song_resource_queue = std::make_shared<music::persistence::mysql::MySqlSongResourceQueue>(components.mysql_pool);
    logApp(logging::LogLevel::L_INFO, "Persistence backend: mysql (native async repository).");
    return components;
}

} // namespace orchestrator::modules::crawler::infrastructure::app
