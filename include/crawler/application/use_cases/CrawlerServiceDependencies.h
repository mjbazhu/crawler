#pragma once

#include "crawler/application/model/CrawlerRuntimeSettings.h"
#include "crawler/application/ports/diagnostics/DiagnosticsSink.h"
#include "crawler/application/ports/execution/TaskExecutor.h"
#include "crawler/application/ports/parsing/AlbumSourceParser.h"
#include "crawler/application/ports/parsing/PageInspector.h"
#include "crawler/application/ports/parsing/ParseResultParser.h"
#include "crawler/application/ports/parsing/SingerProfileParser.h"
#include "crawler/application/ports/parsing/SingerSourceParser.h"
#include "crawler/application/ports/parsing/SongResourceParser.h"
#include "crawler/application/ports/parsing/SongSourceParser.h"
#include "crawler/application/ports/persistence/AlbumRepository.h"
#include "crawler/application/ports/persistence/MetadataBatchWriter.h"
#include "crawler/application/ports/storage/MetadataCoverStore.h"
#include "crawler/application/ports/persistence/AlbumSourceSyncQueue.h"
#include "crawler/application/ports/persistence/AlbumSourceWriter.h"
#include "crawler/application/ports/persistence/BatchTaskRepository.h"
#include "crawler/application/ports/persistence/AsyncSongResourceQueue.h"
#include "crawler/application/ports/persistence/MusicCatalogQueryRepository.h"
#include "crawler/application/ports/persistence/SingerRepository.h"
#include "crawler/application/ports/persistence/SingerSourceRepository.h"
#include "crawler/application/ports/persistence/SingerSourceSyncQueue.h"
#include "crawler/application/ports/persistence/SongResourceQueue.h"
#include "crawler/application/ports/persistence/SongRepository.h"
#include "crawler/application/ports/persistence/SongSourceRepository.h"
#include "crawler/application/ports/provider/AlbumDetailProvider.h"
#include "crawler/application/ports/provider/AlbumSearchProvider.h"
#include "crawler/application/ports/provider/AsyncSongResourceContentFetcher.h"
#include "crawler/application/ports/provider/AsyncSongResourceProvider.h"
#include "crawler/application/ports/provider/MiguPacmtokenRefresher.h"
#include "crawler/application/ports/provider/MiguPacmtokenStore.h"
#include "crawler/application/ports/provider/RawPageFetcher.h"
#include "crawler/application/ports/provider/SingerCatalogProvider.h"
#include "crawler/application/ports/provider/SongResourceContentFetcher.h"
#include "crawler/application/ports/provider/SongResourceProvider.h"
#include "crawler/application/ports/provider/SongSearchProvider.h"
#include "crawler/application/ports/storage/FileStore.h"
#include "crawler/application/ports/storage/AsyncSongResourceBlobStore.h"
#include "crawler/application/ports/storage/AsyncSongResourceBatchCloudUploader.h"
#include "crawler/application/ports/storage/AsyncSongResourceCloudUploader.h"
#include "crawler/application/ports/storage/AsyncSongResourceRemoteCopier.h"
#include "crawler/application/ports/storage/SongResourceCloudUploader.h"
#include "crawler/application/ports/storage/SongResourceBatchCloudUploader.h"
#include "crawler/application/ports/storage/SongResourceBlobStore.h"
#include "crawler/application/ports/execution/AsyncRuntime.h"

#include <chrono>
#include <memory>

