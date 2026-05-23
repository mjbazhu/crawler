#include "crawler/infrastructure/music/persistence/mysql/MySqlMusicCatalogQueryRepository.h"

#include <boost/asio/use_awaitable.hpp>
#include <boost/mysql/results.hpp>

#include <algorithm>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {

namespace {

// 规整超时秒数，避免误传 0 导致批量恢复所有 running 记录。
int normalizeStaleSeconds(int stale_after_seconds)
{
    return (std::max)(1, stale_after_seconds);
}

// 执行单条恢复 SQL，返回受影响记录数。
boost::asio::awaitable<int> recoverStale(
    const std::shared_ptr<MySqlConnectionPool> &pool,
    const std::string &sql,
    std::string source,
    int stale_after_seconds,
    std::string error
)
{
    auto connection = co_await pool->coAcquire();
    auto statement = co_await connection->async_prepare_statement(sql, boost::asio::use_awaitable);
    boost::mysql::results result;
    co_await connection->async_execute(statement.bind(std::move(error), std::move(source), normalizeStaleSeconds(stale_after_seconds)), result, boost::asio::use_awaitable);
    co_return static_cast<int>(result.affected_rows());
}

} // namespace

boost::asio::awaitable<int> MySqlMusicCatalogQueryRepository::coRecoverStaleSingerSongs(std::string source, int stale_after_seconds, std::string error)
{
    co_return co_await recoverStale(pool_, recover_singer_songs_sql_, std::move(source), stale_after_seconds, std::move(error));
}

boost::asio::awaitable<int> MySqlMusicCatalogQueryRepository::coRecoverStaleSingerAlbums(std::string source, int stale_after_seconds, std::string error)
{
    co_return co_await recoverStale(pool_, recover_singer_albums_sql_, std::move(source), stale_after_seconds, std::move(error));
}

boost::asio::awaitable<int> MySqlMusicCatalogQueryRepository::coRecoverStaleAlbumDetails(std::string source, int stale_after_seconds, std::string error)
{
    co_return co_await recoverStale(pool_, recover_album_details_sql_, std::move(source), stale_after_seconds, std::move(error));
}

boost::asio::awaitable<int> MySqlMusicCatalogQueryRepository::coRecoverStaleAlbumSongs(std::string source, int stale_after_seconds, std::string error)
{
    co_return co_await recoverStale(pool_, recover_album_songs_sql_, std::move(source), stale_after_seconds, std::move(error));
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
