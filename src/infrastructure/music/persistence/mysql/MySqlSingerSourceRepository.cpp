#include "crawler/infrastructure/music/persistence/mysql/MySqlSingerSourceRepository.h"

#include "crawler/infrastructure/music/persistence/mysql/MySqlTransactionRunner.h"
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

constexpr std::size_t kSingerSaveChunkSize = 100;

// 判断歌手来源是否有足够的唯一标识和名称。
bool isValidSingerSource(const application::model::SingerSourceMetadata &singer)
{
    return !singer.source.empty() && !singer.name.empty() && !singer.source_singer_id.empty();
}

// 歌手来源唯一键，用于单批去重和稳定排序。
std::string singerSourceKey(const application::model::SingerSourceMetadata &singer)
{
    return singer.source + '\n' + singer.source_singer_id;
}

// 单批内按固定顺序写入，减少多个分类并发时的锁顺序差异。
std::vector<application::model::SingerSourceMetadata> normalizeSingerBatch(std::vector<application::model::SingerSourceMetadata> singers)
{
    singers.erase(
        std::remove_if(singers.begin(), singers.end(), [](const application::model::SingerSourceMetadata &singer) {
            return !isValidSingerSource(singer);
        }),
        singers.end()
    );
    std::sort(singers.begin(), singers.end(), [](const application::model::SingerSourceMetadata &left, const application::model::SingerSourceMetadata &right) {
        const std::string left_name = left.normalized_name.empty() ? left.name : left.normalized_name;
        const std::string right_name = right.normalized_name.empty() ? right.name : right.normalized_name;
        return std::tie(left.source, left.source_singer_id, left_name, left.source_index_order) < std::tie(right.source, right.source_singer_id, right_name, right.source_index_order);
    });
    singers.erase(
        std::unique(singers.begin(), singers.end(), [](const application::model::SingerSourceMetadata &left, const application::model::SingerSourceMetadata &right) {
            return singerSourceKey(left) == singerSourceKey(right);
        }),
        singers.end()
    );
    return singers;
}

// MySQL 并发写入可恢复错误，允许当前分片重试。
bool isRetryableSingerPersistenceFailure(const std::string &message)
{
    return message.find("er_lock_deadlock") != std::string::npos ||
           message.find("1213") != std::string::npos ||
           message.find("Deadlock") != std::string::npos ||
           message.find("deadlock") != std::string::npos ||
           message.find("er_lock_wait_timeout") != std::string::npos ||
           message.find("1205") != std::string::npos ||
           message.find("singer source write lock") != std::string::npos;
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
    auto ordered_singers = normalizeSingerBatch(std::move(singers));
    if (ordered_singers.empty()) {
        co_return true;
    }

    bool all_saved = true;
    for (std::size_t first = 0; first < ordered_singers.size(); first += kSingerSaveChunkSize) {
        const std::size_t last = (std::min)(ordered_singers.size(), first + kSingerSaveChunkSize);
        if (co_await coSaveChunk(ordered_singers, first, last, 5, "chunk")) {
            continue;
        }

        bool chunk_saved_by_rows = true;
        for (std::size_t row = first; row < last; ++row) {
            if (co_await coSaveChunk(ordered_singers, row, row + 1, 5, "row")) {
                continue;
            }
            chunk_saved_by_rows = false;
            const auto &singer = ordered_singers[row];
            std::cerr << "[MySqlSingerSourceRepository] failed to save singer source: source="
                      << singer.source
                      << ", source_singer_id=" << singer.source_singer_id
                      << ", name=" << singer.name << std::endl;
        }
        all_saved = all_saved && chunk_saved_by_rows;
    }

    co_return all_saved;
}

boost::asio::awaitable<bool> MySqlSingerSourceRepository::coSaveChunk(
    const std::vector<application::model::SingerSourceMetadata> &singers,
    std::size_t first,
    std::size_t last,
    int max_attempts,
    std::string_view context
)
{
    std::string failure_message;
    const int attempts = (std::max)(1, max_attempts);
    for (int attempt = 0; attempt < attempts; ++attempt) {
        auto connection = co_await pool_->coAcquire();
        bool locked = false;
        bool transaction_started = false;
        failure_message.clear();

        try {
            boost::mysql::results result;
            co_await connection->async_execute("SELECT GET_LOCK('crawler_migu_singer_source_write', 30)", result, boost::asio::use_awaitable);
            locked = !result.rows().empty() && MySqlValueReader::intAt(result.rows().front(), 0) == 1;
            if (!locked) {
                throw std::runtime_error("failed to acquire singer source write lock");
            }

            co_await MySqlTransactionRunner::executeStatements(connection, begin_sql_);
            transaction_started = true;

            auto set_singer_vars_statement = co_await connection->async_prepare_statement(set_singer_vars_sql_, boost::asio::use_awaitable);

            for (std::size_t index = first; index < last; ++index) {
                const auto &singer = singers[index];
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
                        singer.avatar_url,
                        singer.source_index_tab,
                        singer.source_index_order
                    ),
                    result,
                    boost::asio::use_awaitable
                );

                co_await MySqlTransactionRunner::executeStatements(connection, insert_singer_sql_);
            }

            co_await MySqlTransactionRunner::executeStatements(connection, commit_sql_);
            transaction_started = false;
            boost::mysql::error_code ignored_error;
            co_await connection->async_execute("SELECT RELEASE_LOCK('crawler_migu_singer_source_write')", result, boost::asio::redirect_error(boost::asio::use_awaitable, ignored_error));
            co_return true;
        } catch (const std::exception &e) {
            failure_message = e.what();
        }

        if (transaction_started) {
            co_await MySqlTransactionRunner::rollbackQuietly(connection, rollback_sql_, "MySqlSingerSourceRepository");
        }
        if (locked) {
            boost::mysql::results ignored_result;
            boost::mysql::error_code ignored_error;
            co_await connection->async_execute("SELECT RELEASE_LOCK('crawler_migu_singer_source_write')", ignored_result, boost::asio::redirect_error(boost::asio::use_awaitable, ignored_error));
        }

        if (!isRetryableSingerPersistenceFailure(failure_message) || attempt + 1 >= attempts) {
            break;
        }
        std::cerr << "[MySqlSingerSourceRepository] retrying saveBatch after transient failure: context="
                  << context
                  << ", attempt=" << (attempt + 1) << "/" << attempts
                  << ", range=" << first << "-" << last
                  << ", error=" << failure_message << std::endl;
    }

    if (!failure_message.empty()) {
        std::cerr << "[MySqlSingerSourceRepository] coSaveBatch failed: context="
                  << context
                  << ", range=" << first << "-" << last
                  << ", error=" << failure_message << std::endl;
    }
    co_return false;
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
