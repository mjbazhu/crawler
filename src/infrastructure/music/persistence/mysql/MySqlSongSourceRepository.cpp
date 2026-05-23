#include "crawler/infrastructure/music/persistence/mysql/MySqlSongSourceRepository.h"

#include "crawler/infrastructure/music/persistence/mysql/MySqlTransactionRunner.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlWriteRunner.h"
#include "crawler/infrastructure/music/persistence/sql/SqlFileLoader.h"

#include <boost/asio/use_awaitable.hpp>
#include <boost/mysql/results.hpp>
#include <boost/mysql/statement.hpp>

#include <iostream>
#include <stdexcept>
#include <utility>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {

namespace {

namespace sql_loader = orchestrator::modules::crawler::infrastructure::music::persistence::sql;

// 判断歌曲来源是否有足够的唯一标识和标题。
bool isValidSongSource(const application::model::SongSourceMetadata &song)
{
    return !song.source.empty() && !song.title.empty() && (!song.source_content_id.empty() || !song.source_song_id.empty());
}

// 返回可入库的歌手关系；当 parser 没有返回 singerList 时使用主歌手字段兜底。
std::vector<application::model::SongSourceArtistRef> artistRefsForSong(const application::model::SongSourceMetadata &song)
{
    if (!song.artist_refs.empty()) {
        return song.artist_refs;
    }
    if (song.source_primary_artist_id.empty() && song.source_primary_artist_name.empty()) {
        return {};
    }

    application::model::SongSourceArtistRef artist_ref;
    artist_ref.source_artist_id = song.source_primary_artist_id;
    artist_ref.source_artist_name = song.source_primary_artist_name;
    artist_ref.artist_order = 1;
    artist_ref.is_primary = true;
    return {std::move(artist_ref)};
}

} // namespace

MySqlSongSourceRepository::MySqlSongSourceRepository(std::shared_ptr<MySqlConnectionPool> pool) : MySqlSongSourceRepository(std::move(pool), SqlPaths {})
{}

MySqlSongSourceRepository::MySqlSongSourceRepository(std::shared_ptr<MySqlConnectionPool> pool, SqlPaths paths) : pool_(std::move(pool)),
                                                                                                                  begin_sql_(MySqlTransactionRunner::loadStatementsOrThrow(paths.transaction_begin, "MySqlSongSourceRepository")),
                                                                                                                  commit_sql_(MySqlTransactionRunner::loadStatementsOrThrow(paths.transaction_commit, "MySqlSongSourceRepository")),
                                                                                                                  rollback_sql_(MySqlTransactionRunner::loadStatementsOrThrow(paths.transaction_rollback, "MySqlSongSourceRepository")),
                                                                                                                  set_song_vars_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.set_song_item_vars)),
                                                                                                                  insert_song_sql_(MySqlTransactionRunner::loadStatementsOrThrow(paths.insert_song_item, "MySqlSongSourceRepository")),
                                                                                                                  set_audio_vars_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.set_audio_format_vars)),
                                                                                                                  insert_audio_sql_(MySqlTransactionRunner::loadStatementsOrThrow(paths.insert_audio_format, "MySqlSongSourceRepository")),
                                                                                                                  set_artist_vars_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.set_artist_ref_vars)),
                                                                                                                  insert_artist_sql_(MySqlTransactionRunner::loadStatementsOrThrow(paths.insert_artist_ref, "MySqlSongSourceRepository")),
                                                                                                                  set_album_song_vars_sql_(sql_loader::SqlFileLoader::loadOrThrow(paths.set_album_song_ref_vars)),
                                                                                                                  insert_album_song_sql_(MySqlTransactionRunner::loadStatementsOrThrow(paths.insert_album_song_ref, "MySqlSongSourceRepository")),
                                                                                                                  insert_audio_placeholder_sql_(MySqlTransactionRunner::loadStatementsOrThrow(paths.insert_audio_placeholder, "MySqlSongSourceRepository"))
{
    if (!pool_) {
        throw std::invalid_argument("[MySqlSongSourceRepository] pool must not be null");
    }
}

bool MySqlSongSourceRepository::saveBatch(const std::vector<application::model::SongSourceMetadata> &songs)
{
    return MySqlWriteRunner::waitForWrite(pool_->executor(), coSaveBatch(songs), "MySqlSongSourceRepository", "saveBatch");
}

