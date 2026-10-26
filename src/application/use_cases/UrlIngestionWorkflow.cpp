#include "crawler/application/use_cases/UrlIngestionWorkflow.h"

#include "crawler/application/use_cases/WorkflowPaging.h"

#include <utility>

namespace orchestrator::modules::crawler::application::use_cases {

UrlIngestionWorkflow::UrlIngestionWorkflow(
    std::shared_ptr<application::ports::RawPageFetcher> raw_page_fetcher,
    std::shared_ptr<ParserDispatch> parser,
    std::shared_ptr<MetadataPersistenceService> persistence
) : raw_page_fetcher_(std::move(raw_page_fetcher)),
    parser_(std::move(parser)),
    persistence_(std::move(persistence))
{}

bool UrlIngestionWorkflow::processUrl(std::string url, int limit)
{
    if (!raw_page_fetcher_ || !parser_ || !persistence_) {
        return false;
    }

    auto response = raw_page_fetcher_->fetchUrl(std::move(url));
    if (!response) {
        return false;
    }

    std::vector<char> body = std::move(response->body);
    auto song_sources = parser_->parseSongSources(body);
    if (persistence_->hasAtomicWriter()) {
        application::model::MetadataWriteBatch batch;
        batch.songs = std::move(song_sources);
        batch.albums = parser_->parseAlbumSources(body);
        batch.singers = parser_->parseSingerSources(body, {});
        return persistence_->saveSources(std::move(batch), normalizeWorkflowLimit(limit));
    }
    auto results = parser_->parse(body);

    bool saved = false;
    saved = persistence_->saveSongSources(song_sources, normalizeWorkflowLimit(limit)) || saved;
    saved = persistence_->saveParseResults(results, normalizeWorkflowLimit(limit)) || saved;
    return saved;
}

} // namespace orchestrator::modules::crawler::application::use_cases
