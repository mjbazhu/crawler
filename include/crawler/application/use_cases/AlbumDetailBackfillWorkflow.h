#pragma once

#include "crawler/application/ports/diagnostics/DiagnosticsSink.h"
#include "crawler/application/ports/execution/TaskExecutor.h"
#include "crawler/application/ports/persistence/AlbumSourceSyncQueue.h"
#include "crawler/application/ports/provider/AlbumDetailProvider.h"
#include "crawler/application/use_cases/MetadataPersistenceService.h"
#include "crawler/application/use_cases/ParserDispatch.h"
#include "crawler/application/use_cases/TaskRunner.h"

#include <memory>
#include <string>

namespace orchestrator::modules::crawler::application::use_cases {

// 专辑详情补齐 workflow，负责 pending 专辑详情抓取和同步状态更新。
class AlbumDetailBackfillWorkflow {
public:
    // 保存专辑详情 provider、同步队列、解析调度器、持久化服务和分页执行器。
    AlbumDetailBackfillWorkflow(
        std::shared_ptr<application::ports::AlbumDetailProvider> album_detail_provider,
        std::shared_ptr<application::ports::AlbumSourceSyncQueue> album_source_sync_queue,
        std::shared_ptr<ParserDispatch> parser,
        std::shared_ptr<MetadataPersistenceService> persistence,
        std::shared_ptr<application::ports::TaskExecutor> page_executor,
        std::shared_ptr<application::ports::DiagnosticsSink> diagnostics
    );

    // 获取待补齐专辑详情并保存详情与歌曲来源参数。
    bool crawlPendingAlbumDetailsAndSave(std::string source, int limit, int max_page_concurrency);

    // 获取待补齐专辑歌曲并保存歌曲来源参数。
    bool crawlPendingAlbumSongsAndSave(std::string source, int limit, int max_page_concurrency);

    // 查询待补齐详情的专辑来源。
    std::vector<application::model::AlbumSourceMetadata> pendingAlbumDetails(std::string source, int limit);

    // 查询待补齐歌曲的专辑来源。
    std::vector<application::model::AlbumSourceMetadata> pendingAlbumSongs(std::string source, int limit);

    // 补齐单个专辑详情。
    bool crawlAlbumDetailAndSave(application::model::AlbumSourceMetadata source_metadata);

    // 补齐单个专辑歌曲。
    bool crawlAlbumSongsAndSave(application::model::AlbumSourceMetadata source_metadata);

private:
    std::shared_ptr<application::ports::AlbumDetailProvider> album_detail_provider_;    // 专辑详情 port
    std::shared_ptr<application::ports::AlbumSourceSyncQueue> album_source_sync_queue_; // 专辑同步队列 port
    std::shared_ptr<ParserDispatch> parser_;                                            // 解析调度器
    std::shared_ptr<MetadataPersistenceService> persistence_;                           // 持久化服务
    TaskRunner page_runner_;                                                            // 分页执行器
};

} // namespace orchestrator::modules::crawler::application::use_cases