namespace orchestrator::modules::crawler::application::use_cases {

// Crawler provider 依赖组，新增 use case 必须依赖窄 provider port。
struct CrawlerProviderPorts {
    std::shared_ptr<application::ports::RawPageFetcher> raw_page_fetcher;                                     // URL 原始响应 port
    std::shared_ptr<application::ports::SongSearchProvider> song_search_provider;                             // 歌曲搜索 port
    std::shared_ptr<application::ports::AlbumSearchProvider> album_search_provider;                           // 专辑搜索 port
    std::shared_ptr<application::ports::SingerCatalogProvider> singer_catalog_provider;                       // 歌手目录 port
    std::shared_ptr<application::ports::AlbumDetailProvider> album_detail_provider;                           // 专辑详情 port
    std::shared_ptr<application::ports::SongResourceProvider> song_resource_provider;                         // 歌曲资源 URL 获取 port
    std::shared_ptr<application::ports::SongResourceContentFetcher> song_resource_content_fetcher;            // 歌曲资源内容下载 port
    std::shared_ptr<application::ports::AsyncSongResourceProvider> async_song_resource_provider;              // 异步歌曲资源 URL 获取 port
    std::shared_ptr<application::ports::AsyncSongResourceContentFetcher> async_song_resource_content_fetcher; // 异步歌曲资源内容下载 port
    std::shared_ptr<application::ports::MiguPacmtokenRefresher> migu_pacmtoken_refresher;                     // Migu pacmtoken 刷新 port
    std::shared_ptr<application::ports::MiguPacmtokenStore> migu_pacmtoken_store;                             // Migu pacmtoken 运行期存储 port
};

// Crawler parser 依赖组，新增解析逻辑必须依赖窄 parser port。
struct CrawlerParserPorts {
    std::shared_ptr<application::ports::ParseResultParser> parse_result_parser;     // 领域结果解析 port
    std::shared_ptr<application::ports::SongSourceParser> song_source_parser;       // 歌曲来源解析 port
    std::shared_ptr<application::ports::SingerSourceParser> singer_source_parser;   // 歌手来源解析 port
    std::shared_ptr<application::ports::AlbumSourceParser> album_source_parser;     // 专辑来源解析 port
    std::shared_ptr<application::ports::SingerProfileParser> singer_profile_parser; // 歌手概览解析 port
    std::shared_ptr<application::ports::PageInspector> page_inspector;              // 分页摘要 port
    std::shared_ptr<application::ports::SongResourceParser> song_resource_parser;   // 歌曲资源响应解析 port
};

// Crawler 持久化依赖组，专辑来源写入和同步队列保持拆分。
struct CrawlerPersistencePorts {
    std::shared_ptr<application::ports::MetadataBatchWriter> metadata_batch_writer;                                    // 实体及关系原子写入 port
    std::shared_ptr<application::ports::MetadataCoverStore> metadata_cover_store;                                      // 采集阶段本地封面 port
    std::shared_ptr<application::ports::FileStore> file_store;                                                         // 文件存储 port
    std::shared_ptr<application::ports::SongRepository> song_repository;                                               // 歌曲仓储 port
    std::shared_ptr<application::ports::AlbumRepository> album_repository;                                             // 专辑仓储 port
    std::shared_ptr<application::ports::SingerRepository> singer_repository;                                           // 歌手仓储 port
    std::shared_ptr<application::ports::SongSourceRepository> song_source_repository;                                  // 歌曲来源仓储 port
    std::shared_ptr<application::ports::SingerSourceRepository> singer_source_repository;                              // 歌手来源仓储 port
    std::shared_ptr<application::ports::SingerSourceSyncQueue> singer_source_sync_queue;                               // 歌手来源同步队列 port
    std::shared_ptr<application::ports::AlbumSourceWriter> album_source_writer;                                        // 专辑来源写入 port
    std::shared_ptr<application::ports::AlbumSourceSyncQueue> album_source_sync_queue;                                 // 专辑来源同步队列 port
    std::shared_ptr<application::ports::BatchTaskRepository> batch_task_repository;                                    // 批次任务仓储 port
    std::shared_ptr<application::ports::MusicCatalogQueryRepository> catalog_query_repository;                         // 音乐目录查询 port
    std::shared_ptr<application::ports::SongResourceQueue> song_resource_queue;                                        // 歌曲资源处理队列 port
    std::shared_ptr<application::ports::SongResourceBlobStore> song_resource_blob_store;                               // 歌曲资源二进制保存 port
    std::shared_ptr<application::ports::SongResourceCloudUploader> song_resource_cloud_uploader;                       // 歌曲资源云端上传 port
    std::shared_ptr<application::ports::SongResourceBatchCloudUploader> song_resource_batch_cloud_uploader;            // 歌曲资源批量云端上传 port
    std::shared_ptr<application::ports::AsyncSongResourceQueue> async_song_resource_queue;                             // 异步歌曲资源处理队列 port
    std::shared_ptr<application::ports::AsyncSongResourceBlobStore> async_song_resource_blob_store;                    // 异步歌曲资源二进制保存 port
    std::shared_ptr<application::ports::AsyncSongResourceCloudUploader> async_song_resource_cloud_uploader;            // 异步歌曲资源云端上传 port
    std::shared_ptr<application::ports::AsyncSongResourceBatchCloudUploader> async_song_resource_batch_cloud_uploader; // 异步歌曲资源批量云端上传 port
    std::shared_ptr<application::ports::AsyncSongResourceRemoteCopier> async_song_resource_remote_copier;              // 异步远端 URL 复制 port
};

// Crawler 执行器依赖组，按任务类型隔离并发策略。
struct CrawlerExecutorPorts {
    std::shared_ptr<application::ports::TaskExecutor> parse_executor;       // 解析执行器
    std::shared_ptr<application::ports::TaskExecutor> batch_executor;       // 批量任务执行器
    std::shared_ptr<application::ports::TaskExecutor> page_executor;        // 分页执行器
    std::shared_ptr<application::ports::TaskExecutor> persistence_executor; // 持久化执行器
    std::shared_ptr<application::ports::TaskExecutor> resource_executor;    // 资源下载执行器
    std::shared_ptr<application::ports::AsyncRuntime> async_runtime;        // 异步资源管线 runtime
};

// Crawler 运行选项，保留旧构造函数的默认语义。
struct CrawlerRuntimeOptions {
    int max_batch_concurrency = 4;                                     // 批量并发上限
    int max_page_concurrency = 4;                                      // 分页并发上限
    int max_retries = 1;                                               // 目标重试次数
    std::chrono::milliseconds task_timeout = std::chrono::minutes(10); // 任务超时
    application::model::CrawlerRuntimeSettings runtime_settings {};    // 可热更新运行设置
};

// CrawlerService 窄依赖结构，避免继续扩展超长构造函数。
struct CrawlerServiceDependencies {
    CrawlerProviderPorts providers;                                   // provider 依赖组
    CrawlerParserPorts parsers;                                       // parser 依赖组
    CrawlerPersistencePorts persistence;                              // 持久化依赖组
    CrawlerExecutorPorts executors;                                   // 执行器依赖组
    CrawlerRuntimeOptions options;                                    // 运行选项
    std::shared_ptr<application::ports::DiagnosticsSink> diagnostics; // 诊断输出 port
};

} // namespace orchestrator::modules::crawler::application::use_cases
