#pragma once

#include "crawler/application/model/CrawlBatch.h"

#include <optional>
#include <string>
#include <vector>

namespace orchestrator::modules::crawler::application::ports {

// 批次任务持久化 port，负责把任务生命周期落库并提供前端查询。
class BatchTaskRepository {
public:
    virtual ~BatchTaskRepository() = default;

    // 创建批次主记录和目标记录。
    virtual bool createBatch(
        const application::model::CrawlBatchRecord &batch,
        const std::vector<application::model::CrawlBatchItemRecord> &items
    ) = 0;

    // 更新批次运行态统计。
    virtual bool updateBatchProgress(const application::model::CrawlBatchRecord &batch) = 0;

    // 更新单个目标状态。
    virtual bool updateItem(const application::model::CrawlBatchItemRecord &item) = 0;

    // 新增或更新单个目标状态，用于同步流程中动态发现的目标。
    virtual bool upsertItem(const application::model::CrawlBatchItemRecord &item) = 0;

    // 追加批次事件。
    virtual bool appendEvent(const application::model::CrawlBatchEventRecord &event) = 0;

    // 查询批次分页。
    virtual application::model::CrawlBatchPage listBatches(const application::model::CrawlBatchQuery &query) = 0;

    // 查询批次详情。
    virtual std::optional<application::model::CrawlBatchRecord> findBatch(std::string batch_id) = 0;

    // 查询批次目标分页。
    virtual application::model::CrawlBatchItemPage listItems(std::string batch_id, const application::model::CrawlBatchItemQuery &query) = 0;

    // 查询批次事件分页。
    virtual application::model::CrawlBatchEventPage listEvents(std::string batch_id, const application::model::CrawlBatchEventQuery &query) = 0;

    // 查询批次全局摘要。
    virtual application::model::CrawlBatchSummary summary(std::string source, int stale_after_seconds) = 0;

    // 归档批次，返回是否更新成功。
    virtual bool archiveBatch(std::string batch_id, std::string reason) = 0;

    // 恢复超时停留在 Running 的批次和未完成目标，返回恢复批次数量。
    virtual int recoverStaleRunningBatches(std::string source, int stale_after_seconds, std::string error) = 0;
};

} // namespace orchestrator::modules::crawler::application::ports
