#pragma once

#include "crawler/application/ports/diagnostics/DiagnosticsSink.h"
#include "crawler/application/ports/execution/TaskExecutor.h"
#include "crawler/application/model/SingerCollectionResult.h"
#include "crawler/application/ports/provider/SingerCatalogProvider.h"
#include "crawler/application/use_cases/MetadataPersistenceService.h"
#include "crawler/application/use_cases/ParserDispatch.h"
#include "crawler/application/use_cases/TaskRunner.h"

#include <cstdint>
#include <memory>
#include <string>

namespace orchestrator::modules::crawler::application::use_cases {

// 歌手目录 workflow，负责歌手歌曲、歌手分类和歌手专辑分页采集。
class SingerCatalogWorkflow {
public:
    // 保存歌手目录 provider、解析调度器、持久化服务和分页执行器。
    SingerCatalogWorkflow(
        std::shared_ptr<application::ports::SingerCatalogProvider> singer_catalog_provider,
        std::shared_ptr<ParserDispatch> parser,
        std::shared_ptr<MetadataPersistenceService> persistence,
        std::shared_ptr<application::ports::TaskExecutor> page_executor,
        std::shared_ptr<application::ports::DiagnosticsSink> diagnostics
    );

    // 获取指定歌手歌曲分页并保存歌曲来源参数。
    bool crawlSingerSongsAndSave(std::uint64_t singer_id, int limit, int max_page_concurrency);

    // 获取指定歌手歌曲分页并返回写入数量，供同步队列更新状态。
    application::model::SingerCollectionResult crawlSingerSongsAndSaveResult(std::uint64_t singer_id, int limit, int max_page_concurrency);

    // 获取歌手分类列表并保存歌手来源参数。
    bool crawlSingerIndexAndSave(std::string tab, int limit);

    // 获取歌手分类列表并返回实际解析保存数量，供批次 item 展示。
    application::model::SingerCollectionResult crawlSingerIndexAndSaveResult(std::string tab, int limit);

    // 获取指定歌手专辑分页并保存专辑来源参数。
    bool crawlSingerAlbumsAndSave(std::uint64_t singer_id, int limit, int max_page_concurrency);

    // 获取指定歌手专辑分页并返回写入数量，供同步队列更新状态。
    application::model::SingerCollectionResult crawlSingerAlbumsAndSaveResult(std::uint64_t singer_id, int limit, int max_page_concurrency);

private:
    std::shared_ptr<application::ports::SingerCatalogProvider> singer_catalog_provider_; // 歌手目录 port
    std::shared_ptr<ParserDispatch> parser_;                                             // 解析调度器
    std::shared_ptr<MetadataPersistenceService> persistence_;                             // 持久化服务
    TaskRunner page_runner_;                                                              // 分页执行器
};

} // namespace orchestrator::modules::crawler::application::use_cases
