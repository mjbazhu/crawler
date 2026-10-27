#include "crawler/application/use_cases/SingerCatalogWorkflow.h"
#include "SingerCatalogPagingSupport.h"

namespace orchestrator::modules::crawler::application::use_cases {
using namespace singer_detail; // 歌手目录入口共享的分页与完成度规则

SingerCatalogWorkflow::SingerCatalogWorkflow(
    std::shared_ptr<application::ports::SingerCatalogProvider> singer_catalog_provider,
    std::shared_ptr<ParserDispatch> parser,
    std::shared_ptr<MetadataPersistenceService> persistence,
    std::shared_ptr<application::ports::TaskExecutor> page_executor,
    std::shared_ptr<application::ports::DiagnosticsSink> diagnostics
) : singer_catalog_provider_(std::move(singer_catalog_provider)),
    parser_(std::move(parser)),
    persistence_(std::move(persistence)),
    page_runner_(std::move(page_executor), std::move(diagnostics))
{}

SingerCatalogWorkflow::SingerCatalogWorkflow(SingerCatalogWorkflowDependencies dependencies)
    : SingerCatalogWorkflow(std::move(dependencies.provider), std::move(dependencies.parser), std::move(dependencies.persistence), std::move(dependencies.pages), std::move(dependencies.diagnostics))
{
    completion_ = std::move(dependencies.completion);
}

bool SingerCatalogWorkflow::crawlSingerIndexAndSave(std::string tab, int limit)
{
    const auto result = crawlSingerIndexAndSaveResult(std::move(tab), limit);
    return result.success && result.written_count > 0;
}

application::model::SingerCollectionResult SingerCatalogWorkflow::crawlSingerIndexAndSaveResult(std::string tab, int limit)
{
    if (!singer_catalog_provider_ || !parser_ || !persistence_ || tab.empty()) {
        return {false, false, 0, "singer index workflow is not configured"};
    }

    auto response = singer_catalog_provider_->fetchSingerIndexPage(tab);
    if (!response) {
        return {false, false, 0, "failed to fetch singer index page"};
    }

    auto singer_sources = parser_->parseSingerSources(std::move(response->body), tab);
    const int singer_index_limit = limit < 0 ? kWorkflowDefaultLimit : limit;
    const int unique_count = singerIndexUniqueCount(singer_sources, singer_index_limit);
    if (unique_count == 0) {
        return {false, false, 0, "singer index page did not contain valid singer sources"};
    }

    const bool saved = persistence_->saveSingerSources(singer_sources, singer_index_limit);
    if (!saved) {
        return {false, false, unique_count, "failed to save singer sources"};
    }
    return {true, false, unique_count, {}};
}

application::model::SingerSourceProfile SingerCatalogWorkflow::fetchAndSaveSingerSourceProfile(std::uint64_t singer_id)
{
    application::model::SingerSourceProfile profile;
    profile.source = "migu";
    profile.source_singer_id = std::to_string(singer_id);

    if (!singer_catalog_provider_ || !parser_) {
        return profile;
    }

    auto response = singer_catalog_provider_->fetchSingerInfo(singer_id);
    if (!response) {
        return profile;
    }

    profile = parser_->parseSingerSourceProfile(std::move(response->body));
    if (profile.source.empty()) {
        profile.source = "migu";
    }
    if (profile.source_singer_id.empty()) {
        profile.source_singer_id = std::to_string(singer_id);
    }
    if (persistence_ && (hasSingerSourceProfileCounts(profile) || profile.metadata)) {
        if (!persistence_->saveSingerSourceProfile(profile)) {
            profile.source_song_total.reset();
            profile.source_album_total.reset();
        }
    }
    return profile;
}

} // namespace orchestrator::modules::crawler::application::use_cases
