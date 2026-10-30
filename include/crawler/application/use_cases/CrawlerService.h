#pragma once

#include "crawler/application/model/CrawlerRuntimeSettings.h"
#include "crawler/application/model/CrawlBatch.h"
#include "crawler/application/model/MusicCatalog.h"
#include "crawler/application/model/MusicOverview.h"
#include "crawler/application/model/SingerCollectionResult.h"
#include "crawler/application/model/SingerSourceSync.h"
#include "crawler/application/model/SongResourceQueue.h"
#include "crawler/application/ports/parsing/MetadataParser.h"
#include "crawler/application/ports/persistence/AlbumRepository.h"
#include "crawler/application/ports/persistence/AlbumSourceRepository.h"
#include "crawler/application/ports/persistence/SingerRepository.h"
#include "crawler/application/ports/persistence/SingerSourceRepository.h"
#include "crawler/application/ports/persistence/SongRepository.h"
#include "crawler/application/ports/persistence/SongSourceRepository.h"
#include "crawler/application/ports/execution/TaskExecutor.h"
#include "crawler/application/ports/provider/IMusicProvider.h"
#include "crawler/application/ports/storage/FileStore.h"
#include "crawler/application/use_cases/BatchTaskManager.h"
#include "crawler/application/use_cases/AsyncSongResourceDownloadWorkflow.h"
#include "crawler/application/use_cases/CrawlerServiceDependencies.h"
#include "crawler/application/use_cases/CrawlerBatchQueryService.h"
#include "crawler/application/use_cases/MusicCatalogQueryService.h"
#include "crawler/application/use_cases/MusicCollectionWorkflow.h"
#include "crawler/application/use_cases/SongResourceDownloadWorkflow.h"
#include "crawler/application/use_cases/TaskRunner.h"
#include "crawler/application/use_cases/TargetDispatcher.h"

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace orchestrator::modules::crawler::application::use_cases {

// Crawler application 门面，负责对外暴露用例并委托给更小职责组件。
class CrawlerService {
public:
    // 使用窄 port 依赖结构创建 Crawler 门面。
    explicit CrawlerService(CrawlerServiceDependencies dependencies);

    // 兼容旧聚合接口的依赖注入入口；新增装配请使用 CrawlerServiceDependencies。
    [[deprecated("Use CrawlerServiceDependencies")]]
    CrawlerService(
        std::shared_ptr<application::ports::IMusicProvider> provider,
        std::shared_ptr<application::ports::FileStore> file_store,
        std::shared_ptr<application::ports::SongRepository> song_repository,
        std::shared_ptr<application::ports::AlbumRepository> album_repository,
        std::shared_ptr<application::ports::SingerRepository> singer_repository,
        std::shared_ptr<application::ports::MetadataParser> parser,
        std::shared_ptr<application::ports::TaskExecutor> parse_executor,
        std::shared_ptr<application::ports::TaskExecutor> batch_executor,
        std::shared_ptr<application::ports::TaskExecutor> page_executor,
        std::shared_ptr<application::ports::TaskExecutor> persistence_executor = nullptr,
        int max_batch_concurrency = 4,
        int max_page_concurrency = 4,
        int max_retries = 1,
        std::chrono::milliseconds task_timeout = std::chrono::minutes(10),
        application::model::CrawlerRuntimeSettings runtime_settings = {},
        std::shared_ptr<application::ports::SongSourceRepository> song_source_repository = nullptr,
        std::shared_ptr<application::ports::SingerSourceRepository> singer_source_repository = nullptr,
        std::shared_ptr<application::ports::AlbumSourceRepository> album_source_repository = nullptr
    );

    // 执行完整的爬取流程，包含下载、解析、存储和入库。
    bool processUrl(std::string url, int limit);

    // 执行搜索并保存流程；若 target 是 URL，则进入 URL 爬取流程。
    bool searchAndSave(std::string keyword, int limit);

    // 获取指定 Migu 歌手的歌曲分页，并把歌曲来源参数写入持久化仓储。
    bool crawlSingerSongsAndSave(std::uint64_t singer_id, int limit);

    // 获取指定 Migu 歌手分类列表，并把歌手来源参数写入持久化仓储。
    bool crawlSingerIndexAndSave(std::string tab, int limit);

    // 获取指定 Migu 歌手专辑列表，并把专辑来源参数写入持久化仓储；详情补齐由独立用例处理。
    bool crawlSingerAlbumsAndSave(std::uint64_t singer_id, int limit);

    // 获取待补齐的专辑详情，并把详情参数写入持久化仓储。
    bool crawlPendingAlbumDetailsAndSave(std::string source, int limit);

    // 获取待补齐的专辑歌曲，并把歌曲来源参数写入持久化仓储。
    bool crawlPendingAlbumSongsAndSave(std::string source, int limit);

    // 兼容同步调试入口，等价于 processUrl。
    bool processUrlSync(std::string_view url, int limit);

    // 兼容同步调试入口，等价于 searchAndSave。
    bool searchAndSaveSync(std::string_view keyword, int limit);

    // 兼容同步调试入口，等价于 crawlSingerSongsAndSave。
    bool crawlSingerSongsAndSaveSync(std::uint64_t singer_id, int limit);

    // 兼容同步调试入口，等价于 crawlSingerIndexAndSave。
    bool crawlSingerIndexAndSaveSync(std::string_view tab, int limit);

    // 兼容同步调试入口，等价于 crawlSingerAlbumsAndSave。
    bool crawlSingerAlbumsAndSaveSync(std::uint64_t singer_id, int limit);

    // 兼容同步调试入口，等价于 crawlPendingAlbumDetailsAndSave。
    bool crawlPendingAlbumDetailsAndSaveSync(std::string_view source, int limit);

    // 兼容同步调试入口，等价于 crawlPendingAlbumSongsAndSave。
    bool crawlPendingAlbumSongsAndSaveSync(std::string_view source, int limit);

    /**
     * @brief 启动异步批量爬取任务
     * @param keywords 待爬取的关键词/歌手名列表
     * @param limit 每个关键词的最大抓取数量
     * @return std::string 任务唯一标识 (ID)
     */
    std::string startBatchAsync(const std::vector<std::string> &keywords, int limit);

    // 启动带任务类型和请求快照的异步批量任务。
    std::string startBatchAsync(const std::vector<std::string> &targets, int limit, std::string task_type, std::string request_params);

    // 启动待补齐专辑详情任务，按实际待处理专辑生成批次目标。
    std::string startAlbumDetailsBackfillAsync(std::string source, int limit, std::string request_params);

    // 启动待补齐专辑歌曲任务，按实际待处理专辑生成批次目标。
    std::string startAlbumSongsBackfillAsync(std::string source, int limit, std::string request_params);

    // 从 singer_source_refs 认领待同步歌手，并启动歌曲列表采集任务。
    std::string startSingerSongsFromDbAsync(application::model::SingerSourceSyncQuery query, int limit_per_singer, std::string request_params);

    // 预览从 singer_source_refs 启动歌曲列表采集会命中的歌手来源；不写库。
    std::optional<application::model::SingerSourceSyncPreviewResult> previewSingerSongsFromDb(application::model::SingerSourceSyncQuery query) const;

    // 从 singer_source_refs 认领待同步歌手，并启动专辑列表采集任务。
    std::string startSingerAlbumsFromDbAsync(application::model::SingerSourceSyncQuery query, int limit_per_singer, std::string request_params);

    // 预览从 singer_source_refs 启动专辑列表采集会命中的歌手来源；不写库。
    std::optional<application::model::SingerSourceSyncPreviewResult> previewSingerAlbumsFromDb(application::model::SingerSourceSyncQuery query) const;

    // 请求取消批量任务；已开始的单个爬取会在当前协程步骤结束后协作退出。
    bool cancelBatch(const std::string &batch_id);

    /**
     * @brief 获取任务进度快照
     */
    std::optional<application::model::BatchProgressSnapshot> getBatchProgress(const std::string &batch_id);

    // 查询持久化批次分页。
    application::model::CrawlBatchPage listBatches(application::model::CrawlBatchQuery query) const;

    // 查询持久化批次详情。
    std::optional<application::model::CrawlBatchRecord> findBatch(std::string batch_id) const;

    // 查询持久化批次目标分页。
    application::model::CrawlBatchItemPage listBatchItems(std::string batch_id, application::model::CrawlBatchItemQuery query) const;

    // 查询持久化批次事件分页。
    application::model::CrawlBatchEventPage listBatchEvents(std::string batch_id, application::model::CrawlBatchEventQuery query) const;

    // 查询批次全局摘要。
    application::model::CrawlBatchSummary batchSummary(int stale_after_seconds) const;

    // 从旧批次失败目标创建新批次。
    application::model::CrawlBatchGovernanceResult rerunFailedBatch(std::string batch_id, application::model::CrawlBatchGovernanceRequest request, std::string request_params);

    // 克隆旧批次参数和目标创建新批次。
    application::model::CrawlBatchGovernanceResult cloneBatch(std::string batch_id, application::model::CrawlBatchGovernanceRequest request, std::string request_params);

    // 从批次 item 抽取目标并走补池 retry 认领。
    application::model::CrawlBatchGovernanceResult backfillRetryFromBatch(std::string batch_id, application::model::CrawlBatchGovernanceRequest request, std::string request_params);

    // 归档批次，不删除数据。
    bool archiveBatch(std::string batch_id, std::string reason) const;

    // 查询采集控制台总览。
    application::model::MusicOverview overview() const;

    // 分页查询 Migu 歌手目录。
    application::model::MiguSingerCatalogPage listMiguSingers(application::model::MiguSingerCatalogQuery query) const;

    // 按平台歌手 ID 查询 Migu 歌手详情。
    std::optional<application::model::MiguSingerCatalogRecord> findMiguSinger(std::string source_singer_id) const;

    // 查询 Migu 歌手歌曲。
    application::model::MiguSongCatalogPage listMiguSingerSongs(std::string source_singer_id, application::model::MiguSongCatalogQuery query) const;

    // 查询 Migu 歌手专辑。
    application::model::MiguAlbumCatalogPage listMiguSingerAlbums(std::string source_singer_id, application::model::MiguAlbumCatalogQuery query) const;

    // 分页查询 Migu 专辑目录。
    application::model::MiguAlbumCatalogPage listMiguAlbums(application::model::MiguAlbumCatalogQuery query) const;

    // 按平台专辑 ID 查询 Migu 专辑详情。
    std::optional<application::model::MiguAlbumCatalogRecord> findMiguAlbum(std::string source_album_id) const;

    // 查询 Migu 专辑歌曲。
    application::model::MiguSongCatalogPage listMiguAlbumSongs(std::string source_album_id, application::model::MiguSongCatalogQuery query) const;

    // 分页查询 Migu 歌曲目录。
    application::model::MiguSongCatalogPage listMiguSongs(application::model::MiguSongCatalogQuery query) const;

    // 按平台歌曲 ID 查询 Migu 歌曲详情。
    std::optional<application::model::MiguSongCatalogRecord> findMiguSong(std::string source_song_id) const;

    // 分页查询 Migu 资源队列。
    application::model::MiguResourceQueuePage listMiguResources(application::model::MiguResourceQueueQuery query) const;

    // 按资源任务 ID 查询 Migu 资源队列记录。
    std::optional<application::model::MiguResourceQueueRecord> findMiguResource(std::uint64_t resource_id) const;

    // 预览 Migu 资源治理影响范围；不写库。
    application::model::MiguResourcePreviewResult previewMiguResources(application::model::MiguResourceActionQuery query) const;

    // 将 Migu 资源任务重新置为待获取；不执行资源下载。
    application::model::MiguResourceActionResult retryMiguResources(application::model::MiguResourceActionQuery query) const;

    // 重置 Migu 资源任务；不删除本地文件。
    application::model::MiguResourceActionResult resetMiguResources(application::model::MiguResourceActionQuery query) const;

    // 恢复超时停留在获取中的 Migu 资源任务。
    application::model::MiguResourceActionResult recoverStaleMiguResources(application::model::MiguResourceActionQuery query) const;

    // 认领并处理 Migu 歌曲资源队列，执行 URL 获取、下载、保存和回写。
    application::model::SongResourceProcessResult processMiguResources(application::model::SongResourceClaimQuery query);

    // 异步启动 Migu 歌曲资源处理；返回空字符串表示 workflow 或执行器未配置。
    std::string startMiguResourcesProcessAsync(application::model::SongResourceClaimQuery query);

    // 查询 Migu 元数据一致性摘要。
    application::model::MiguIntegritySummary miguIntegrity(int stale_after_seconds) const;

    // 查询 MySQL schema 版本和关键对象完整性。
    application::model::MiguSchemaStatus miguSchemaStatus() const;

    // 查询治理审计事件。
    application::model::MiguGovernanceEventPage listMiguGovernanceEvents(application::model::MiguGovernanceEventQuery query) const;

    // 重试指定补池，返回批次 ID 和数据库认领统计。
    application::model::MiguBackfillRetryResult retryMiguBackfill(std::string backfill_type, application::model::MiguBackfillTargetQuery query, int limit_per_target, std::string request_params);

    // 重置指定补池，返回更新数量。
    int resetMiguBackfill(std::string backfill_type, application::model::MiguBackfillTargetQuery query) const;

    // 独立刷新库内歌手整份资料，不修改歌曲或专辑目录生命周期。
    application::result::ServiceResult<application::model::MiguBackfillRetryResult> refreshMiguSingerProfiles(application::model::MiguBackfillTargetQuery query, bool force, std::string request_params);

    // 恢复 Migu 元数据和批次中超时停留在 running 的记录。
    application::model::MiguStaleRecoveryResult recoverMiguStale(application::model::MiguStaleRecoveryRequest request) const;

    // 获取当前运行设置快照，供 API 层返回给前端。
    application::model::CrawlerRuntimeSettings getRuntimeSettings() const;
    // 旧与统一队列共用默认设置和单次覆盖的解析规则。
    application::model::SongResourceProcessOptions resourceProcessOptions(const application::model::SongResourceProcessOptionOverrides &overrides) const;

    // 更新支持热变更的运行设置；返回错误信息表示更新失败。
    std::optional<std::string> updateRuntimeSettings(const application::model::CrawlerRuntimeSettingsUpdate &update);

private:
    // 按运行设置执行带重试的目标采集。
    bool searchAndSaveWithRetry(std::string keyword, int limit);

    // 按库内声明总数执行歌手歌曲采集，并用真实关系数量校准结果。
    application::model::SingerCollectionResult crawlSingerSongsWithStoredTotals(std::uint64_t singer_id, int limit);

    // 按库内声明总数执行歌手专辑采集，并用真实关系数量校准结果。
    application::model::SingerCollectionResult crawlSingerAlbumsWithStoredTotals(std::uint64_t singer_id, int limit);

    // 创建通用批次目标记录。
    std::vector<application::model::CrawlBatchItemRecord> makeTargetItems(const std::vector<std::string> &targets, std::string item_type) const;

    // 启动指定目标记录的批次。
    std::string startBatchItemsAsync(
        std::vector<application::model::CrawlBatchItemRecord> items,
        int limit,
        std::string task_type,
        std::string request_params,
        BatchTaskManager::TargetHandler handler,
        std::string parent_batch_id = {},
        bool allow_empty = false, // 仅已校验的数据库队列允许零目标正常结束
        int max_retries = 0 // 由具体用例显式启用有限重试
    );

    // 按显式或查询结果启动专辑详情补齐批次。
    std::string startAlbumDetailsBackfillItemsAsync(std::vector<application::model::AlbumSourceMetadata> albums, int limit, std::string request_params);

    // 按显式或查询结果启动专辑歌曲补齐批次。
    std::string startAlbumSongsBackfillItemsAsync(std::vector<application::model::AlbumSourceMetadata> albums, int limit, std::string request_params);

    // 当前运行设置 JSON 快照。
    std::string runtimeSettingsJson() const;

    // 读取当前批量并发设置。
    int maxBatchConcurrency() const;

    // 读取当前分页并发设置。
    int maxPageConcurrency() const;

    // 读取当前重试次数设置。
    int maxRetries() const;

    // 读取当前任务超时设置。
    std::chrono::milliseconds taskTimeout() const;

    TargetDispatcher target_dispatcher_;                                                  // 目标分发器
    std::shared_ptr<MetadataPersistenceService> persistence_;                             // 持久化服务
    std::shared_ptr<MusicCollectionWorkflow> workflow_;                                   // 采集编排服务
    std::shared_ptr<SongResourceDownloadWorkflow> song_resource_workflow_;                // 歌曲资源下载编排服务，异步任务需共享生命周期
    std::shared_ptr<AsyncSongResourceDownloadWorkflow> async_song_resource_workflow_;     // 真异步资源下载编排服务
    std::shared_ptr<application::ports::AsyncRuntime> async_runtime_;                     // 异步资源管线 runtime
    TaskRunner resource_process_runner_;                                                  // 资源异步处理执行器
    std::shared_ptr<application::ports::SingerSourceSyncQueue> singer_source_sync_queue_; // 歌手来源同步队列
    std::shared_ptr<application::ports::BatchTaskRepository> batch_task_repository_;      // 批次持久化 port
    std::unique_ptr<BatchTaskManager> batch_task_manager_;                                // 批量任务服务
    std::unique_ptr<CrawlerBatchQueryService> batch_query_service_;                       // 批次查询服务
    std::unique_ptr<MusicCatalogQueryService> catalog_query_service_;                     // 音乐目录查询服务
    bool singer_profile_refresh_configured_ = false;                                      // 装配时确认资料请求、解析、查询和原子写入均可用
    std::shared_ptr<application::ports::DiagnosticsSink> diagnostics_;                    // 诊断输出 port

    // 运行设置由 mutex 保护，允许前端在任务之间调整并发、重试和超时。
    mutable std::mutex settings_mutex_;
    application::model::CrawlerRuntimeSettings runtime_settings_;
};

} // namespace orchestrator::modules::crawler::application::use_cases
