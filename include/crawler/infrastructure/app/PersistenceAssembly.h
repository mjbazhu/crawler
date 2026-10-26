#pragma once

#include "crawler/application/ports/execution/TaskExecutor.h"
#include "crawler/application/ports/persistence/AlbumRepository.h"
#include "crawler/application/ports/persistence/MetadataBatchWriter.h"
#include "crawler/application/ports/persistence/AlbumSourceRepository.h"
#include "crawler/application/ports/persistence/BatchTaskRepository.h"
#include "crawler/application/ports/persistence/MusicCatalogQueryRepository.h"
#include "crawler/application/ports/persistence/SingerRepository.h"
#include "crawler/application/ports/persistence/SingerSourceRepository.h"
#include "crawler/application/ports/persistence/SingerSourceSyncQueue.h"
#include "crawler/application/ports/persistence/SongResourceQueue.h"
#include "crawler/application/ports/persistence/SongRepository.h"
#include "crawler/application/ports/persistence/SongSourceRepository.h"
#include "crawler/infrastructure/app/ServerApplication.h"
#include "crawler/infrastructure/execution/threading/ThreadPool.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlConnectionPool.h"
#include "crawler/infrastructure/music/persistence/sqlite/ConnectionPool.h"

#include <memory>
#include <optional>

namespace orchestrator::modules::crawler::infrastructure::app {

// 持久化装配结果，组合根接收后负责保存生命周期。
struct PersistenceComponents {
    std::shared_ptr<application::ports::MetadataBatchWriter> metadata_batch_writer;            // 元数据事务 writer
    std::unique_ptr<music::persistence::sqlite::ConnectionPool> sqlite_pool;                   // SQLite 调试连接池
    std::shared_ptr<music::persistence::mysql::MySqlConnectionPool> mysql_pool;                // MySQL 连接池
    std::shared_ptr<execution::threading::ThreadPool> persistence_worker_pool;                 // 同步持久化 worker
    std::shared_ptr<application::ports::TaskExecutor> persistence_executor;                    // application 持久化执行器
    std::shared_ptr<application::ports::SongRepository> song_repository;                       // 歌曲仓储
    std::shared_ptr<application::ports::SongSourceRepository> song_source_repository;          // 歌曲来源仓储
    std::shared_ptr<application::ports::SingerSourceRepository> singer_source_repository;      // 歌手来源仓储
    std::shared_ptr<application::ports::SingerSourceSyncQueue> singer_source_sync_queue;       // 歌手来源同步队列
    std::shared_ptr<application::ports::AlbumSourceRepository> album_source_repository;        // 专辑来源兼容仓储
    std::shared_ptr<application::ports::AlbumRepository> album_repository;                     // 专辑仓储
    std::shared_ptr<application::ports::SingerRepository> singer_repository;                   // 歌手仓储
    std::shared_ptr<application::ports::BatchTaskRepository> batch_task_repository;            // 批次任务仓储
    std::shared_ptr<application::ports::MusicCatalogQueryRepository> catalog_query_repository; // 控制台总览查询仓储
    std::shared_ptr<application::ports::SongResourceQueue> song_resource_queue;                // 歌曲资源处理队列
};

// 根据持久化配置装配 SQLite/MySQL adapter，并执行必要的启动期 schema 初始化。
std::optional<PersistenceComponents> assemblePersistence(const ServerApplication::Config &config);

} // namespace orchestrator::modules::crawler::infrastructure::app
