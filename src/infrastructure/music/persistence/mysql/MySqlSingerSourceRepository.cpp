#include "crawler/infrastructure/music/persistence/mysql/MySqlSingerSourceRepository.h"

#include "crawler/infrastructure/music/persistence/mysql/MySqlTransactionRunner.h"
#include "crawler/application/use_cases/MetadataBatchBuilder.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlValueReader.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlWriteRunner.h"
#include "crawler/infrastructure/music/persistence/sql/SqlFileLoader.h"

#include <boost/asio/redirect_error.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/mysql/error_code.hpp>
#include <boost/mysql/results.hpp>
#include <boost/mysql/statement.hpp>

#include <algorithm>
#include <cstddef>
#include <iostream>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {

namespace {

namespace sql_loader = orchestrator::modules::crawler::infrastructure::music::persistence::sql;

// 歌手拼音批量回填前按平台歌手 ID 去重，避免同一页重复更新。
std::vector<application::model::SingerSourceNamePinyinPatch> normalizeNamePinyinPatches(std::vector<application::model::SingerSourceNamePinyinPatch> patches)
{
    patches.erase(
        std::remove_if(patches.begin(), patches.end(), [](const application::model::SingerSourceNamePinyinPatch &patch) {
            return patch.source.empty() || patch.source_singer_id.empty() || patch.source_name_pinyin.empty();
        }),
        patches.end()
    );
    std::sort(patches.begin(), patches.end(), [](const auto &left, const auto &right) {
        return std::tie(left.source, left.source_singer_id, left.source_name_pinyin) < std::tie(right.source, right.source_singer_id, right.source_name_pinyin);
    });
    patches.erase(
        std::unique(patches.begin(), patches.end(), [](const auto &left, const auto &right) {
            return left.source == right.source && left.source_singer_id == right.source_singer_id;
        }),
        patches.end()
    );
    return patches;
}

} // namespace

MySqlSingerSourceRepository::MySqlSingerSourceRepository(std::shared_ptr<MySqlConnectionPool> pool) : MySqlSingerSourceRepository(std::move(pool), SqlPaths {})
{}

MySqlSingerSourceRepository::MySqlSingerSourceRepository(std::shared_ptr<MySqlConnectionPool> pool, SqlPaths paths) : pool_(std::move(pool)),
                                                                                                                      writer_(pool_),
                                                                                                                      begin_sql_(MySqlTransactionRunner::loadStatementsOrThrow(paths.transaction_begin, "MySqlSingerSourceRepository")),
                                                                                                                      commit_sql_(MySqlTransactionRunner::loadStatementsOrThrow(paths.transaction_commit, "MySqlSingerSourceRepository")),
                                                                                                                      rollback_sql_(MySqlTransactionRunner::loadStatementsOrThrow(paths.transaction_rollback, "MySqlSingerSourceRepository")),
                                                                                                                      update_singer_profile_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.update_singer_source_profile)),
                                                                                                                      update_name_pinyin_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.update_singer_name_pinyin))
{
    if (!pool_) {
        throw std::invalid_argument("[MySqlSingerSourceRepository] pool must not be null");
    }
}

bool MySqlSingerSourceRepository::saveBatch(const std::vector<application::model::SingerSourceMetadata> &singers)
{
    return MySqlWriteRunner::waitForWrite(pool_->executor(), coSaveBatch(singers), "MySqlSingerSourceRepository", "saveBatch");
}

bool MySqlSingerSourceRepository::updateProfileCounts(const application::model::SingerSourceProfile &profile)
{
    return MySqlWriteRunner::waitForWrite(pool_->executor(), coUpdateProfileCounts(profile), "MySqlSingerSourceRepository", "updateProfileCounts");
}

bool MySqlSingerSourceRepository::updateNamePinyinBatch(const std::vector<application::model::SingerSourceNamePinyinPatch> &patches)
{
    return MySqlWriteRunner::waitForWrite(pool_->executor(), coUpdateNamePinyinBatch(patches), "MySqlSingerSourceRepository", "updateNamePinyinBatch");
}

boost::asio::awaitable<bool> MySqlSingerSourceRepository::coSaveBatch(std::vector<application::model::SingerSourceMetadata> singers)
{
    if (singers.empty()) co_return true;
    application::model::MetadataWriteBatch batch;
    batch.singers = std::move(singers);
    const auto result = co_await writer_.coSaveBatch(application::use_cases::buildMetadataWriteBatch(std::move(batch)));
    co_return result.isSuccess();
}

boost::asio::awaitable<bool> MySqlSingerSourceRepository::coUpdateProfileCounts(application::model::SingerSourceProfile profile)
{
    if (profile.source.empty() || profile.source_singer_id.empty() || (!profile.source_song_total && !profile.source_album_total)) {
        co_return false;
    }

    auto connection = co_await pool_->coAcquire();
    auto statement = co_await connection->async_prepare_statement(update_singer_profile_sql_, boost::asio::use_awaitable);
    boost::mysql::results result;
    co_await connection->async_execute(
        statement.bind(
            profile.source_song_total ? 1 : 0,
            profile.source_song_total.value_or(0),
            profile.source_album_total ? 1 : 0,
            profile.source_album_total.value_or(0),
            profile.source,
            profile.source_singer_id
        ),
        result,
        boost::asio::use_awaitable
    );
    co_return result.affected_rows() > 0;
}

boost::asio::awaitable<bool> MySqlSingerSourceRepository::coUpdateNamePinyinBatch(std::vector<application::model::SingerSourceNamePinyinPatch> patches)
{
    auto normalized = normalizeNamePinyinPatches(std::move(patches));
    if (normalized.empty()) {
        co_return true;
    }

    auto connection = co_await pool_->coAcquire();
    bool transaction_started = false;
    std::string failure_message;
    try {
        co_await MySqlTransactionRunner::executeStatements(connection, begin_sql_);
        transaction_started = true;

        auto statement = co_await connection->async_prepare_statement(update_name_pinyin_sql_, boost::asio::use_awaitable);
        boost::mysql::results result;
        for (const auto &patch : normalized) {
            co_await connection->async_execute(
                statement.bind(
                    patch.source_name_pinyin,
                    patch.source,
                    patch.source_singer_id
                ),
                result,
                boost::asio::use_awaitable
            );
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
        std::cerr << "[MySqlSingerSourceRepository] coUpdateNamePinyinBatch failed: " << failure_message << std::endl;
    }
    co_return false;
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
