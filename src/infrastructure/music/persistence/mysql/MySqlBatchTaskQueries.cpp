#include "crawler/infrastructure/music/persistence/mysql/MySqlBatchTaskRepository.h"

#include "crawler/infrastructure/music/persistence/mysql/MySqlValueReader.h"

#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/redirect_error.hpp>
#include <boost/mysql/results.hpp>
#include <boost/mysql/statement.hpp>
#include <ctime>
#include <iostream>
#include <exception>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {
namespace {

// 读取批次列表查询的标量计数。
std::uint64_t countFromResult(const boost::mysql::results &result)
{
    if (result.rows().empty()) {
        return 0;
    }
    return MySqlValueReader::uint64At(result.rows().front(), 0);
}

} // namespace

boost::asio::awaitable<application::model::CrawlBatchPage> MySqlBatchTaskRepository::coListBatches(application::model::CrawlBatchQuery query)
{
    application::model::CrawlBatchPage page;
    page.page = query.page;
    page.page_size = query.page_size;
    const auto offset = static_cast<std::uint64_t>((query.page - 1) * query.page_size);
    auto connection = co_await pool_->coAcquire();
    boost::mysql::results result;
    auto count_statement = co_await connection->async_prepare_statement(sql_.at("count_batches"), boost::asio::use_awaitable);
    co_await connection->async_execute(count_statement.bind(query.state, query.task_type, query.keyword, query.display_name, query.target_summary, query.target_keywords, query.last_error, query.error_category, query.result_code, query.include_archived ? 1 : 0), result, boost::asio::use_awaitable);
    page.total = countFromResult(result);

    auto list_statement = co_await connection->async_prepare_statement(sql_.at("list_batches"), boost::asio::use_awaitable);
    co_await connection->async_execute(list_statement.bind(query.state, query.task_type, query.keyword, query.display_name, query.target_summary, query.target_keywords, query.last_error, query.error_category, query.result_code, query.include_archived ? 1 : 0, static_cast<std::uint64_t>(query.page_size), offset), result, boost::asio::use_awaitable);
    for (auto row : result.rows()) {
        page.items.push_back(mapBatch(row));
    }
    co_return page;
}

boost::asio::awaitable<std::optional<application::model::CrawlBatchRecord>> MySqlBatchTaskRepository::coFindBatch(std::string batch_id)
{
    auto connection = co_await pool_->coAcquire();
    auto statement = co_await connection->async_prepare_statement(sql_.at("find_batch"), boost::asio::use_awaitable);
    boost::mysql::results result;
    co_await connection->async_execute(statement.bind(std::move(batch_id)), result, boost::asio::use_awaitable);
    if (result.rows().empty()) {
        co_return std::nullopt;
    }
    co_return mapBatch(result.rows().front());
}

boost::asio::awaitable<application::model::CrawlBatchItemPage> MySqlBatchTaskRepository::coListItems(std::string batch_id, application::model::CrawlBatchItemQuery query)
{
    application::model::CrawlBatchItemPage page;
    page.page = query.page;
    page.page_size = query.page_size;
    const auto offset = static_cast<std::uint64_t>((query.page - 1) * query.page_size);
    auto connection = co_await pool_->coAcquire();
    boost::mysql::results result;
    auto count_statement = co_await connection->async_prepare_statement(sql_.at("count_items"), boost::asio::use_awaitable);
    co_await connection->async_execute(count_statement.bind(batch_id, query.state, query.state, query.error_category, query.error_category, query.result_code, query.result_code), result, boost::asio::use_awaitable);
    page.total = countFromResult(result);

    auto list_statement = co_await connection->async_prepare_statement(sql_.at("list_items"), boost::asio::use_awaitable);
    co_await connection->async_execute(list_statement.bind(std::move(batch_id), query.state, query.state, query.error_category, query.error_category, query.result_code, query.result_code, static_cast<std::uint64_t>(query.page_size), offset), result, boost::asio::use_awaitable);
    for (auto row : result.rows()) {
        page.items.push_back(mapItem(row));
    }
    co_return page;
}

boost::asio::awaitable<application::model::CrawlBatchEventPage> MySqlBatchTaskRepository::coListEvents(std::string batch_id, application::model::CrawlBatchEventQuery query)
{
    application::model::CrawlBatchEventPage page;
    page.page = query.page;
    page.page_size = query.page_size;
    const auto offset = static_cast<std::uint64_t>((query.page - 1) * query.page_size);
    auto connection = co_await pool_->coAcquire();
    boost::mysql::results result;
    auto count_statement = co_await connection->async_prepare_statement(sql_.at("count_events"), boost::asio::use_awaitable);
    co_await connection->async_execute(count_statement.bind(batch_id), result, boost::asio::use_awaitable);
    page.total = countFromResult(result);

    auto list_statement = co_await connection->async_prepare_statement(sql_.at("list_events"), boost::asio::use_awaitable);
    co_await connection->async_execute(list_statement.bind(std::move(batch_id), static_cast<std::uint64_t>(query.page_size), offset), result, boost::asio::use_awaitable);
    for (auto row : result.rows()) {
        page.items.push_back(mapEvent(row));
    }
    co_return page;
}

