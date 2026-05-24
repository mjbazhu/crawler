#include "crawler/infrastructure/music/persistence/mysql/MySqlSongResourceQueue.h"

#include "crawler/infrastructure/music/persistence/mysql/MySqlValueReader.h"
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
#include <stdexcept>
#include <string_view>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {

namespace {

namespace sql_loader = infrastructure::music::persistence::sql;

constexpr std::size_t kStatusSlotCount = 5;

// 在同步 port 边界阻塞等待 MySQL 查询完成，避免把 Asio 暴露到 application。
template <typename T>
T waitForMysqlResult(boost::asio::any_io_executor executor, boost::asio::awaitable<T> task, std::string_view component, std::string_view context, T fallback)
{
    std::future<T> future = boost::asio::co_spawn(std::move(executor), std::move(task), boost::asio::use_future);
    try {
        return future.get();
    } catch (const std::exception &e) {
        std::cerr << "[" << component << "] " << context << " failed: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "[" << component << "] " << context << " failed with unknown error" << std::endl;
    }
    return fallback;
}

// 记录资源队列异常。
void logResourceQueueError(const std::string &context, const std::exception &error)
{
    std::cerr << "[MySqlSongResourceQueue] " << context << " failed: " << error.what() << std::endl;
}

// 规整资源状态过滤，空过滤默认认领待获取和失败；跳过状态不再认领。
std::array<int, kStatusSlotCount> normalizedStatuses(const std::vector<int> &statuses)
{
    std::array<int, kStatusSlotCount> normalized {-1, -1, -1, -1, -1};
    std::vector<int> values;
    values.reserve(statuses.empty() ? 2 : statuses.size());
    if (statuses.empty()) {
        values = {0, 3};
    } else {
        for (int status : statuses) {
            if ((status == 0 || status == 3) && std::find(values.begin(), values.end(), status) == values.end()) {
                values.push_back(status);
            }
        }
    }

    for (std::size_t index = 0; index < values.size() && index < normalized.size(); ++index) {
        normalized[index] = values[index];
    }
    return normalized;
}

// 规整资源队列认领查询。
application::model::SongResourceClaimQuery normalizeQuery(application::model::SongResourceClaimQuery query)
{
    if (query.source.empty()) {
        query.source = "migu";
    }
    if (query.limit == 0) {
        query.limit = 20;
    }
    query.limit = (std::min<std::size_t>)(query.limit, 1000);
    return query;
}

} // namespace

MySqlSongResourceQueue::MySqlSongResourceQueue(std::shared_ptr<MySqlConnectionPool> pool) : MySqlSongResourceQueue(std::move(pool), SqlPaths {})
{}

MySqlSongResourceQueue::MySqlSongResourceQueue(std::shared_ptr<MySqlConnectionPool> pool, SqlPaths paths) : pool_(std::move(pool)),
                                                                                                            claim_select_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.claim_select)),
                                                                                                            claim_update_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.claim_update)),
                                                                                                            related_claim_update_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.related_claim_update)),
                                                                                                            related_find_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.related_find)),
                                                                                                            mark_succeeded_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.mark_succeeded)),
                                                                                                            mark_failed_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.mark_failed)),
                                                                                                            mark_skipped_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.mark_skipped))
{
    if (!pool_) {
        throw std::invalid_argument("[MySqlSongResourceQueue] pool must not be null");
    }
}

std::vector<application::model::SongResourceQueueTarget> MySqlSongResourceQueue::claimPending(application::model::SongResourceClaimQuery query)
{
    return waitForMysqlResult<std::vector<application::model::SongResourceQueueTarget>>(pool_->executor(), coClaimPending(std::move(query)), "MySqlSongResourceQueue", "claimPending", {});
}

std::optional<application::model::SongResourceQueueTarget> MySqlSongResourceQueue::claimRelated(application::model::SongResourceRelatedClaimRequest request)
{
    return waitForMysqlResult<std::optional<application::model::SongResourceQueueTarget>>(pool_->executor(), coClaimRelated(std::move(request)), "MySqlSongResourceQueue", "claimRelated", std::nullopt);
}

bool MySqlSongResourceQueue::markSucceeded(application::model::SongResourceSuccessUpdate update)
{
    return MySqlWriteRunner::waitForWrite(pool_->executor(), coMarkSucceeded(std::move(update)), "MySqlSongResourceQueue", "markSucceeded");
}

bool MySqlSongResourceQueue::markFailed(application::model::SongResourceFailureUpdate update)
{
    return MySqlWriteRunner::waitForWrite(pool_->executor(), coMarkFailed(std::move(update)), "MySqlSongResourceQueue", "markFailed");
}

bool MySqlSongResourceQueue::markSkipped(application::model::SongResourceSkipUpdate update)
{
    return MySqlWriteRunner::waitForWrite(pool_->executor(), coMarkSkipped(std::move(update)), "MySqlSongResourceQueue", "markSkipped");
}

