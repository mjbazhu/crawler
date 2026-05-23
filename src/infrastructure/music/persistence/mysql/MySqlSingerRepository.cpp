#include "crawler/infrastructure/music/persistence/mysql/MySqlSingerRepository.h"

#include "crawler/infrastructure/music/persistence/mysql/MySqlValueReader.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlWriteRunner.h"
#include "crawler/infrastructure/music/persistence/sql/SqlFileLoader.h"

#include <boost/asio/use_awaitable.hpp>
#include <boost/mysql/results.hpp>
#include <boost/mysql/statement.hpp>

#include <ctime>
#include <iostream>
#include <string>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {

namespace {

// 当前 Unix 秒时间戳，用于保持与 SQLite 调试后端一致的 created_at/updated_at 语义。
std::uint64_t nowSeconds()
{
    return static_cast<std::uint64_t>(std::time(nullptr));
}

// 歌手公共表按 normalized_name 去重，平台来源唯一性仍由 singer_source_refs 负责。
std::string normalizedSingerName(const domain::Singer &singer)
{
    return singer.name;
}

} // namespace

MySqlSingerRepository::MySqlSingerRepository(std::shared_ptr<MySqlConnectionPool> pool, SqlPaths paths) : MySqlCrudRepository(std::move(pool)),
                                                                                                          upsert_sql_(sql::SqlFileLoader::loadOrThrow(paths.upsert)),
                                                                                                          delete_sql_(sql::SqlFileLoader::loadOrThrow(paths.delete_by_id)),
                                                                                                          find_sql_(sql::SqlFileLoader::loadOrThrow(paths.find_by_id)),
                                                                                                          find_all_sql_(sql::SqlFileLoader::loadOrThrow(paths.find_all))
{}

bool MySqlSingerRepository::saveBatch(const std::vector<domain::Singer> &singers)
{
    return MySqlWriteRunner::waitForWrite(pool_->executor(), coSaveBatch(singers), "MySqlSingerRepository", "saveBatch");
}

boost::asio::awaitable<bool> MySqlSingerRepository::coSaveBatch(std::vector<domain::Singer> singers)
{
    if (singers.empty()) {
        co_return true;
    }

    auto connection = co_await pool_->coAcquire();
    bool transaction_started = false;
    std::string failure_message;
    try {
        boost::mysql::results result;
        co_await connection->async_execute("START TRANSACTION", result, boost::asio::use_awaitable);
        transaction_started = true;

        auto statement = co_await connection->async_prepare_statement(upsert_sql_, boost::asio::use_awaitable);
        for (const auto &singer : singers) {
            const std::uint64_t now = nowSeconds();
            const std::uint64_t created_at = singer.created_at == 0 ? now : singer.created_at;
            const std::uint64_t updated_at = singer.updated_at == 0 ? now : singer.updated_at;
            co_await connection->async_execute(
                statement.bind(singer.name, normalizedSingerName(singer), static_cast<int>(singer.gender), created_at, updated_at),
                result,
                boost::asio::use_awaitable
            );
        }

        co_await connection->async_execute("COMMIT", result, boost::asio::use_awaitable);
        co_return true;
    } catch (const std::exception &e) {
        failure_message = e.what();
    }

    if (transaction_started) {
        try {
            boost::mysql::results rollback_result;
            co_await connection->async_execute("ROLLBACK", rollback_result, boost::asio::use_awaitable);
        } catch (const std::exception &rollback_error) {
            std::cerr << "[MySqlSingerRepository] rollback failed: " << rollback_error.what() << std::endl;
        }
    }

    if (!failure_message.empty()) {
        std::cerr << "[MySqlSingerRepository] coSaveBatch failed: " << failure_message << std::endl;
    }
    co_return false;
}

boost::asio::awaitable<bool> MySqlSingerRepository::coCreate(const domain::Singer &entity)
{
    try {
        auto connection = co_await pool_->coAcquire();
        auto statement = co_await connection->async_prepare_statement(upsert_sql_, boost::asio::use_awaitable);
        boost::mysql::results result;
        const std::uint64_t now = nowSeconds();
        const std::uint64_t created_at = entity.created_at == 0 ? now : entity.created_at;
        const std::uint64_t updated_at = entity.updated_at == 0 ? now : entity.updated_at;
        co_await connection->async_execute(
            statement.bind(entity.name, normalizedSingerName(entity), static_cast<int>(entity.gender), created_at, updated_at),
            result,
            boost::asio::use_awaitable
        );
        co_return hasAffectedRows(result);
    } catch (const std::exception &e) {
        std::cerr << "[MySqlSingerRepository] coCreate failed: " << e.what() << std::endl;
        co_return false;
    }
}

boost::asio::awaitable<bool> MySqlSingerRepository::coUpdate(const domain::Singer &entity)
{
    co_return co_await coCreate(entity);
}

boost::asio::awaitable<bool> MySqlSingerRepository::coDeleteById(const std::int64_t &id)
{
    try {
        auto connection = co_await pool_->coAcquire();
        auto statement = co_await connection->async_prepare_statement(delete_sql_, boost::asio::use_awaitable);
        boost::mysql::results result;
        co_await connection->async_execute(statement.bind(id), result, boost::asio::use_awaitable);
        co_return hasAffectedRows(result);
    } catch (const std::exception &e) {
        std::cerr << "[MySqlSingerRepository] coDeleteById failed: " << e.what() << std::endl;
        co_return false;
    }
}

boost::asio::awaitable<std::optional<domain::Singer>> MySqlSingerRepository::coFindById(const std::int64_t &id)
{
    try {
        auto connection = co_await pool_->coAcquire();
        auto statement = co_await connection->async_prepare_statement(find_sql_, boost::asio::use_awaitable);
        boost::mysql::results result;
        co_await connection->async_execute(statement.bind(id), result, boost::asio::use_awaitable);
        if (result.rows().empty()) {
            co_return std::nullopt;
        }
        co_return mapRow(result.rows().front());
    } catch (const std::exception &e) {
        std::cerr << "[MySqlSingerRepository] coFindById failed: " << e.what() << std::endl;
        co_return std::nullopt;
    }
}

boost::asio::awaitable<std::vector<domain::Singer>> MySqlSingerRepository::coFindAll(std::size_t limit, std::size_t offset)
{
    std::vector<domain::Singer> singers;
    try {
        auto connection = co_await pool_->coAcquire();
        auto statement = co_await connection->async_prepare_statement(find_all_sql_, boost::asio::use_awaitable);
        boost::mysql::results result;
        co_await connection->async_execute(statement.bind(static_cast<std::uint64_t>(limit), static_cast<std::uint64_t>(offset)), result, boost::asio::use_awaitable);
        singers.reserve(result.rows().size());
        for (boost::mysql::row_view row : result.rows()) {
            singers.push_back(mapRow(row));
        }
    } catch (const std::exception &e) {
        std::cerr << "[MySqlSingerRepository] coFindAll failed: " << e.what() << std::endl;
    }
    co_return singers;
}

domain::Singer MySqlSingerRepository::mapRow(boost::mysql::row_view row)
{
    domain::Singer singer;
    singer.singer_id = MySqlValueReader::int64At(row, 0);
    singer.name = MySqlValueReader::stringAt(row, 1);
    singer.gender = static_cast<domain::Gender>(MySqlValueReader::intAt(row, 3));
    singer.created_at = MySqlValueReader::uint64At(row, 4);
    singer.updated_at = MySqlValueReader::uint64At(row, 5);
    return singer;
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
