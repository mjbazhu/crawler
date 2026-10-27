#include "crawler/infrastructure/music/persistence/mysql/MySqlSongResourceQueue.h"

#include "crawler/infrastructure/music/persistence/mysql/MySqlValueReader.h"
#include "MySqlResourceQueueSupport.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlWriteRunner.h"
#include "crawler/infrastructure/music/persistence/sql/SqlFileLoader.h"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/mysql/results.hpp>
#include <boost/mysql/statement.hpp>

#include <algorithm>
#include <array>
#include <exception>
#include <future>
#include <iostream>
#include <set>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {
using resource_detail::waitForMysqlResult;
using resource_detail::logResourceQueueError;

bool MySqlSongResourceQueue::markSucceeded(application::model::SongResourceSuccessUpdate update)
{
    return MySqlWriteRunner::waitForWrite(pool_->executor(), coMarkSucceeded(std::move(update)), "MySqlSongResourceQueue", "markSucceeded");
}

application::result::ServiceResult<bool> MySqlSongResourceQueue::confirmSucceeded(application::model::SongResourceSuccessUpdate update)
{
    using Result = application::result::ServiceResult<bool>;
    const auto actual = waitForMysqlResult<std::optional<bool>>(pool_->executor(), coConfirmSucceeded(std::move(update)), "MySqlSongResourceQueue", "confirmSucceeded", std::nullopt);
    return actual ? Result::success(*actual) : Result::failure(application::result::ServiceErrorType::InternalError, "resource writeback result is uncertain");
}

bool MySqlSongResourceQueue::markFailed(application::model::SongResourceFailureUpdate update)
{
    return MySqlWriteRunner::waitForWrite(pool_->executor(), coMarkFailed(std::move(update)), "MySqlSongResourceQueue", "markFailed");
}

bool MySqlSongResourceQueue::markSkipped(application::model::SongResourceSkipUpdate update)
{
    return MySqlWriteRunner::waitForWrite(pool_->executor(), coMarkSkipped(std::move(update)), "MySqlSongResourceQueue", "markSkipped");
}

boost::asio::awaitable<bool> MySqlSongResourceQueue::coMarkSucceeded(application::model::SongResourceSuccessUpdate update)
{
    const auto result = co_await coConfirmSucceeded(std::move(update));
    co_return result.value_or(false);
}

boost::asio::awaitable<std::optional<bool>> MySqlSongResourceQueue::coConfirmSucceeded(application::model::SongResourceSuccessUpdate update)
{
    if (update.resource_id == 0 || update.local_path.empty()) {
        co_return false;
    }
    if (update.result_code.empty()) {
        update.result_code = "ok";
    }

    try {
        boost::mysql::pooled_connection connection = co_await pool_->coAcquire();
        boost::mysql::statement statement = co_await connection->async_prepare_statement(mark_succeeded_sql_, boost::asio::use_awaitable);
        boost::mysql::results result;
        const std::string cloud_backend = update.cloud_backend;
        co_await connection->async_execute(
            statement.bind(
                update.local_path,
                std::move(update.cloud_backend),
                std::move(update.cloud_path),
                update.size_bytes,
                update.checksum,
                std::move(update.result_code),
                std::move(update.batch_id),
                cloud_backend,
                update.resource_id,
                update.claim_generation
            ),
            result,
            boost::asio::use_awaitable
        );
        if (result.affected_rows() > 0) co_return true;
    } catch (const std::exception &e) {
        logResourceQueueError("coMarkSucceeded", e);
    }
    // UPDATE 的响应丢失不能直接解释为未提交；同一行锁读取会等待在途事务结束。
    try {
        auto connection = co_await pool_->coAcquire();
        auto statement = co_await connection->async_prepare_statement(sql::SqlFileLoader::loadOrThrow("sql/mysql/song_resources/find_write_receipt.sql"), boost::asio::use_awaitable);
        boost::mysql::results actual;
        co_await connection->async_execute(statement.bind(update.resource_id), actual, boost::asio::use_awaitable);
        if (actual.rows().empty()) co_return false;
        const auto row = actual.rows().front();
        co_return MySqlValueReader::intAt(row, 0) == 2 && MySqlValueReader::uint64At(row, 1) == update.claim_generation &&MySqlValueReader::stringAt(row, 2) == update.local_path;
    } catch (const std::exception &) {
        co_return std::nullopt;
    }
}

boost::asio::awaitable<bool> MySqlSongResourceQueue::coMarkFailed(application::model::SongResourceFailureUpdate update)
{
    if (update.resource_id == 0) {
        co_return false;
    }
    if (update.error_category.empty()) {
        update.error_category = "unknown";
    }
    if (update.result_code.empty()) {
        update.result_code = "failed";
    }

    try {
        boost::mysql::pooled_connection connection = co_await pool_->coAcquire();
        boost::mysql::statement statement = co_await connection->async_prepare_statement(mark_failed_sql_, boost::asio::use_awaitable);
        boost::mysql::results result;
        co_await connection->async_execute(statement.bind(std::move(update.message), std::move(update.error_category), std::move(update.result_code), std::move(update.batch_id), update.resource_id, update.claim_generation), result, boost::asio::use_awaitable);
        co_return result.affected_rows() > 0;
    } catch (const std::exception &e) {
        logResourceQueueError("coMarkFailed", e);
    }
    co_return false;
}

boost::asio::awaitable<bool> MySqlSongResourceQueue::coMarkSkipped(application::model::SongResourceSkipUpdate update)
{
    if (update.resource_id == 0 && update.song_source_ref_id == 0) {
        co_return false;
    }
    if (update.error_category.empty()) {
        update.error_category = "resource";
    }
    if (update.result_code.empty()) {
        update.result_code = "skipped";
    }

    try {
        const std::string expected_code = update.result_code;
        const std::string resource_kind = update.resource_kind;
        const std::string format_type = update.format_type;
        boost::mysql::pooled_connection connection = co_await pool_->coAcquire();
        boost::mysql::statement statement = co_await connection->async_prepare_statement(mark_skipped_sql_, boost::asio::use_awaitable);
        boost::mysql::results result;
        co_await connection->async_execute(
            statement.bind(
                std::move(update.message),
                std::move(update.error_category),
                std::move(update.result_code),
                std::move(update.batch_id),
                update.song_source_ref_id,
                update.song_source_ref_id,
                update.song_source_ref_id,
                update.resource_id,
                resource_kind,
                resource_kind,
                format_type,
                format_type,
                update.resource_id,
                update.claim_generation
            ),
            result,
            boost::asio::use_awaitable
        );
        if (result.affected_rows() > 0) co_return true;
        // 同一首歌并发音质已被另一有效 worker 跳过时，认可相同代次与业务码。
        auto receipt = co_await connection->async_prepare_statement(sql::SqlFileLoader::loadOrThrow("sql/mysql/song_resources/find_skip_receipt.sql"), boost::asio::use_awaitable);
        boost::mysql::results actual;
        co_await connection->async_execute(receipt.bind(update.resource_id), actual, boost::asio::use_awaitable);
        if (actual.rows().empty()) co_return false;
        const auto row = actual.rows().front();
        co_return MySqlValueReader::intAt(row, 0) == 4 && MySqlValueReader::uint64At(row, 1) == update.claim_generation &&MySqlValueReader::stringAt(row, 2) == expected_code;
    } catch (const std::exception &e) {
        logResourceQueueError("coMarkSkipped", e);
    }
    co_return false;
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
