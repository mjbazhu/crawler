#pragma once

#include "crawler/application/ports/diagnostics/DiagnosticsSink.h"
#include "crawler/application/ports/execution/TaskExecutor.h"
#include "crawler/application/ports/parsing/AlbumSourceParser.h"
#include "crawler/application/ports/parsing/PageInspector.h"
#include "crawler/application/ports/parsing/ParseResultParser.h"
#include "crawler/application/ports/parsing/SingerProfileParser.h"
#include "crawler/application/ports/parsing/SingerSourceParser.h"
#include "crawler/application/ports/parsing/SongSourceParser.h"
#include "crawler/application/ports/persistence/AlbumSourceSyncQueue.h"
#include "crawler/application/ports/provider/AlbumDetailProvider.h"
#include "crawler/application/ports/provider/AlbumSearchProvider.h"
#include "crawler/application/ports/provider/RawPageFetcher.h"
#include "crawler/application/ports/provider/SingerCatalogProvider.h"
#include "crawler/application/model/SingerSourceProfile.h"
#include "crawler/application/ports/provider/SongSearchProvider.h"
#include "crawler/application/use_cases/AlbumDetailBackfillWorkflow.h"
#include "crawler/application/use_cases/MetadataPersistenceService.h"
#include "crawler/application/use_cases/SearchCollectionWorkflow.h"
#include "crawler/application/use_cases/SingerCatalogWorkflow.h"
#include "crawler/application/use_cases/UrlIngestionWorkflow.h"

#include "crawler/application/result/ServiceResult.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace orchestrator::modules::crawler::application::use_cases {

// 音乐采集兼容门面，只负责把旧入口委托给专门 workflow。
class MusicCollectionWorkflow {
public:
    // 生产装配直接注入已有流程，兼容门面不增加采集策略或状态。
    MusicCollectionWorkflow(std::shared_ptr<UrlIngestionWorkflow> url, std::shared_ptr<SearchCollectionWorkflow> search, std::shared_ptr<SingerCatalogWorkflow> singer, std::shared_ptr<AlbumDetailBackfillWorkflow> album);
    // 保存窄 provider/parser port 并装配专门 workflow。
    MusicCollectionWorkflow(
        std::shared_ptr<application::ports::RawPageFetcher> raw_page_fetcher,
        std::shared_ptr<application::ports::SongSearchProvider> song_search_provider,
        std::shared_ptr<application::ports::AlbumSearchProvider> album_search_provider,
        std::shared_ptr<application::ports::SingerCatalogProvider> singer_catalog_provider,
        std::shared_ptr<application::ports::AlbumDetailProvider> album_detail_provider,
        std::shared_ptr<application::ports::ParseResultParser> parse_result_parser,
        std::shared_ptr<application::ports::SongSourceParser> song_source_parser,
        std::shared_ptr<application::ports::SingerSourceParser> singer_source_parser,
        std::shared_ptr<application::ports::AlbumSourceParser> album_source_parser,
        std::shared_ptr<application::ports::SingerProfileParser> singer_profile_parser,
        std::shared_ptr<application::ports::PageInspector> page_inspector,
        std::shared_ptr<application::ports::AlbumSourceSyncQueue> album_source_sync_queue,
        std::shared_ptr<MetadataPersistenceService> persistence,
        std::shared_ptr<application::ports::TaskExecutor> parse_executor,
        std::shared_ptr<application::ports::TaskExecutor> page_executor,
        std::shared_ptr<application::ports::DiagnosticsSink> diagnostics
    );

    // 执行 URL 原始响应采集、解析和保存。
    bool processUrl(std::string url, int limit);

    // 执行普通关键词搜索并保存歌曲和专辑。
    bool searchAndSave(std::string keyword, int limit, int max_page_concurrency);

    // 获取指定歌手歌曲分页并保存歌曲来源参数。
    bool crawlSingerSongsAndSave(std::uint64_t singer_id, int limit, int max_page_concurrency);

    // 获取指定歌手歌曲分页并返回写入数量。
    application::model::SingerCollectionResult crawlSingerSongsAndSaveResult(std::uint64_t singer_id, int limit, int max_page_concurrency, std::optional<int> source_song_total = std::nullopt, std::optional<int> source_album_total = std::nullopt, bool refresh_profile = false);

    // 获取歌手分类列表并保存歌手来源参数。
    bool crawlSingerIndexAndSave(std::string tab, int limit);

    // 获取歌手分类列表并返回实际解析保存数量。
    application::model::SingerCollectionResult crawlSingerIndexAndSaveResult(std::string tab, int limit);

    // 获取指定歌手专辑分页并保存专辑来源参数。
    bool crawlSingerAlbumsAndSave(std::uint64_t singer_id, int limit, int max_page_concurrency);

    // 获取指定歌手专辑分页并返回写入数量。
    application::model::SingerCollectionResult crawlSingerAlbumsAndSaveResult(std::uint64_t singer_id, int limit, int max_page_concurrency, std::optional<int> source_song_total = std::nullopt, std::optional<int> source_album_total = std::nullopt, bool refresh_profile = false);

    // 刷新并保存歌手主要信息中的平台声明歌曲/专辑总数。
    application::result::ServiceResult<application::model::SingerSourceProfile> fetchAndSaveSingerSourceProfile(std::uint64_t singer_id);

    // 获取待补齐专辑详情并保存详情与歌曲来源参数。
    bool crawlPendingAlbumDetailsAndSave(std::string source, int limit, int max_page_concurrency);

    // 获取待补齐专辑歌曲并保存歌曲来源参数。
    bool crawlPendingAlbumSongsAndSave(std::string source, int limit, int max_page_concurrency);

    // 查询待补齐详情专辑。
    std::vector<application::model::AlbumSourceMetadata> pendingAlbumDetails(std::string source, int limit);

    // 查询待补齐歌曲专辑。
    std::vector<application::model::AlbumSourceMetadata> pendingAlbumSongs(std::string source, int limit);

    // 补齐单个专辑详情。
    bool crawlAlbumDetailAndSave(application::model::AlbumSourceMetadata source_metadata);

    // 补齐单个专辑歌曲。
    bool crawlAlbumSongsAndSave(application::model::AlbumSourceMetadata source_metadata);

private:
    std::shared_ptr<UrlIngestionWorkflow> url_workflow_;                 // URL 采集 workflow
    std::shared_ptr<SearchCollectionWorkflow> search_workflow_;          // 关键词搜索 workflow
    std::shared_ptr<SingerCatalogWorkflow> singer_workflow_;             // 歌手目录 workflow
    std::shared_ptr<AlbumDetailBackfillWorkflow> album_detail_workflow_; // 专辑详情补齐 workflow
};

} // namespace orchestrator::modules::crawler::application::use_cases