boost::asio::awaitable<application::model::CrawlBatchSummary> MySqlBatchTaskRepository::coSummary(std::string source, int stale_after_seconds)
{
    application::model::CrawlBatchSummary summary;
    summary.generated_at = static_cast<std::uint64_t>(std::time(nullptr));
    if (source.empty()) {
        source = "migu";
    }
    stale_after_seconds = (std::max)(1, stale_after_seconds);
    auto connection = co_await pool_->coAcquire();
    boost::mysql::results result;

    auto state_statement = co_await connection->async_prepare_statement(sql_.at("state_distribution"), boost::asio::use_awaitable);
    co_await connection->async_execute(state_statement.bind(source), result, boost::asio::use_awaitable);
    for (boost::mysql::row_view row : result.rows()) {
        summary.state_distribution.emplace_back(MySqlValueReader::stringAt(row, 0), MySqlValueReader::uint64At(row, 1));
    }

    auto failure_statement = co_await connection->async_prepare_statement(sql_.at("failure_task_types"), boost::asio::use_awaitable);
    co_await connection->async_execute(failure_statement.bind(source), result, boost::asio::use_awaitable);
    for (boost::mysql::row_view row : result.rows()) {
        summary.failure_task_types.emplace_back(MySqlValueReader::stringAt(row, 0), MySqlValueReader::uint64At(row, 1));
    }

    auto running_statement = co_await connection->async_prepare_statement(sql_.at("running_count"), boost::asio::use_awaitable);
    co_await connection->async_execute(running_statement.bind(source), result, boost::asio::use_awaitable);
    summary.running_count = countFromResult(result);

    auto stale_statement = co_await connection->async_prepare_statement(sql_.at("oldest_stale_running"), boost::asio::use_awaitable);
    co_await connection->async_execute(stale_statement.bind(source, stale_after_seconds), result, boost::asio::use_awaitable);
    summary.oldest_stale_running_at = countFromResult(result);

    auto latest_statement = co_await connection->async_prepare_statement(sql_.at("latest_batch_by_state"), boost::asio::use_awaitable);
    co_await connection->async_execute(latest_statement.bind(source, "Completed"), result, boost::asio::use_awaitable);
    if (!result.rows().empty()) {
        summary.latest_success = mapBatch(result.rows().front());
    }
    co_await connection->async_execute(latest_statement.bind(source, "Failed"), result, boost::asio::use_awaitable);
    if (!result.rows().empty()) {
        summary.latest_failed = mapBatch(result.rows().front());
    }

    co_return summary;
}

boost::asio::awaitable<bool> MySqlBatchTaskRepository::coArchiveBatch(std::string batch_id, std::string reason)
{
    auto connection = co_await pool_->coAcquire();
    auto statement = co_await connection->async_prepare_statement(sql_.at("archive_batch"), boost::asio::use_awaitable);
    boost::mysql::results result;
    co_await connection->async_execute(statement.bind(std::move(reason), std::move(batch_id)), result, boost::asio::use_awaitable);
    co_return result.affected_rows() > 0;
}

boost::asio::awaitable<int> MySqlBatchTaskRepository::coRecoverStaleRunningBatches(std::string source, int stale_after_seconds, std::string error)
{
    if (stale_after_seconds <= 0) {
        stale_after_seconds = 1;
    }
    auto connection = co_await pool_->coAcquire();
    boost::mysql::results result;
    auto select_statement = co_await connection->async_prepare_statement(sql_.at("select_stale_batches"), boost::asio::use_awaitable);
    co_await connection->async_execute(select_statement.bind(source, stale_after_seconds), result, boost::asio::use_awaitable);

    std::vector<std::string> batch_ids;
    batch_ids.reserve(result.rows().size());
    for (boost::mysql::row_view row : result.rows()) {
        batch_ids.push_back(MySqlValueReader::stringAt(row, 0));
    }
    if (batch_ids.empty()) {
        co_return 0;
    }

    const auto now = static_cast<std::uint64_t>(std::time(nullptr));
    auto item_statement = co_await connection->async_prepare_statement(sql_.at("recover_stale_items"), boost::asio::use_awaitable);
    auto batch_statement = co_await connection->async_prepare_statement(sql_.at("recover_stale_batch"), boost::asio::use_awaitable);
    auto event_statement = co_await connection->async_prepare_statement(sql_.at("insert_event"), boost::asio::use_awaitable);
    int recovered = 0;
    for (const auto &batch_id : batch_ids) {
        boost::mysql::results write_result;
        co_await connection->async_execute("START TRANSACTION", write_result, boost::asio::use_awaitable);
        bool needs_rollback = false;
        try {
            co_await connection->async_execute(item_statement.bind(now, error, now, batch_id), write_result, boost::asio::use_awaitable);
            const auto recovered_items = write_result.affected_rows();
            co_await connection->async_execute(batch_statement.bind(error, now, now, batch_id), write_result, boost::asio::use_awaitable);
            if (write_result.affected_rows() > 0) {
                const std::string payload = "{\"stage\":\"batch_recovered\",\"recovered_items\":" + std::to_string(recovered_items) + "}";
                co_await connection->async_execute(event_statement.bind(batch_id, std::optional<int> {}, "timeoutRecovered", error, payload, now), write_result, boost::asio::use_awaitable);
                ++recovered;
            }
            co_await connection->async_execute("COMMIT", write_result, boost::asio::use_awaitable);
        } catch (const std::exception &e) {
            std::cerr << "[MySqlBatchTaskRepository] coRecoverStaleRunningBatches failed for batch " << batch_id << ": " << e.what() << std::endl;
            needs_rollback = true;
        }
        if (needs_rollback) {
            boost::mysql::error_code ignored_error;
            co_await connection->async_execute("ROLLBACK", write_result, boost::asio::redirect_error(boost::asio::use_awaitable, ignored_error));
        }
    }
    co_return recovered;
}

