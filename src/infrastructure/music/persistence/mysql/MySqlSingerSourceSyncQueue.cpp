#include "crawler/infrastructure/music/persistence/mysql/MySqlSingerSourceSyncQueue.h"

#include "crawler/infrastructure/music/persistence/mysql/MySqlValueReader.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlWriteRunner.h"
#include "crawler/infrastructure/music/persistence/sql/SqlFileLoader.h"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/mysql/results.hpp>
#include <boost/mysql/row_view.hpp>

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

// 查询状态槽位数量，对应 SQL 中固定的 IN (?, ?, ?, ?, ?)。
constexpr std::size_t kStatusSlotCount = 5;

// 在同步 port 边界阻塞等待 MySQL 查询完成，避免把 Asio 暴露到 application。
std::vector<application::model::SingerSourceSyncTarget> waitForMysqlSingerTargets(
    boost::asio::any_io_executor executor,
    boost::asio::awaitable<std::vector<application::model::SingerSourceSyncTarget>> task,
    std::string_view context
)
{
    auto future = boost::asio::co_spawn(std::move(executor), std::move(task), boost::asio::use_future);
    try {
        return future.get();
    } catch (const std::exception &e) {
        std::cerr << "[MySqlSingerSourceSyncQueue] " << context << " failed: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "[MySqlSingerSourceSyncQueue] " << context << " failed with unknown error" << std::endl;
    }
    return {};
}

// 在同步 port 边界阻塞等待预览结果，避免把 Asio 暴露到 application。
application::model::SingerSourceSyncPreviewResult waitForMysqlSingerPreview(
    boost::asio::any_io_executor executor,
    boost::asio::awaitable<application::model::SingerSourceSyncPreviewResult> task,
    std::string_view context
)
{
    auto future = boost::asio::co_spawn(std::move(executor), std::move(task), boost::asio::use_future);
    try {
        return future.get();
    } catch (const std::exception &e) {
        std::cerr << "[MySqlSingerSourceSyncQueue] " << context << " failed: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "[MySqlSingerSourceSyncQueue] " << context << " failed with unknown error" << std::endl;
    }
    return {};
}

// 记录同步队列异常，避免同步调试入口无错误上下文。
void logSyncQueueError(const std::string &context, const std::exception &error)
{
    std::cerr << "[MySqlSingerSourceSyncQueue] " << context << " failed: " << error.what() << std::endl;
}

// 规整同步状态过滤，空过滤默认认领未同步、失败和部分完成。
std::array<int, kStatusSlotCount> normalizedStatuses(const std::vector<int> &statuses, bool force)
{
    std::array<int, kStatusSlotCount> normalized {-1, -1, -1, -1, -1};
    std::vector<int> values;
    values.reserve(statuses.empty() ? 3 : statuses.size());
    if (statuses.empty()) {
        values = {0, 3, 4};
    } else {
        for (int status : statuses) {
            if (status >= 0 && status <= 4 && std::find(values.begin(), values.end(), status) == values.end()) {
                values.push_back(status);
            }
        }
    }

    if (!force)
        std::erase(values, 2);
    else if (std::find(values.begin(), values.end(), 2) == values.end())
        values.push_back(2);

    for (std::size_t index = 0; index < values.size() && index < normalized.size(); ++index) {
        normalized[index] = values[index];
    }
    return normalized;
}

// 规整队列查询参数。
application::model::SingerSourceSyncQuery normalizeQuery(application::model::SingerSourceSyncQuery query)
{
    if (query.source.empty()) {
        query.source = "migu";
    }
    if (query.limit == 0) {
        query.limit = 20;
    }
    query.limit = (std::min<std::size_t>)(query.limit, 1000);
    if (query.stale_after_seconds <= 0) {
        query.stale_after_seconds = 3600;
    }
    return query;
}

// 从查询结果映射歌手同步目标。
application::model::SingerSourceSyncTarget mapSingerTargetRow(boost::mysql::row_view row)
{
    application::model::SingerSourceSyncTarget target;
    target.id = MySqlValueReader::uint64At(row, 0);
    target.singer_db_id = MySqlValueReader::uint64At(row, 1);
    target.source = MySqlValueReader::stringAt(row, 2);
    target.source_item_key = MySqlValueReader::stringAt(row, 3);
    target.source_singer_id = MySqlValueReader::stringAt(row, 4);
    target.name = MySqlValueReader::stringAt(row, 5);
    target.source_country = MySqlValueReader::stringAt(row, 6);
    target.gender = MySqlValueReader::intAt(row, 7);
    target.avatar_url = MySqlValueReader::stringAt(row, 8);
    target.source_song_total = MySqlValueReader::optionalIntAt(row, 9);
    target.source_album_total = MySqlValueReader::optionalIntAt(row, 10);
    target.sync_status = MySqlValueReader::intAt(row, 11);
    target.synced_at = MySqlValueReader::uint64At(row, 12);
    target.sync_count = MySqlValueReader::intAt(row, 13);
    target.sync_error = MySqlValueReader::stringAt(row, 14);
    target.updated_at = MySqlValueReader::uint64At(row, 15);
    return target;
}

