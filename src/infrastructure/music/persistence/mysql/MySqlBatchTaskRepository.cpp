#include "crawler/infrastructure/music/persistence/mysql/MySqlBatchTaskRepository.h"

#include "crawler/infrastructure/music/persistence/mysql/MySqlValueReader.h"
#include "crawler/infrastructure/music/persistence/sql/SqlFileLoader.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlWriteRunner.h"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/mysql/error_code.hpp>
#include <boost/mysql/results.hpp>
#include <boost/mysql/statement.hpp>

#include <algorithm>
#include <ctime>
#include <exception>
#include <future>
#include <iostream>
#include <optional>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {

namespace {

template <typename T>
T waitForResult(boost::asio::any_io_executor executor, boost::asio::awaitable<T> task, std::string_view context, T fallback)
{
    auto future = boost::asio::co_spawn(std::move(executor), std::move(task), boost::asio::use_future);
    try {
        return future.get();
    } catch (const std::exception &e) {
        std::cerr << "[MySqlBatchTaskRepository] " << context << " failed: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "[MySqlBatchTaskRepository] " << context << " failed with unknown error" << std::endl;
    }
    return fallback;
}

} // namespace

MySqlBatchTaskRepository::MySqlBatchTaskRepository(std::shared_ptr<MySqlConnectionPool> pool) : pool_(std::move(pool))
{
    if (!pool_) {
        throw std::invalid_argument("[MySqlBatchTaskRepository] pool must not be null");
    }
    sql_.emplace("insert_batch", sql::SqlFileLoader::loadOrThrow("sql/mysql/crawl_batches/insert_batch.sql"));
    sql_.emplace("insert_item", sql::SqlFileLoader::loadOrThrow("sql/mysql/crawl_batch_items/insert_item.sql"));
    sql_.emplace("update_batch", sql::SqlFileLoader::loadOrThrow("sql/mysql/crawl_batches/update_batch.sql"));
    sql_.emplace("update_item", sql::SqlFileLoader::loadOrThrow("sql/mysql/crawl_batch_items/update_item.sql"));
    sql_.emplace("insert_event", sql::SqlFileLoader::loadOrThrow("sql/mysql/crawl_batch_events/insert_event.sql"));
    sql_.emplace("list_batches", sql::SqlFileLoader::loadOrThrow("sql/mysql/crawl_batches/list_batches.sql"));
    sql_.emplace("count_batches", sql::SqlFileLoader::loadOrThrow("sql/mysql/crawl_batches/count_batches.sql"));
    sql_.emplace("find_batch", sql::SqlFileLoader::loadOrThrow("sql/mysql/crawl_batches/find_batch.sql"));
    sql_.emplace("list_items", sql::SqlFileLoader::loadOrThrow("sql/mysql/crawl_batch_items/list_items.sql"));
    sql_.emplace("count_items", sql::SqlFileLoader::loadOrThrow("sql/mysql/crawl_batch_items/count_items.sql"));
    sql_.emplace("list_events", sql::SqlFileLoader::loadOrThrow("sql/mysql/crawl_batch_events/list_events.sql"));
    sql_.emplace("count_events", sql::SqlFileLoader::loadOrThrow("sql/mysql/crawl_batch_events/count_events.sql"));
    sql_.emplace("select_stale_batches", sql::SqlFileLoader::loadOrThrow("sql/mysql/crawl_batches/select_stale_batches.sql"));
    sql_.emplace("recover_stale_items", sql::SqlFileLoader::loadOrThrow("sql/mysql/crawl_batch_items/recover_stale_items.sql"));
    sql_.emplace("recover_stale_batch", sql::SqlFileLoader::loadOrThrow("sql/mysql/crawl_batches/recover_stale_batch.sql"));
    sql_.emplace("archive_batch", sql::SqlFileLoader::loadOrThrow("sql/mysql/crawl_batches/archive_batch.sql"));
    sql_.emplace("state_distribution", sql::SqlFileLoader::loadOrThrow("sql/mysql/crawl_batches/state_distribution.sql"));
    sql_.emplace("failure_task_types", sql::SqlFileLoader::loadOrThrow("sql/mysql/crawl_batches/failure_task_types.sql"));
    sql_.emplace("running_count", sql::SqlFileLoader::loadOrThrow("sql/mysql/crawl_batches/running_count.sql"));
    sql_.emplace("oldest_stale_running", sql::SqlFileLoader::loadOrThrow("sql/mysql/crawl_batches/oldest_stale_running.sql"));
    sql_.emplace("latest_batch_by_state", sql::SqlFileLoader::loadOrThrow("sql/mysql/crawl_batches/latest_batch_by_state.sql"));
}