application::model::CrawlBatchRecord MySqlBatchTaskRepository::mapBatch(boost::mysql::row_view row)
{
    application::model::CrawlBatchRecord batch;
    batch.batch_id = MySqlValueReader::stringAt(row, 0);
    batch.source = MySqlValueReader::stringAt(row, 1);
    batch.task_type = MySqlValueReader::stringAt(row, 2);
    batch.state = MySqlValueReader::stringAt(row, 3);
    batch.request_params = MySqlValueReader::stringAt(row, 4);
    batch.settings_snapshot = MySqlValueReader::stringAt(row, 5);
    batch.limit_count = MySqlValueReader::intAt(row, 6);
    batch.target_count = MySqlValueReader::intAt(row, 7);
    batch.processed_count = MySqlValueReader::intAt(row, 8);
    batch.success_count = MySqlValueReader::intAt(row, 9);
    batch.failure_count = MySqlValueReader::intAt(row, 10);
    batch.cancel_requested = MySqlValueReader::intAt(row, 11) != 0;
    batch.last_error = MySqlValueReader::stringAt(row, 12);
    batch.created_at = MySqlValueReader::uint64At(row, 13);
    batch.started_at = MySqlValueReader::uint64At(row, 14);
    batch.finished_at = MySqlValueReader::uint64At(row, 15);
    batch.updated_at = MySqlValueReader::uint64At(row, 16);
    batch.archived_at = MySqlValueReader::uint64At(row, 17);
    batch.archived_reason = MySqlValueReader::stringAt(row, 18);
    batch.parent_batch_id = MySqlValueReader::stringAt(row, 19);
    batch.display_name = MySqlValueReader::stringAt(row, 20);
    batch.target_summary = MySqlValueReader::stringAt(row, 21);
    batch.target_keywords = MySqlValueReader::stringAt(row, 22);
    batch.error_category = MySqlValueReader::stringAt(row, 23);
    batch.result_code = MySqlValueReader::stringAt(row, 24);
    return batch;
}

application::model::CrawlBatchItemRecord MySqlBatchTaskRepository::mapItem(boost::mysql::row_view row)
{
    application::model::CrawlBatchItemRecord item;
    item.batch_id = MySqlValueReader::stringAt(row, 0);
    item.item_index = MySqlValueReader::intAt(row, 1);
    item.item_key = MySqlValueReader::stringAt(row, 2);
    item.item_type = MySqlValueReader::stringAt(row, 3);
    item.item_label = MySqlValueReader::stringAt(row, 4);
    item.source = MySqlValueReader::stringAt(row, 5);
    item.source_singer_id = MySqlValueReader::stringAt(row, 6);
    item.source_album_id = MySqlValueReader::stringAt(row, 7);
    item.source_song_id = MySqlValueReader::stringAt(row, 8);
    item.state = MySqlValueReader::stringAt(row, 9);
    item.retry_count = MySqlValueReader::intAt(row, 10);
    item.written_count = MySqlValueReader::intAt(row, 11);
    item.error_category = MySqlValueReader::stringAt(row, 12);
    item.result_code = MySqlValueReader::stringAt(row, 13);
    item.started_at = MySqlValueReader::uint64At(row, 14);
    item.finished_at = MySqlValueReader::uint64At(row, 15);
    item.last_error = MySqlValueReader::stringAt(row, 16);
    item.payload = MySqlValueReader::stringAt(row, 17);
    item.created_at = MySqlValueReader::uint64At(row, 18);
    item.updated_at = MySqlValueReader::uint64At(row, 19);
    return item;
}

application::model::CrawlBatchEventRecord MySqlBatchTaskRepository::mapEvent(boost::mysql::row_view row)
{
    application::model::CrawlBatchEventRecord event;
    event.batch_id = MySqlValueReader::stringAt(row, 0);
    if (row.size() > 1 && !row[1].is_null()) {
        event.item_index = MySqlValueReader::intAt(row, 1);
    }
    event.event_type = MySqlValueReader::stringAt(row, 2);
    event.message = MySqlValueReader::stringAt(row, 3);
    event.payload = MySqlValueReader::stringAt(row, 4);
    event.created_at = MySqlValueReader::uint64At(row, 5);
    return event;
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
