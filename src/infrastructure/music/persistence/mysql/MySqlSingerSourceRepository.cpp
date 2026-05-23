#include "crawler/infrastructure/music/persistence/mysql/MySqlSingerSourceRepository.h"

#include "crawler/infrastructure/music/persistence/mysql/MySqlTransactionRunner.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlWriteRunner.h"
#include "crawler/infrastructure/music/persistence/sql/SqlFileLoader.h"

#include <boost/asio/use_awaitable.hpp>
#include <boost/mysql/results.hpp>
#include <boost/mysql/statement.hpp>

#include <iostream>
#include <stdexcept>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {

namespace {

namespace sql_loader = orchestrator::modules::crawler::infrastructure::music::persistence::sql;

// 判断歌手来源是否有足够的唯一标识和名称。
bool isValidSingerSource(const application::model::SingerSourceMetadata &singer)
{
    return !singer.source.empty() && !singer.name.empty() && !singer.source_singer_id.empty();
}

} // namespace

MySqlSingerSourceRepository::MySqlSingerSourceRepository(std::shared_ptr<MySqlConnectionPool> pool) : MySqlSingerSourceRepository(std::move(pool), SqlPaths {})
{}

MySqlSingerSourceRepository::MySqlSingerSourceRepository(std::shared_ptr<MySqlConnectionPool> pool, SqlPaths paths) : pool_(std::move(pool)),
                                                                                                                      begin_sql_(MySqlTransactionRunner::loadStatementsOrThrow(paths.transaction_begin, "MySqlSingerSourceRepository")),
                                                                                                                      commit_sql_(MySqlTransactionRunner::loadStatementsOrThrow(paths.transaction_commit, "MySqlSingerSourceRepository")),
                                                                                                                      rollback_sql_(MySqlTransactionRunner::loadStatementsOrThrow(paths.transaction_rollback, "MySqlSingerSourceRepository")),
                                                                                                                      set_singer_vars_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.set_singer_source_vars)),
                                                                                                                      insert_singer_sql_(MySqlTransactionRunner::loadStatementsOrThrow(paths.insert_singer_source, "MySqlSingerSourceRepository"))
{
    if (!pool_) {
        throw std::invalid_argument("[MySqlSingerSourceRepository] pool must not be null");
    }
}

bool MySqlSingerSourceRepository::saveBatch(const std::vector<application::model::SingerSourceMetadata> &singers)
{
    return MySqlWriteRunner::waitForWrite(pool_->executor(), coSaveBatch(singers), "MySqlSingerSourceRepository", "saveBatch");
}

boost::asio::awaitable<bool> MySqlSingerSourceRepository::coSaveBatch(std::vector<application::model::SingerSourceMetadata> singers)
{
    if (singers.empty()) {
        co_return true;
    }

    auto connection = co_await pool_->coAcquire();
    bool transaction_started = false;
    std::string failure_message;

    try {
        co_await MySqlTransactionRunner::executeStatements(connection, begin_sql_);
        transaction_started = true;

        auto set_singer_vars_statement = co_await connection->async_prepare_statement(set_singer_vars_sql_, boost::asio::use_awaitable);
        boost::mysql::results result;

        for (const auto &singer : singers) {
            if (!isValidSingerSource(singer)) {
                continue;
            }

            co_await connection->async_execute(
                set_singer_vars_statement.bind(
                    singer.source,
                    singer.name,
                    singer.normalized_name.empty() ? singer.name : singer.normalized_name,
                    static_cast<int>(singer.gender),
                    singer.source_item_key,
                    singer.source_singer_id,
                    singer.source_view_id,
                    singer.source_res_type,
                    singer.source_action,
                    singer.source_track,
                    singer.source_display_id,
                    singer.source_name_pinyin,
                    singer.source_tag,
                    singer.source_metric,
                    singer.source_country,
                    singer.avatar_url
                ),
                result,
                boost::asio::use_awaitable
            );

            co_await MySqlTransactionRunner::executeStatements(connection, insert_singer_sql_);
        }

        co_await MySqlTransactionRunner::executeStatements(connection, commit_sql_);
        co_return true;
    } catch (const std::exception &e) {
        failure_message = e.what();
    }

    if (transaction_started) {
        co_await MySqlTransactionRunner::rollbackQuietly(connection, rollback_sql_, "MySqlSingerSourceRepository");
    }

    if (!failure_message.empty()) {
        std::cerr << "[MySqlSingerSourceRepository] coSaveBatch failed: " << failure_message << std::endl;
    }
    co_return false;
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