boost::asio::awaitable<bool> MySqlSongSourceRepository::coSaveBatch(std::vector<application::model::SongSourceMetadata> songs)
{
    if (songs.empty()) {
        co_return true;
    }

    auto connection = co_await pool_->coAcquire();
    bool transaction_started = false;
    std::string failure_message;

    try {
        co_await MySqlTransactionRunner::executeStatements(connection, begin_sql_);
        transaction_started = true;

        auto set_song_vars_statement = co_await connection->async_prepare_statement(set_song_vars_sql_, boost::asio::use_awaitable);
        auto set_audio_vars_statement = co_await connection->async_prepare_statement(set_audio_vars_sql_, boost::asio::use_awaitable);
        auto set_artist_vars_statement = co_await connection->async_prepare_statement(set_artist_vars_sql_, boost::asio::use_awaitable);
        auto set_album_song_vars_statement = co_await connection->async_prepare_statement(set_album_song_vars_sql_, boost::asio::use_awaitable);
        boost::mysql::results result;

        for (const auto &song : songs) {
            if (!isValidSongSource(song)) {
                continue;
            }

            co_await connection->async_execute(
                set_song_vars_statement.bind(
                    song.source,
                    song.source_song_id,
                    song.source_content_id,
                    song.source_resource_type,
                    song.title,
                    song.normalized_title.empty() ? song.title : song.normalized_title,
                    song.source_duration_seconds,
                    song.source_title_pinyin,
                    song.source_album_id,
                    song.source_album_name,
                    song.source_album_pinyin,
                    song.cover_url,
                    song.lyric_url,
                    song.mrc_url,
                    song.trc_url,
                    song.copyright_id
                ),
                result,
                boost::asio::use_awaitable
            );

            co_await MySqlTransactionRunner::executeStatements(connection, insert_song_sql_);

            for (const auto &artist_ref : artistRefsForSong(song)) {
                if (artist_ref.source_artist_id.empty() && artist_ref.source_artist_name.empty()) {
                    continue;
                }

                co_await connection->async_execute(
                    set_artist_vars_statement.bind(
                        artist_ref.source_artist_id,
                        artist_ref.source_artist_name,
                        artist_ref.source_artist_image,
                        artist_ref.source_name_spelling,
                        artist_ref.role.empty() ? std::string {"artist"} : artist_ref.role,
                        artist_ref.artist_order,
                        artist_ref.is_primary ? 1 : 0,
                        artist_ref.is_seed ? 1 : 0
                    ),
                    result,
                    boost::asio::use_awaitable
                );

                co_await MySqlTransactionRunner::executeStatements(connection, insert_artist_sql_);
            }

            co_await connection->async_execute(
                set_album_song_vars_statement.bind(
                    song.source_album_id,
                    song.source_song_id,
                    song.source_content_id,
                    song.album_disc_no,
                    song.album_disc_name,
                    song.album_track_no,
                    song.album_song_order
                ),
                result,
                boost::asio::use_awaitable
            );
            co_await MySqlTransactionRunner::executeStatements(connection, insert_album_song_sql_);

            int valid_audio_format_count = 0;
            for (const auto &format : song.audio_formats) {
                if (format.format_type.empty()) {
                    continue;
                }
                ++valid_audio_format_count;

                co_await connection->async_execute(
                    set_audio_vars_statement.bind(
                        format.format_type,
                        format.resource_type,
                        format.iformat,
                        format.aformat,
                        format.isize,
                        format.asize
                    ),
                    result,
                    boost::asio::use_awaitable
                );

                co_await MySqlTransactionRunner::executeStatements(connection, insert_audio_sql_);
            }
            if (valid_audio_format_count == 0) {
                co_await MySqlTransactionRunner::executeStatements(connection, insert_audio_placeholder_sql_);
            }
        }

        co_await MySqlTransactionRunner::executeStatements(connection, commit_sql_);
        co_return true;
    } catch (const std::exception &e) {
        failure_message = e.what();
    }

    if (transaction_started) {
        co_await MySqlTransactionRunner::rollbackQuietly(connection, rollback_sql_, "MySqlSongSourceRepository");
    }

    if (!failure_message.empty()) {
        std::cerr << "[MySqlSongSourceRepository] coSaveBatch failed: " << failure_message << std::endl;
    }
    co_return false;
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
