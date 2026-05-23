#include "crawler/infrastructure/music/persistence/mysql/MySqlAlbumSourceSyncQueue.h"

#include "crawler/infrastructure/music/persistence/mysql/MySqlValueReader.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlWriteRunner.h"
#include "crawler/infrastructure/music/persistence/sql/SqlFileLoader.h"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/mysql/results.hpp>
#include <boost/mysql/row_view.hpp>

#include <cstdint>
#include <exception>
#include <future>
#include <iostream>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {

namespace {

namespace sql_loader = infrastructure::music::persistence::sql;

// 在同步 port 边界阻塞等待 MySQL 查询完成，避免把 Asio 暴露到 application。
std::vector<application::model::AlbumSourceMetadata> waitForMysqlAlbumSources(
    boost::asio::any_io_executor executor,
    boost::asio::awaitable<std::vector<application::model::AlbumSourceMetadata>> task,
    std::string_view context
)
{
    auto future = boost::asio::co_spawn(std::move(executor), std::move(task), boost::asio::use_future);
    try {
        return future.get();
    } catch (const std::exception &e) {
        std::cerr << "[MySqlAlbumSourceSyncQueue] " << context << " failed: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "[MySqlAlbumSourceSyncQueue] " << context << " failed with unknown error" << std::endl;
    }
    return {};
}

// 记录同步队列异常，避免同步调试入口无错误上下文。
void logSyncQueueError(const std::string &context, const std::exception &error)
{
    std::cerr << "[MySqlAlbumSourceSyncQueue] " << context << " failed: " << error.what() << std::endl;
}

// 从待补齐详情查询结果映射专辑来源参数。
application::model::AlbumSourceMetadata mapPendingDetailRow(boost::mysql::row_view row)
{
    application::model::AlbumSourceMetadata album;
    album.source = MySqlValueReader::stringAt(row, 2);
    album.source_item_key = MySqlValueReader::stringAt(row, 3);
    album.source_album_id = MySqlValueReader::stringAt(row, 4);
    album.source_resource_type = MySqlValueReader::stringAt(row, 5);
    album.source_res_type = MySqlValueReader::stringAt(row, 6);
    album.source_action = MySqlValueReader::stringAt(row, 7);
    album.source_album_kind = MySqlValueReader::intAt(row, 8);
    album.source_title = MySqlValueReader::stringAt(row, 9);
    album.title = album.source_title;
    album.normalized_title = album.title;
    album.source_artist_id = MySqlValueReader::stringAt(row, 10);
    album.source_artist_name = MySqlValueReader::stringAt(row, 11);
    album.artist_name = album.source_artist_name;
    album.cover_url = MySqlValueReader::stringAt(row, 12);
    album.detail_sync_status = MySqlValueReader::intAt(row, 13);
    return album;
}

// 从待获取歌曲查询结果映射专辑来源参数。
application::model::AlbumSourceMetadata mapPendingSongsRow(boost::mysql::row_view row)
{
    application::model::AlbumSourceMetadata album;
    album.source = MySqlValueReader::stringAt(row, 2);
    album.source_item_key = MySqlValueReader::stringAt(row, 3);
    album.source_album_id = MySqlValueReader::stringAt(row, 4);
    album.source_resource_type = MySqlValueReader::stringAt(row, 5);
    album.source_res_type = MySqlValueReader::stringAt(row, 6);
    album.source_action = MySqlValueReader::stringAt(row, 7);
    album.source_album_kind = MySqlValueReader::intAt(row, 8);
    album.source_title = MySqlValueReader::stringAt(row, 9);
    album.title = album.source_title;
    album.normalized_title = album.title;
    album.source_artist_id = MySqlValueReader::stringAt(row, 10);
    album.source_artist_name = MySqlValueReader::stringAt(row, 11);
    album.artist_name = album.source_artist_name;
    album.cover_url = MySqlValueReader::stringAt(row, 12);
    album.total_count = MySqlValueReader::stringAt(row, 13);
    album.detail_sync_status = MySqlValueReader::intAt(row, 14);
    return album;
}

} // namespace

MySqlAlbumSourceSyncQueue::MySqlAlbumSourceSyncQueue(std::shared_ptr<MySqlConnectionPool> pool) : MySqlAlbumSourceSyncQueue(std::move(pool), MySqlAlbumSourceSqlPaths {})
{}

MySqlAlbumSourceSyncQueue::MySqlAlbumSourceSyncQueue(std::shared_ptr<MySqlConnectionPool> pool, MySqlAlbumSourceSqlPaths paths) : pool_(std::move(pool)),
                                                                                                                                    update_detail_sync_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.update_detail_sync_status)),
                                                                                                                                    update_songs_sync_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.update_songs_sync_status)),
                                                                                                                                    select_pending_details_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.select_pending_details)),
                                                                                                                                    select_pending_songs_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.select_pending_songs))
{
    if (!pool_) {
        throw std::invalid_argument("[MySqlAlbumSourceSyncQueue] pool must not be null");
    }
}

