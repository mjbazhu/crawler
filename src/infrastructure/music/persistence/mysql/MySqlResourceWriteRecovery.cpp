#include "crawler/infrastructure/music/persistence/mysql/MySqlSongResourceQueue.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlValueReader.h"
#include "crawler/infrastructure/music/persistence/sql/SqlFileLoader.h"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/mysql/results.hpp>
#include <boost/mysql/statement.hpp>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {
namespace {
// 按行读取事实；未确认的同代次 running 文件继续保留，避免与活跃下载争用。
boost::asio::awaitable<std::optional<application::model::ResourceWriteDisposition>> inspectWrite(std::shared_ptr<MySqlConnectionPool> pool, std::uint64_t resource, std::uint64_t generation, std::string path)
{
    using State = application::model::ResourceWriteDisposition;
    try {
        auto connection = co_await pool->coAcquire();
        if (resource == 0) {
            for (const auto *table : {"singer_resources", "album_resources"}) {
                auto metadata = co_await connection->async_prepare_statement(sql::SqlFileLoader::loadOrThrow(std::string("sql/mysql/") + table + "/find_file_receipt.sql"), boost::asio::use_awaitable);
                boost::mysql::results result;
                co_await connection->async_execute(metadata.bind(path), result, boost::asio::use_awaitable);
                if (!result.rows().empty()) co_return State::Committed;
            }
            co_return State::Discard;
        }
        auto statement = co_await connection->async_prepare_statement(sql::SqlFileLoader::loadOrThrow("sql/mysql/song_resources/find_write_receipt.sql"), boost::asio::use_awaitable);
        boost::mysql::results result;
        co_await connection->async_execute(statement.bind(resource), result, boost::asio::use_awaitable);
        if (result.rows().empty()) co_return State::Discard;
        const auto row = result.rows().front();
        if (MySqlValueReader::intAt(row, 0) == 2 && MySqlValueReader::stringAt(row, 2) == path) co_return State::Committed;
        if (MySqlValueReader::intAt(row, 0) == 1 && MySqlValueReader::uint64At(row, 1) == generation) co_return State::Active;
        co_return State::Discard;
    } catch (...) {
        co_return std::nullopt;
    }
}
} // namespace

std::optional<application::model::ResourceWriteDisposition> MySqlSongResourceQueue::inspectResourceWrite(std::uint64_t resource, std::uint64_t generation, const std::string &path)
{
    return boost::asio::co_spawn(pool_->executor(), inspectWrite(pool_, resource, generation, path), boost::asio::use_future).get();
}
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