boost::asio::awaitable<std::vector<application::model::SongResourceQueueTarget>> MySqlSongResourceQueue::coClaimPending(application::model::SongResourceClaimQuery query)
{
    query = normalizeQuery(std::move(query));
    std::vector<application::model::SongResourceQueueTarget> claimed;
    const std::array<int, kStatusSlotCount> statuses = normalizedStatuses(query.statuses);

    try {
        boost::mysql::pooled_connection connection = co_await pool_->coAcquire();
        boost::mysql::statement select_statement = co_await connection->async_prepare_statement(claim_select_sql_, boost::asio::use_awaitable);
        boost::mysql::results result;
        co_await connection->async_execute(
            select_statement.bind(query.source, query.resource_kind, query.resource_kind, query.source_content_id, query.source_content_id, query.format_type, query.format_type, statuses[0], statuses[1], statuses[2], statuses[3], statuses[4], static_cast<std::uint64_t>(query.limit), static_cast<std::uint64_t>(query.offset)),
            result,
            boost::asio::use_awaitable
        );

        boost::mysql::statement claim_statement = co_await connection->async_prepare_statement(claim_update_sql_, boost::asio::use_awaitable);
        for (boost::mysql::row_view row : result.rows()) {
            application::model::SongResourceQueueTarget target = mapTarget(row);
            boost::mysql::results claim_result;
            co_await connection->async_execute(
                claim_statement.bind(query.batch_id, target.resource_id, statuses[0], statuses[1], statuses[2], statuses[3], statuses[4]),
                claim_result,
                boost::asio::use_awaitable
            );
            if (claim_result.affected_rows() > 0) {
                target.status = 1;
                claimed.push_back(std::move(target));
            }
        }
    } catch (const std::exception &e) {
        logResourceQueueError("coClaimPending", e);
    }
    co_return claimed;
}

boost::asio::awaitable<std::optional<application::model::SongResourceQueueTarget>> MySqlSongResourceQueue::coClaimRelated(application::model::SongResourceRelatedClaimRequest request)
{
    if (request.song_source_ref_id == 0 || request.resource_kind.empty() || request.remote_url.empty()) {
        co_return std::nullopt;
    }

    try {
        boost::mysql::pooled_connection connection = co_await pool_->coAcquire();
        boost::mysql::statement claim_statement = co_await connection->async_prepare_statement(related_claim_update_sql_, boost::asio::use_awaitable);
        boost::mysql::results claim_result;
        co_await connection->async_execute(claim_statement.bind(request.song_source_ref_id, request.resource_kind, request.remote_url, request.batch_id, request.remote_url, request.song_source_ref_id), claim_result, boost::asio::use_awaitable);
        if (claim_result.affected_rows() == 0) {
            co_return std::nullopt;
        }

        boost::mysql::statement find_statement = co_await connection->async_prepare_statement(related_find_sql_, boost::asio::use_awaitable);
        boost::mysql::results find_result;
        co_await connection->async_execute(find_statement.bind(request.song_source_ref_id, request.resource_kind), find_result, boost::asio::use_awaitable);
        if (find_result.rows().empty()) {
            co_return std::nullopt;
        }

        application::model::SongResourceQueueTarget target = mapTarget(find_result.rows().front());
        target.status = 1;
        co_return target;
    } catch (const std::exception &e) {
        logResourceQueueError("coClaimRelated", e);
    }
    co_return std::nullopt;
}

boost::asio::awaitable<bool> MySqlSongResourceQueue::coMarkSucceeded(application::model::SongResourceSuccessUpdate update)
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
        co_await connection->async_execute(statement.bind(std::move(update.remote_url), std::move(update.local_path), update.size_bytes, std::move(update.checksum), std::move(update.result_code), std::move(update.batch_id), update.resource_id), result, boost::asio::use_awaitable);
        co_return result.affected_rows() > 0;
    } catch (const std::exception &e) {
        logResourceQueueError("coMarkSucceeded", e);
    }
    co_return false;
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
        co_await connection->async_execute(statement.bind(std::move(update.message), std::move(update.error_category), std::move(update.result_code), std::move(update.batch_id), update.resource_id), result, boost::asio::use_awaitable);
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
                format_type
            ),
            result,
            boost::asio::use_awaitable
        );
        co_return result.affected_rows() > 0;
    } catch (const std::exception &e) {
        logResourceQueueError("coMarkSkipped", e);
    }
    co_return false;
}

application::model::SongResourceQueueTarget MySqlSongResourceQueue::mapTarget(boost::mysql::row_view row)
{
    application::model::SongResourceQueueTarget target;
    target.resource_id = MySqlValueReader::uint64At(row, 0);
    target.song_source_ref_id = MySqlValueReader::uint64At(row, 1);
    target.source = MySqlValueReader::stringAt(row, 2);
    target.source_song_id = MySqlValueReader::stringAt(row, 3);
    target.source_content_id = MySqlValueReader::stringAt(row, 4);
    target.source_resource_type = MySqlValueReader::stringAt(row, 5);
    target.audio_resource_type = MySqlValueReader::stringAt(row, 6);
    target.copyright_id = MySqlValueReader::stringAt(row, 7);
    target.title = MySqlValueReader::stringAt(row, 8);
    target.artist = MySqlValueReader::stringAt(row, 9);
    target.album = MySqlValueReader::stringAt(row, 10);
    target.source_album_kind = MySqlValueReader::intAt(row, 11);
    target.resource_kind = MySqlValueReader::stringAt(row, 12);
    target.format_type = MySqlValueReader::stringAt(row, 13);
    target.remote_url = MySqlValueReader::stringAt(row, 14);
    target.local_path = MySqlValueReader::stringAt(row, 15);
    target.status = MySqlValueReader::intAt(row, 16);
    target.retry_count = MySqlValueReader::intAt(row, 17);
    return target;
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
