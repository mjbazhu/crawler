#include "crawler/infrastructure/music/persistence/mysql/MySqlAlbumSourceWriter.h"

#include "crawler/infrastructure/music/persistence/mysql/MySqlTransactionRunner.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlWriteRunner.h"
#include "crawler/infrastructure/music/persistence/sql/SqlFileLoader.h"

#include <boost/asio/use_awaitable.hpp>
#include <boost/mysql/results.hpp>

#include <exception>
#include <iostream>
#include <stdexcept>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {

namespace {

namespace sql_loader = infrastructure::music::persistence::sql;

// 判断专辑来源是否具备幂等入库所需的最小业务键。
bool isValidAlbumSource(const application::model::AlbumSourceMetadata &album)
{
    return !album.source.empty() && !album.source_album_id.empty() && !album.title.empty();
}

} // namespace

MySqlAlbumSourceWriter::MySqlAlbumSourceWriter(std::shared_ptr<MySqlConnectionPool> pool) : MySqlAlbumSourceWriter(std::move(pool), MySqlAlbumSourceSqlPaths {})
{}

MySqlAlbumSourceWriter::MySqlAlbumSourceWriter(std::shared_ptr<MySqlConnectionPool> pool, MySqlAlbumSourceSqlPaths paths) : pool_(std::move(pool)),
                                                                                                                            begin_sql_(MySqlTransactionRunner::loadStatementsOrThrow(paths.transaction_begin, "MySqlAlbumSourceWriter")),
                                                                                                                            commit_sql_(MySqlTransactionRunner::loadStatementsOrThrow(paths.transaction_commit, "MySqlAlbumSourceWriter")),
                                                                                                                            rollback_sql_(MySqlTransactionRunner::loadStatementsOrThrow(paths.transaction_rollback, "MySqlAlbumSourceWriter")),
                                                                                                                            set_album_vars_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.set_album_source_vars)),
                                                                                                                            insert_album_sql_(MySqlTransactionRunner::loadStatementsOrThrow(paths.insert_album_source, "MySqlAlbumSourceWriter"))
{
    if (!pool_) {
        throw std::invalid_argument("[MySqlAlbumSourceWriter] pool must not be null");
    }
}

bool MySqlAlbumSourceWriter::saveBatch(const std::vector<application::model::AlbumSourceMetadata> &albums)
{
    return MySqlWriteRunner::waitForWrite(pool_->executor(), coSaveBatch(albums), "MySqlAlbumSourceWriter", "saveBatch");
}

boost::asio::awaitable<bool> MySqlAlbumSourceWriter::coSaveBatch(std::vector<application::model::AlbumSourceMetadata> albums)
{
    if (albums.empty()) {
        co_return false;
    }

    auto connection = co_await pool_->coAcquire();
    bool transaction_started = false;
    std::string failure_message;

    try {
        boost::mysql::results result;
        co_await MySqlTransactionRunner::executeStatements(connection, begin_sql_);
        transaction_started = true;

        auto set_statement = co_await connection->async_prepare_statement(set_album_vars_sql_, boost::asio::use_awaitable);
        for (const auto &album : albums) {
            if (!isValidAlbumSource(album)) {
                continue;
            }

            co_await connection->async_execute(
                set_statement.bind(
                    album.source,
                    album.title,
                    album.normalized_title.empty() ? album.title : album.normalized_title,
                    album.artist_name,
                    album.source_item_key.empty() ? "album:" + album.source_album_id : album.source_item_key,
                    album.source_album_id,
                    album.source_material_id,
                    album.source_resource_type,
                    album.source_res_type,
                    album.source_action,
                    album.source_album_kind,
                    album.source_title.empty() ? album.title : album.source_title,
                    album.source_artist_id,
                    album.source_artist_name.empty() ? album.artist_name : album.source_artist_name,
                    album.cover_url,
                    album.summary,
                    album.price,
                    album.total_count,
                    album.release_date,
                    album.publish_date,
                    album.issue_date,
                    album.online_date,
                    album.copyright_id,
                    album.source_item_id,
                    album.source_campaign_id,
                    album.presale_date,
                    album.first_start_date,
                    album.first_end_date,
                    album.sale_end_date,
                    album.invalidate_date,
                    album.original_first_start_date,
                    album.original_first_end_date,
                    album.digital_period_status,
                    album.vip_type,
                    album.vip_flag,
                    album.vip_start_time,
                    album.vip_end_time,
                    album.effective_status,
                    album.convert_status,
                    album.forever_listen_flag,
                    album.forever_listen,
                    album.is_before_sale_end,
                    album.is_in_first_date,
                    album.is_call_payment,
                    album.is_call_payment_for_present,
                    album.is_call_payment_for_purchase,
                    album.is_knowledge_paid,
                    album.have_filter_tag,
                    album.is_convert,
                    album.is_appointment,
                    album.play_num,
                    album.play_num_desc,
                    album.detail_sync_status
                ),
                result,
                boost::asio::use_awaitable
            );

            co_await MySqlTransactionRunner::executeStatements(connection, insert_album_sql_);
        }

        co_await MySqlTransactionRunner::executeStatements(connection, commit_sql_);
        co_return true;
    } catch (const std::exception &e) {
        failure_message = e.what();
    }

    if (transaction_started) {
        co_await MySqlTransactionRunner::rollbackQuietly(connection, rollback_sql_, "MySqlAlbumSourceWriter");
    }

    if (!failure_message.empty()) {
        std::cerr << "[MySqlAlbumSourceWriter] coSaveBatch failed: " << failure_message << std::endl;
    }
    co_return false;
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
