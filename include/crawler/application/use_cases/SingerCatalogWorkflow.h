#pragma once

#include "crawler/application/ports/diagnostics/DiagnosticsSink.h"
#include "crawler/application/ports/execution/TaskExecutor.h"
#include "crawler/application/model/SingerCollectionResult.h"
#include "crawler/application/model/SingerSourceProfile.h"
#include "crawler/application/ports/provider/SingerCatalogProvider.h"
#include "crawler/application/use_cases/MetadataPersistenceService.h"
#include "crawler/application/use_cases/ParserDispatch.h"
#include "crawler/application/use_cases/TaskRunner.h"
#include "crawler/application/use_cases/CatalogMetadataCompletion.h"

#include "crawler/application/result/ServiceResult.h"

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace orchestrator::modules::crawler::application::use_cases {

// 新装配按目录职责注入依赖，兼容构造函数不承担完整资料采集。
struct SingerCatalogWorkflowDependencies {
    std::shared_ptr<application::ports::SingerCatalogProvider> provider; // 歌手目录
    std::shared_ptr<ParserDispatch> parser;                             // 响应解析
    std::shared_ptr<MetadataPersistenceService> persistence;           // 原子写入
    std::shared_ptr<application::ports::TaskExecutor> pages;            // 有界分页执行
    std::shared_ptr<application::ports::DiagnosticsSink> diagnostics;   // 结构化诊断
    std::shared_ptr<CatalogMetadataCompletion> completion;             // 目录内整份资料补齐
};

// 歌手目录 workflow，负责歌手歌曲、歌手分类和歌手专辑分页采集。
class SingerCatalogWorkflow {
public:
    // 生产装配通过依赖组启用统一元数据补齐。
    explicit SingerCatalogWorkflow(SingerCatalogWorkflowDependencies dependencies);
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
    application::model::SingerCollectionResult crawlSingerSongsAndSaveResult(std::uint64_t singer_id, int limit, int max_page_concurrency, std::optional<int> source_song_total = std::nullopt, std::optional<int> source_album_total = std::nullopt, bool refresh_profile = false);

    // 获取歌手分类列表并保存歌手来源参数。
    bool crawlSingerIndexAndSave(std::string tab, int limit);

    // 获取歌手分类列表并返回实际解析保存数量，供批次 item 展示。
    application::model::SingerCollectionResult crawlSingerIndexAndSaveResult(std::string tab, int limit);

    // 获取指定歌手专辑分页并保存专辑来源参数。
    bool crawlSingerAlbumsAndSave(std::uint64_t singer_id, int limit, int max_page_concurrency);

    // 获取指定歌手专辑分页并返回写入数量，供同步队列更新状态。
    application::model::SingerCollectionResult crawlSingerAlbumsAndSaveResult(std::uint64_t singer_id, int limit, int max_page_concurrency, std::optional<int> source_song_total = std::nullopt, std::optional<int> source_album_total = std::nullopt, bool refresh_profile = false);

    // 独立刷新整份歌手资料并原子保存；失败保留请求、身份或持久化错误。
    application::result::ServiceResult<application::model::SingerSourceProfile> fetchAndSaveSingerSourceProfile(std::uint64_t singer_id);

private:
    std::shared_ptr<application::ports::SingerCatalogProvider> singer_catalog_provider_; // 歌手目录 port
    std::shared_ptr<ParserDispatch> parser_;                                             // 解析调度器
    std::shared_ptr<MetadataPersistenceService> persistence_;                             // 持久化服务
    TaskRunner page_runner_;                                                              // 分页执行器
    std::shared_ptr<CatalogMetadataCompletion> completion_;                                // 可选目录资料补齐
};

} // namespace orchestrator::modules::crawler::application::use_cases