// 从 count SQL 读取预览统计。
void mapPreviewCounts(const boost::mysql::results &result, application::model::SingerSourceSyncPreviewResult &preview)
{
    if (result.rows().empty()) {
        return;
    }
    preview.matched_count = MySqlValueReader::uint64At(result.rows().front(), 0);
    preview.eligible_count = MySqlValueReader::uint64At(result.rows().front(), 1);
}

} // namespace

MySqlSingerSourceSyncQueue::MySqlSingerSourceSyncQueue(std::shared_ptr<MySqlConnectionPool> pool) : MySqlSingerSourceSyncQueue(std::move(pool), SqlPaths {})
{}

MySqlSingerSourceSyncQueue::MySqlSingerSourceSyncQueue(std::shared_ptr<MySqlConnectionPool> pool, SqlPaths paths) : pool_(std::move(pool)),
                                                                                                                    select_pending_songs_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.select_pending_songs)),
                                                                                                                    select_pending_albums_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.select_pending_albums)),
                                                                                                                    count_pending_songs_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.count_pending_songs)),
                                                                                                                    count_pending_albums_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.count_pending_albums)),
                                                                                                                    claim_pending_songs_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.claim_pending_songs)),
                                                                                                                    claim_pending_albums_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.claim_pending_albums)),
                                                                                                                    update_songs_sync_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.update_songs_sync_status)),
                                                                                                                    update_albums_sync_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.update_albums_sync_status))
{
    if (!pool_) {
        throw std::invalid_argument("[MySqlSingerSourceSyncQueue] pool must not be null");
    }
}

std::vector<application::model::SingerSourceSyncTarget> MySqlSingerSourceSyncQueue::claimPendingSongs(application::model::SingerSourceSyncQuery query)
{
    return waitForMysqlSingerTargets(pool_->executor(), coClaimPendingSongs(std::move(query)), "claimPendingSongs");
}

std::vector<application::model::SingerSourceSyncTarget> MySqlSingerSourceSyncQueue::claimPendingAlbums(application::model::SingerSourceSyncQuery query)
{
    return waitForMysqlSingerTargets(pool_->executor(), coClaimPendingAlbums(std::move(query)), "claimPendingAlbums");
}

application::model::SingerSourceSyncPreviewResult MySqlSingerSourceSyncQueue::previewPendingSongs(application::model::SingerSourceSyncQuery query)
{
    return waitForMysqlSingerPreview(pool_->executor(), coPreviewPendingSongs(std::move(query)), "previewPendingSongs");
}

application::model::SingerSourceSyncPreviewResult MySqlSingerSourceSyncQueue::previewPendingAlbums(application::model::SingerSourceSyncQuery query)
{
    return waitForMysqlSingerPreview(pool_->executor(), coPreviewPendingAlbums(std::move(query)), "previewPendingAlbums");
}

bool MySqlSingerSourceSyncQueue::updateSongsSyncStatus(std::string source, std::string source_singer_id, int status, int count, std::string error)
{
    return MySqlWriteRunner::waitForWrite(pool_->executor(), coUpdateSongsSyncStatus(std::move(source), std::move(source_singer_id), status, count, std::move(error)), "MySqlSingerSourceSyncQueue", "updateSongsSyncStatus");
}

bool MySqlSingerSourceSyncQueue::updateAlbumsSyncStatus(std::string source, std::string source_singer_id, int status, int count, std::string error)
{
    return MySqlWriteRunner::waitForWrite(pool_->executor(), coUpdateAlbumsSyncStatus(std::move(source), std::move(source_singer_id), status, count, std::move(error)), "MySqlSingerSourceSyncQueue", "updateAlbumsSyncStatus");
}

