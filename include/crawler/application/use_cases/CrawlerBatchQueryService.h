#pragma once

#include "crawler/application/model/CrawlBatch.h"
#include "crawler/application/ports/persistence/BatchTaskRepository.h"

#include <memory>
#include <optional>
#include <string>

namespace orchestrator::modules::crawler::application::use_cases {

// 批次查询 use case，只负责整理查询参数并委托持久化 port。
class CrawlerBatchQueryService {
public:
    // 保存批次查询仓储。
    explicit CrawlerBatchQueryService(std::shared_ptr<application::ports::BatchTaskRepository> repository);

    // 查询批次分页。
    application::model::CrawlBatchPage listBatches(application::model::CrawlBatchQuery query) const;

    // 查询批次详情。
    std::optional<application::model::CrawlBatchRecord> findBatch(std::string batch_id) const;

    // 查询批次目标分页。
    application::model::CrawlBatchItemPage listItems(std::string batch_id, application::model::CrawlBatchItemQuery query) const;

    // 查询批次事件分页。
    application::model::CrawlBatchEventPage listEvents(std::string batch_id, application::model::CrawlBatchEventQuery query) const;

    // 查询批次全局摘要。
    application::model::CrawlBatchSummary summary(std::string source, int stale_after_seconds) const;

    // 归档批次。
    bool archiveBatch(std::string batch_id, std::string reason) const;

    // 恢复超时停留在 Running 的批次。
    int recoverStaleRunningBatches(std::string source, int stale_after_seconds, std::string error) const;

private:
    std::shared_ptr<application::ports::BatchTaskRepository> repository_; // 批次持久化 port
};

} // namespace orchestrator::modules::crawler::application::use_cases
