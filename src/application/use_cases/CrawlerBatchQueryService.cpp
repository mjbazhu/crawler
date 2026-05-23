#include "crawler/application/use_cases/CrawlerBatchQueryService.h"

#include <algorithm>
#include <utility>

namespace orchestrator::modules::crawler::application::use_cases {

namespace {

// 规整分页参数，防止前端提交过大 page_size。
void normalizeBatchQuery(application::model::CrawlBatchQuery &query)
{
    query.page = (std::max)(1, query.page);
    query.page_size = (std::min)((std::max)(1, query.page_size), 100);
}

// 规整目标分页参数。
void normalizeItemQuery(application::model::CrawlBatchItemQuery &query)
{
    query.page = (std::max)(1, query.page);
    query.page_size = (std::min)((std::max)(1, query.page_size), 200);
}

// 规整事件分页参数。
void normalizeEventQuery(application::model::CrawlBatchEventQuery &query)
{
    query.page = (std::max)(1, query.page);
    query.page_size = (std::min)((std::max)(1, query.page_size), 500);
}

} // namespace

CrawlerBatchQueryService::CrawlerBatchQueryService(std::shared_ptr<application::ports::BatchTaskRepository> repository) : repository_(std::move(repository))
{}

application::model::CrawlBatchPage CrawlerBatchQueryService::listBatches(application::model::CrawlBatchQuery query) const
{
    normalizeBatchQuery(query);
    if (!repository_) {
        return application::model::CrawlBatchPage {{}, query.page, query.page_size, 0};
    }
    return repository_->listBatches(query);
}

std::optional<application::model::CrawlBatchRecord> CrawlerBatchQueryService::findBatch(std::string batch_id) const
{
    if (!repository_ || batch_id.empty()) {
        return std::nullopt;
    }
    return repository_->findBatch(std::move(batch_id));
}

application::model::CrawlBatchItemPage CrawlerBatchQueryService::listItems(std::string batch_id, application::model::CrawlBatchItemQuery query) const
{
    normalizeItemQuery(query);
    if (!repository_ || batch_id.empty()) {
        return application::model::CrawlBatchItemPage {{}, query.page, query.page_size, 0};
    }
    return repository_->listItems(std::move(batch_id), query);
}

application::model::CrawlBatchEventPage CrawlerBatchQueryService::listEvents(std::string batch_id, application::model::CrawlBatchEventQuery query) const
{
    normalizeEventQuery(query);
    if (!repository_ || batch_id.empty()) {
        return application::model::CrawlBatchEventPage {{}, query.page, query.page_size, 0};
    }
    return repository_->listEvents(std::move(batch_id), query);
}

application::model::CrawlBatchSummary CrawlerBatchQueryService::summary(std::string source, int stale_after_seconds) const
{
    if (!repository_) {
        return {};
    }
    if (source.empty()) {
        source = "migu";
    }
    stale_after_seconds = (std::max)(60, stale_after_seconds);
    return repository_->summary(std::move(source), stale_after_seconds);
}

bool CrawlerBatchQueryService::archiveBatch(std::string batch_id, std::string reason) const
{
    if (!repository_ || batch_id.empty()) {
        return false;
    }
    return repository_->archiveBatch(std::move(batch_id), std::move(reason));
}

int CrawlerBatchQueryService::recoverStaleRunningBatches(std::string source, int stale_after_seconds, std::string error) const
{
    if (!repository_ || stale_after_seconds <= 0) {
        return 0;
    }
    if (source.empty()) {
        source = "migu";
    }
    return repository_->recoverStaleRunningBatches(std::move(source), stale_after_seconds, std::move(error));
}

} // namespace orchestrator::modules::crawler::application::use_cases
