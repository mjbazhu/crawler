#pragma once

#include "crawler/application/ports/persistence/BatchTaskRepository.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlConnectionPool.h"

#include <boost/asio/awaitable.hpp>
#include <boost/mysql/row_view.hpp>

#include <memory>
#include <optional>
#include <string>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {

// MySQL 批次任务仓储，负责批次、目标和事件的持久化与查询。
class MySqlBatchTaskRepository : public application::ports::BatchTaskRepository {
public:
    // 保存线程安全 MySQL 连接池。
    explicit MySqlBatchTaskRepository(std::shared_ptr<MySqlConnectionPool> pool);

    bool createBatch(const application::model::CrawlBatchRecord &batch, const std::vector<application::model::CrawlBatchItemRecord> &items) override;
    bool updateBatchProgress(const application::model::CrawlBatchRecord &batch) override;
    bool updateItem(const application::model::CrawlBatchItemRecord &item) override;
    bool upsertItem(const application::model::CrawlBatchItemRecord &item) override;
    bool appendEvent(const application::model::CrawlBatchEventRecord &event) override;
    application::model::CrawlBatchPage listBatches(const application::model::CrawlBatchQuery &query) override;
    std::optional<application::model::CrawlBatchRecord> findBatch(std::string batch_id) override;
    application::model::CrawlBatchItemPage listItems(std::string batch_id, const application::model::CrawlBatchItemQuery &query) override;
    application::model::CrawlBatchEventPage listEvents(std::string batch_id, const application::model::CrawlBatchEventQuery &query) override;
    application::model::CrawlBatchSummary summary(std::string source, int stale_after_seconds) override;
    bool archiveBatch(std::string batch_id, std::string reason) override;
    int recoverStaleRunningBatches(std::string source, int stale_after_seconds, std::string error) override;

private:
    boost::asio::awaitable<bool> coCreateBatch(application::model::CrawlBatchRecord batch, std::vector<application::model::CrawlBatchItemRecord> items);
    boost::asio::awaitable<bool> coUpdateBatchProgress(application::model::CrawlBatchRecord batch);
    boost::asio::awaitable<bool> coUpdateItem(application::model::CrawlBatchItemRecord item);
    boost::asio::awaitable<bool> coUpsertItem(application::model::CrawlBatchItemRecord item);
    boost::asio::awaitable<bool> coAppendEvent(application::model::CrawlBatchEventRecord event);
    boost::asio::awaitable<application::model::CrawlBatchPage> coListBatches(application::model::CrawlBatchQuery query);
    boost::asio::awaitable<std::optional<application::model::CrawlBatchRecord>> coFindBatch(std::string batch_id);
    boost::asio::awaitable<application::model::CrawlBatchItemPage> coListItems(std::string batch_id, application::model::CrawlBatchItemQuery query);
    boost::asio::awaitable<application::model::CrawlBatchEventPage> coListEvents(std::string batch_id, application::model::CrawlBatchEventQuery query);
    boost::asio::awaitable<application::model::CrawlBatchSummary> coSummary(std::string source, int stale_after_seconds);
    boost::asio::awaitable<bool> coArchiveBatch(std::string batch_id, std::string reason);
    boost::asio::awaitable<int> coRecoverStaleRunningBatches(std::string source, int stale_after_seconds, std::string error);

    static application::model::CrawlBatchRecord mapBatch(boost::mysql::row_view row);
    static application::model::CrawlBatchItemRecord mapItem(boost::mysql::row_view row);
    static application::model::CrawlBatchEventRecord mapEvent(boost::mysql::row_view row);

    std::shared_ptr<MySqlConnectionPool> pool_; // MySQL 连接池
};

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