boost::asio::awaitable<std::vector<application::model::SingerSourceSyncTarget>> MySqlSingerSourceSyncQueue::coClaimPendingSongs(application::model::SingerSourceSyncQuery query)
{
    query = normalizeQuery(std::move(query));
    std::vector<application::model::SingerSourceSyncTarget> claimed;
    const auto statuses = normalizedStatuses(query.sync_statuses, query.force);
    const int stale_enabled = query.include_stale_running ? 1 : 0;
    const int gender_filter = query.gender.value_or(-1);

    try {
        auto connection = co_await pool_->coAcquire();
        auto select_statement = co_await connection->async_prepare_statement(select_pending_songs_sql_, boost::asio::use_awaitable);
        boost::mysql::results result;
        co_await connection->async_execute(
            select_statement.bind(query.source, statuses[0], statuses[1], statuses[2], statuses[3], statuses[4], stale_enabled, query.stale_after_seconds, query.country, query.country, gender_filter, gender_filter, query.keyword, query.keyword, query.keyword, static_cast<std::uint64_t>(query.limit), static_cast<std::uint64_t>(query.offset)),
            result,
            boost::asio::use_awaitable
        );

        auto claim_statement = co_await connection->async_prepare_statement(claim_pending_songs_sql_, boost::asio::use_awaitable);
        for (boost::mysql::row_view row : result.rows()) {
            auto target = mapSingerTargetRow(row);
            boost::mysql::results claim_result;
            co_await connection->async_execute(
                claim_statement.bind(target.source, target.source_singer_id, statuses[0], statuses[1], statuses[2], statuses[3], statuses[4], stale_enabled, query.stale_after_seconds, query.force ? 1 : 0),
                claim_result,
                boost::asio::use_awaitable
            );
            if (claim_result.affected_rows() > 0) {
                target.sync_status = 1;
                claimed.push_back(std::move(target));
            }
        }
    } catch (const std::exception &e) {
        logSyncQueueError("coClaimPendingSongs", e);
    }
    co_return claimed;
}

boost::asio::awaitable<application::model::SingerSourceSyncPreviewResult> MySqlSingerSourceSyncQueue::coPreviewPendingSongs(application::model::SingerSourceSyncQuery query)
{
    query = normalizeQuery(std::move(query));
    application::model::SingerSourceSyncPreviewResult preview;
    const auto statuses = normalizedStatuses(query.sync_statuses, query.force);
    const int stale_enabled = query.include_stale_running ? 1 : 0;
    const int gender_filter = query.gender.value_or(-1);

    try {
        auto connection = co_await pool_->coAcquire();
        auto count_statement = co_await connection->async_prepare_statement(count_pending_songs_sql_, boost::asio::use_awaitable);
        boost::mysql::results result;
        co_await connection->async_execute(
            count_statement.bind(statuses[0], statuses[1], statuses[2], statuses[3], statuses[4], stale_enabled, query.stale_after_seconds, query.source, query.country, query.country, gender_filter, gender_filter, query.keyword, query.keyword, query.keyword),
            result,
            boost::asio::use_awaitable
        );
        mapPreviewCounts(result, preview);

        auto select_statement = co_await connection->async_prepare_statement(select_pending_songs_sql_, boost::asio::use_awaitable);
        co_await connection->async_execute(
            select_statement.bind(query.source, statuses[0], statuses[1], statuses[2], statuses[3], statuses[4], stale_enabled, query.stale_after_seconds, query.country, query.country, gender_filter, gender_filter, query.keyword, query.keyword, query.keyword, static_cast<std::uint64_t>(query.limit), static_cast<std::uint64_t>(query.offset)),
            result,
            boost::asio::use_awaitable
        );
        preview.items.reserve(result.rows().size());
        for (boost::mysql::row_view row : result.rows()) {
            preview.items.push_back(mapSingerTargetRow(row));
        }
    } catch (const std::exception &e) {
        logSyncQueueError("coPreviewPendingSongs", e);
    }
    co_return preview;
}

