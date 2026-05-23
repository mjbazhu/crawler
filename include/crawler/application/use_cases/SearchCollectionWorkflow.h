#pragma once

#include "crawler/application/ports/diagnostics/DiagnosticsSink.h"
#include "crawler/application/ports/execution/TaskExecutor.h"
#include "crawler/application/ports/provider/AlbumDetailProvider.h"
#include "crawler/application/ports/provider/AlbumSearchProvider.h"
#include "crawler/application/ports/provider/SongSearchProvider.h"
#include "crawler/application/use_cases/MetadataPersistenceService.h"
#include "crawler/application/use_cases/ParserDispatch.h"
#include "crawler/application/use_cases/TaskRunner.h"
#include "crawler/domain/music/model/Album.h"
#include "crawler/domain/music/model/Song.h"

#include <memory>
#include <string>
#include <vector>

namespace orchestrator::modules::crawler::application::use_cases {

// 关键词搜索 workflow，负责歌曲/专辑分页搜索和专辑歌曲补齐。
class SearchCollectionWorkflow {
public:
    // 保存搜索 provider、解析调度器、持久化服务和分页执行器。
    SearchCollectionWorkflow(
        std::shared_ptr<application::ports::SongSearchProvider> song_search_provider,
        std::shared_ptr<application::ports::AlbumSearchProvider> album_search_provider,
        std::shared_ptr<application::ports::AlbumDetailProvider> album_detail_provider,
        std::shared_ptr<ParserDispatch> parser,
        std::shared_ptr<MetadataPersistenceService> persistence,
        std::shared_ptr<application::ports::TaskExecutor> page_executor,
        std::shared_ptr<application::ports::DiagnosticsSink> diagnostics
    );

    // 执行普通关键词搜索并保存歌曲和专辑。
    bool searchAndSave(std::string keyword, int limit, int max_page_concurrency);

private:
    // 获取单页歌曲搜索结果。
    std::vector<domain::Song> collectSongPage(std::string keyword, int page);

    // 获取单页专辑搜索结果。
    std::vector<domain::Album> collectAlbumPage(std::string keyword, int page);

    // 分页获取歌曲搜索结果。
    domain::SongList collectSongs(std::string keyword, int limit, int max_page_concurrency);

    // 获取专辑歌曲。
    std::vector<domain::Song> collectAlbumSongs(const domain::Album &album);

    // 分页获取专辑搜索结果并补齐专辑歌曲。
    domain::AlbumList collectAlbums(std::string keyword, int limit, int max_page_concurrency);

    std::shared_ptr<application::ports::SongSearchProvider> song_search_provider_;   // 歌曲搜索 port
    std::shared_ptr<application::ports::AlbumSearchProvider> album_search_provider_; // 专辑搜索 port
    std::shared_ptr<application::ports::AlbumDetailProvider> album_detail_provider_; // 专辑详情 port
    std::shared_ptr<ParserDispatch> parser_;                                         // 解析调度器
    std::shared_ptr<MetadataPersistenceService> persistence_;                         // 持久化服务
    TaskRunner page_runner_;                                                          // 分页执行器
};

} // namespace orchestrator::modules::crawler::application::use_cases