std::vector<application::model::AlbumSourceMetadata> MySqlAlbumSourceSyncQueue::findPendingDetails(std::string source, std::size_t limit, std::size_t offset)
{
    return waitForMysqlAlbumSources(pool_->executor(), coFindPendingDetails(std::move(source), limit, offset), "findPendingDetails");
}

bool MySqlAlbumSourceSyncQueue::updateDetailSyncStatus(std::string source, std::string source_album_id, int status, std::string error)
{
    return MySqlWriteRunner::waitForWrite(pool_->executor(), coUpdateDetailSyncStatus(std::move(source), std::move(source_album_id), status, std::move(error)), "MySqlAlbumSourceSyncQueue", "updateDetailSyncStatus");
}

std::vector<application::model::AlbumSourceMetadata> MySqlAlbumSourceSyncQueue::findPendingSongs(std::string source, std::size_t limit, std::size_t offset)
{
    return waitForMysqlAlbumSources(pool_->executor(), coFindPendingSongs(std::move(source), limit, offset), "findPendingSongs");
}

bool MySqlAlbumSourceSyncQueue::updateSongsSyncStatus(std::string source, std::string source_album_id, int status, int count, std::string error)
{
    return MySqlWriteRunner::waitForWrite(pool_->executor(), coUpdateSongsSyncStatus(std::move(source), std::move(source_album_id), status, count, std::move(error)), "MySqlAlbumSourceSyncQueue", "updateSongsSyncStatus");
}

boost::asio::awaitable<std::vector<application::model::AlbumSourceMetadata>> MySqlAlbumSourceSyncQueue::coFindPendingDetails(std::string source, std::size_t limit, std::size_t offset)
{
    std::vector<application::model::AlbumSourceMetadata> albums;
    try {
        auto connection = co_await pool_->coAcquire();
        auto statement = co_await connection->async_prepare_statement(select_pending_details_sql_, boost::asio::use_awaitable);
        boost::mysql::results result;
        co_await connection->async_execute(statement.bind(std::move(source), static_cast<std::uint64_t>(limit), static_cast<std::uint64_t>(offset)), result, boost::asio::use_awaitable);
        albums.reserve(result.rows().size());
        for (boost::mysql::row_view row : result.rows()) {
            albums.push_back(mapPendingDetailRow(row));
        }
    } catch (const std::exception &e) {
        logSyncQueueError("coFindPendingDetails", e);
    }
    co_return albums;
}

boost::asio::awaitable<bool> MySqlAlbumSourceSyncQueue::coUpdateDetailSyncStatus(std::string source, std::string source_album_id, int status, std::string error)
{
    try {
        auto connection = co_await pool_->coAcquire();
        auto statement = co_await connection->async_prepare_statement(update_detail_sync_sql_, boost::asio::use_awaitable);
        boost::mysql::results result;
        co_await connection->async_execute(statement.bind(status, status, std::move(error), std::move(source), std::move(source_album_id)), result, boost::asio::use_awaitable);
        co_return result.affected_rows() > 0;
    } catch (const std::exception &e) {
        logSyncQueueError("coUpdateDetailSyncStatus", e);
    }
    co_return false;
}

boost::asio::awaitable<std::vector<application::model::AlbumSourceMetadata>> MySqlAlbumSourceSyncQueue::coFindPendingSongs(std::string source, std::size_t limit, std::size_t offset)
{
    std::vector<application::model::AlbumSourceMetadata> albums;
    try {
        auto connection = co_await pool_->coAcquire();
        auto statement = co_await connection->async_prepare_statement(select_pending_songs_sql_, boost::asio::use_awaitable);
        boost::mysql::results result;
        co_await connection->async_execute(statement.bind(std::move(source), static_cast<std::uint64_t>(limit), static_cast<std::uint64_t>(offset)), result, boost::asio::use_awaitable);
        albums.reserve(result.rows().size());
        for (boost::mysql::row_view row : result.rows()) {
            albums.push_back(mapPendingSongsRow(row));
        }
    } catch (const std::exception &e) {
        logSyncQueueError("coFindPendingSongs", e);
    }
    co_return albums;
}

boost::asio::awaitable<bool> MySqlAlbumSourceSyncQueue::coUpdateSongsSyncStatus(std::string source, std::string source_album_id, int status, int count, std::string error)
{
    try {
        auto connection = co_await pool_->coAcquire();
        auto statement = co_await connection->async_prepare_statement(update_songs_sync_sql_, boost::asio::use_awaitable);
        boost::mysql::results result;
        co_await connection->async_execute(statement.bind(status, status, count, std::move(error), std::move(source), std::move(source_album_id)), result, boost::asio::use_awaitable);
        co_return result.affected_rows() > 0;
    } catch (const std::exception &e) {
        logSyncQueueError("coUpdateSongsSyncStatus", e);
    }
    co_return false;
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