boost::asio::awaitable<std::vector<application::model::SingerSourceSyncTarget>> MySqlSingerSourceSyncQueue::coClaimPendingAlbums(application::model::SingerSourceSyncQuery query)
{
    query = normalizeQuery(std::move(query));
    std::vector<application::model::SingerSourceSyncTarget> claimed;
    const auto statuses = normalizedStatuses(query.sync_statuses, query.force);
    const int stale_enabled = query.include_stale_running ? 1 : 0;
    const int gender_filter = query.gender.value_or(-1);

    try {
        auto connection = co_await pool_->coAcquire();
        auto select_statement = co_await connection->async_prepare_statement(select_pending_albums_sql_, boost::asio::use_awaitable);
        boost::mysql::results result;
        co_await connection->async_execute(
            select_statement.bind(query.source, statuses[0], statuses[1], statuses[2], statuses[3], statuses[4], stale_enabled, query.stale_after_seconds, query.country, query.country, gender_filter, gender_filter, query.keyword, query.keyword, query.keyword, static_cast<std::uint64_t>(query.limit), static_cast<std::uint64_t>(query.offset)),
            result,
            boost::asio::use_awaitable
        );

        auto claim_statement = co_await connection->async_prepare_statement(claim_pending_albums_sql_, boost::asio::use_awaitable);
        for (boost::mysql::row_view row : result.rows()) {
            auto target = mapSingerTargetRow(row);
            boost::mysql::results claim_result;
            co_await connection->async_execute(
                claim_statement.bind(target.source, target.source_singer_id, statuses[0], statuses[1], statuses[2], statuses[3], statuses[4], stale_enabled, query.stale_after_seconds, query.force ? 1 : 0),
                claim_result,
                boost::asio::use_awaitable
            );
            if (claim_result.affected_rows() > 0) {
                target.sync_status = 1;
                claimed.push_back(std::move(target));
            }
        }
    } catch (const std::exception &e) {
        logSyncQueueError("coClaimPendingAlbums", e);
    }
    co_return claimed;
}

boost::asio::awaitable<application::model::SingerSourceSyncPreviewResult> MySqlSingerSourceSyncQueue::coPreviewPendingAlbums(application::model::SingerSourceSyncQuery query)
{
    query = normalizeQuery(std::move(query));
    application::model::SingerSourceSyncPreviewResult preview;
    const auto statuses = normalizedStatuses(query.sync_statuses, query.force);
    const int stale_enabled = query.include_stale_running ? 1 : 0;
    const int gender_filter = query.gender.value_or(-1);

    try {
        auto connection = co_await pool_->coAcquire();
        auto count_statement = co_await connection->async_prepare_statement(count_pending_albums_sql_, boost::asio::use_awaitable);
        boost::mysql::results result;
        co_await connection->async_execute(
            count_statement.bind(statuses[0], statuses[1], statuses[2], statuses[3], statuses[4], stale_enabled, query.stale_after_seconds, query.source, query.country, query.country, gender_filter, gender_filter, query.keyword, query.keyword, query.keyword),
            result,
            boost::asio::use_awaitable
        );
        mapPreviewCounts(result, preview);

        auto select_statement = co_await connection->async_prepare_statement(select_pending_albums_sql_, boost::asio::use_awaitable);
        co_await connection->async_execute(
            select_statement.bind(query.source, statuses[0], statuses[1], statuses[2], statuses[3], statuses[4], stale_enabled, query.stale_after_seconds, query.country, query.country, gender_filter, gender_filter, query.keyword, query.keyword, query.keyword, static_cast<std::uint64_t>(query.limit), static_cast<std::uint64_t>(query.offset)),
            result,
            boost::asio::use_awaitable
        );
        preview.items.reserve(result.rows().size());
        for (boost::mysql::row_view row : result.rows()) {
            preview.items.push_back(mapSingerTargetRow(row));
        }
    } catch (const std::exception &e) {
        logSyncQueueError("coPreviewPendingAlbums", e);
    }
    co_return preview;
}

boost::asio::awaitable<bool> MySqlSingerSourceSyncQueue::coUpdateSongsSyncStatus(std::string source, std::string source_singer_id, int status, int count, std::string error)
{
    try {
        auto connection = co_await pool_->coAcquire();
        auto statement = co_await connection->async_prepare_statement(update_songs_sync_sql_, boost::asio::use_awaitable);
        boost::mysql::results result;
        co_await connection->async_execute(statement.bind(status, status, count, std::move(error), std::move(source), std::move(source_singer_id)), result, boost::asio::use_awaitable);
        co_return result.affected_rows() > 0;
    } catch (const std::exception &e) {
        logSyncQueueError("coUpdateSongsSyncStatus", e);
    }
    co_return false;
}

boost::asio::awaitable<bool> MySqlSingerSourceSyncQueue::coUpdateAlbumsSyncStatus(std::string source, std::string source_singer_id, int status, int count, std::string error)
{
    try {
        auto connection = co_await pool_->coAcquire();
        auto statement = co_await connection->async_prepare_statement(update_albums_sync_sql_, boost::asio::use_awaitable);
        boost::mysql::results result;
        co_await connection->async_execute(statement.bind(status, status, count, std::move(error), std::move(source), std::move(source_singer_id)), result, boost::asio::use_awaitable);
        co_return result.affected_rows() > 0;
    } catch (const std::exception &e) {
        logSyncQueueError("coUpdateAlbumsSyncStatus", e);
    }
    co_return false;
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