bool MySqlBatchTaskRepository::createBatch(const application::model::CrawlBatchRecord &batch, const std::vector<application::model::CrawlBatchItemRecord> &items)
{
    return MySqlWriteRunner::waitForWrite(pool_->executor(), coCreateBatch(batch, items), "MySqlBatchTaskRepository", "createBatch");
}

bool MySqlBatchTaskRepository::updateBatchProgress(const application::model::CrawlBatchRecord &batch)
{
    return MySqlWriteRunner::waitForWrite(pool_->executor(), coUpdateBatchProgress(batch), "MySqlBatchTaskRepository", "updateBatchProgress");
}

bool MySqlBatchTaskRepository::updateItem(const application::model::CrawlBatchItemRecord &item)
{
    return MySqlWriteRunner::waitForWrite(pool_->executor(), coUpdateItem(item), "MySqlBatchTaskRepository", "updateItem");
}

bool MySqlBatchTaskRepository::upsertItem(const application::model::CrawlBatchItemRecord &item)
{
    return MySqlWriteRunner::waitForWrite(pool_->executor(), coUpsertItem(item), "MySqlBatchTaskRepository", "upsertItem");
}

bool MySqlBatchTaskRepository::appendEvent(const application::model::CrawlBatchEventRecord &event)
{
    return MySqlWriteRunner::waitForWrite(pool_->executor(), coAppendEvent(event), "MySqlBatchTaskRepository", "appendEvent");
}

application::model::CrawlBatchPage MySqlBatchTaskRepository::listBatches(const application::model::CrawlBatchQuery &query)
{
    return waitForResult(pool_->executor(), coListBatches(query), "listBatches", application::model::CrawlBatchPage {});
}

std::optional<application::model::CrawlBatchRecord> MySqlBatchTaskRepository::findBatch(std::string batch_id)
{
    return waitForResult<std::optional<application::model::CrawlBatchRecord>>(pool_->executor(), coFindBatch(std::move(batch_id)), "findBatch", std::nullopt);
}

application::model::CrawlBatchItemPage MySqlBatchTaskRepository::listItems(std::string batch_id, const application::model::CrawlBatchItemQuery &query)
{
    return waitForResult(pool_->executor(), coListItems(std::move(batch_id), query), "listItems", application::model::CrawlBatchItemPage {});
}

application::model::CrawlBatchEventPage MySqlBatchTaskRepository::listEvents(std::string batch_id, const application::model::CrawlBatchEventQuery &query)
{
    return waitForResult(pool_->executor(), coListEvents(std::move(batch_id), query), "listEvents", application::model::CrawlBatchEventPage {});
}

application::model::CrawlBatchSummary MySqlBatchTaskRepository::summary(std::string source, int stale_after_seconds)
{
    return waitForResult(pool_->executor(), coSummary(std::move(source), stale_after_seconds), "summary", application::model::CrawlBatchSummary {});
}

bool MySqlBatchTaskRepository::archiveBatch(std::string batch_id, std::string reason)
{
    return MySqlWriteRunner::waitForWrite(pool_->executor(), coArchiveBatch(std::move(batch_id), std::move(reason)), "MySqlBatchTaskRepository", "archiveBatch");
}

int MySqlBatchTaskRepository::recoverStaleRunningBatches(std::string source, int stale_after_seconds, std::string error)
{
    return waitForResult(pool_->executor(), coRecoverStaleRunningBatches(std::move(source), stale_after_seconds, std::move(error)), "recoverStaleRunningBatches", 0);
}

boost::asio::awaitable<bool> MySqlBatchTaskRepository::coCreateBatch(application::model::CrawlBatchRecord batch, std::vector<application::model::CrawlBatchItemRecord> items)
{
    auto connection = co_await pool_->coAcquire();
    boost::mysql::results result;
    co_await connection->async_execute("START TRANSACTION", result, boost::asio::use_awaitable);
    bool needs_rollback = false;
    try {
        auto batch_statement = co_await connection->async_prepare_statement(sql_.at("insert_batch"), boost::asio::use_awaitable);
        co_await connection->async_execute(
            batch_statement.bind(batch.batch_id, batch.source, batch.task_type, batch.state, batch.request_params, batch.settings_snapshot, batch.limit_count, batch.target_count, batch.processed_count, batch.success_count, batch.failure_count, batch.cancel_requested ? 1 : 0, batch.last_error, batch.created_at, batch.started_at, batch.finished_at, batch.updated_at, batch.archived_at, batch.archived_reason, batch.parent_batch_id),
            result,
            boost::asio::use_awaitable
        );

        auto item_statement = co_await connection->async_prepare_statement(sql_.at("insert_item"), boost::asio::use_awaitable);
        for (const auto &item : items) {
            co_await connection->async_execute(
                item_statement.bind(item.batch_id, item.item_index, item.item_key, item.item_type, item.item_label, item.source, item.source_singer_id, item.source_album_id, item.source_song_id, item.state, item.retry_count, item.written_count, item.error_category, item.result_code, item.started_at, item.finished_at, item.last_error, item.payload.empty() ? "{}" : item.payload, item.created_at, item.updated_at),
                result,
                boost::asio::use_awaitable
            );
        }

        co_await connection->async_execute("COMMIT", result, boost::asio::use_awaitable);
        co_return true;
    } catch (const std::exception &e) {
        std::cerr << "[MySqlBatchTaskRepository] coCreateBatch failed: " << e.what() << std::endl;
        needs_rollback = true;
    }

    if (needs_rollback) {
        boost::mysql::error_code ignored_error;
        co_await connection->async_execute("ROLLBACK", result, boost::asio::redirect_error(boost::asio::use_awaitable, ignored_error));
    }
    co_return false;
}

boost::asio::awaitable<bool> MySqlBatchTaskRepository::coUpdateBatchProgress(application::model::CrawlBatchRecord batch)
{
    auto connection = co_await pool_->coAcquire();
    auto statement = co_await connection->async_prepare_statement(sql_.at("update_batch"), boost::asio::use_awaitable);
    boost::mysql::results result;
    co_await connection->async_execute(statement.bind(batch.state, batch.state, batch.target_count, batch.target_count, batch.processed_count, batch.success_count, batch.failure_count, batch.cancel_requested ? 1 : 0, batch.last_error, batch.last_error, batch.finished_at, batch.finished_at, batch.updated_at, batch.batch_id), result, boost::asio::use_awaitable);
    co_return result.affected_rows() > 0;
}

boost::asio::awaitable<bool> MySqlBatchTaskRepository::coUpdateItem(application::model::CrawlBatchItemRecord item)
{
    auto connection = co_await pool_->coAcquire();
    auto statement = co_await connection->async_prepare_statement(sql_.at("update_item"), boost::asio::use_awaitable);
    boost::mysql::results result;
    co_await connection->async_execute(statement.bind(item.state, item.retry_count, item.written_count, item.error_category, item.error_category, item.result_code, item.result_code, item.started_at, item.started_at, item.finished_at, item.finished_at, item.last_error, item.last_error, item.payload, item.payload.empty() ? "{}" : item.payload, item.updated_at, item.batch_id, item.item_index), result, boost::asio::use_awaitable);
    co_return result.affected_rows() > 0;
}

boost::asio::awaitable<bool> MySqlBatchTaskRepository::coUpsertItem(application::model::CrawlBatchItemRecord item)
{
    auto connection = co_await pool_->coAcquire();
    auto statement = co_await connection->async_prepare_statement(sql_.at("insert_item"), boost::asio::use_awaitable);
    boost::mysql::results result;
    co_await connection->async_execute(statement.bind(item.batch_id, item.item_index, item.item_key, item.item_type, item.item_label, item.source, item.source_singer_id, item.source_album_id, item.source_song_id, item.state, item.retry_count, item.written_count, item.error_category, item.result_code, item.started_at, item.finished_at, item.last_error, item.payload.empty() ? "{}" : item.payload, item.created_at, item.updated_at), result, boost::asio::use_awaitable);
    co_return result.affected_rows() > 0;
}

boost::asio::awaitable<bool> MySqlBatchTaskRepository::coAppendEvent(application::model::CrawlBatchEventRecord event)
{
    auto connection = co_await pool_->coAcquire();
    auto statement = co_await connection->async_prepare_statement(sql_.at("insert_event"), boost::asio::use_awaitable);
    boost::mysql::results result;
    co_await connection->async_execute(statement.bind(event.batch_id, event.item_index, event.event_type, event.message, event.payload.empty() ? "{}" : event.payload, event.created_at), result, boost::asio::use_awaitable);
    co_return result.affected_rows() > 0;
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
