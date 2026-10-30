#include "crawler/infrastructure/music/persistence/mysql/MySqlMetadataBatchWriter.h"

#include "MySqlMetadataWriteSession.h"
#include "crawler/infrastructure/music/persistence/sql/SqlFileLoader.h"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/mysql/results.hpp>

#include <chrono>
#include <exception>
#include <optional>
#include <stdexcept>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {
namespace {

// Asio co_spawn 的异常通道要求默认可构造返回值；适配层用 optional 包装既有结果类型。
boost::asio::awaitable<std::optional<application::result::ServiceResult<application::model::MetadataWriteResult>>> executeBatch(MySqlMetadataBatchWriter *writer, application::model::MetadataWriteBatch batch)
{
    co_return co_await writer->coSaveBatch(std::move(batch));
}

} // namespace

MySqlMetadataBatchWriter::MySqlMetadataBatchWriter(std::shared_ptr<MySqlConnectionPool> pool) : pool_(std::move(pool))
{
    if (!pool_) throw std::invalid_argument("metadata writer requires a connection pool");
    for (const auto *path : {
             "songs/upsert_metadata", "singers/upsert_metadata", "albums/upsert_metadata", "song_source_refs/upsert", "song_source_refs/select_metadata_identity", "song_source_refs/select_metadata_identity_by_input", "singer_source_refs/upsert", "album_source_refs/upsert", "singer_index_memberships/upsert", "song_artist_refs/upsert", "album_artist_refs/upsert", "album_song_refs/upsert", "song_source_audio_formats/upsert", "song_resources/upsert", "song_resources/upsert_missing_audio", "song_resources/delete_missing_audio", "singer_resources/upsert", "album_resources/upsert", "singer_resources/upsert_remote_url", "album_resources/upsert_remote_url", "migu_dalbum_details/upsert", "singer_resources/find_current", "album_resources/find_current", "singer_source_refs/find_source_id", "singer_source_refs/update_migu_singer_source_profile_counts", "album_source_refs/find_source_id", "migu_dalbum_details/select_identity_mappings", "album_source_refs/lock_identity_pair", "albums/merge_alias_metadata", "album_source_refs/merge_alias_metadata", "album_artist_refs/merge_album_alias", "album_song_refs/merge_album_alias", "album_resources/merge_album_alias", "album_resources/alias_transfer_guard", "album_resources/alias_transfer_reference", "album_resources/alias_transfer_archive", "album_resources/alias_transfer_receipts", "album_resources/alias_transfer_block", "album_resources/alias_transfer_skip", "album_source_refs/delete_alias", "albums/delete_unreferenced"
         }) {
        scripts_.emplace(path, sql::SqlFileLoader::loadOrThrow(std::string("sql/mysql/") + path + ".sql"));
    }
}

application::result::ServiceResult<application::model::MetadataWriteResult> MySqlMetadataBatchWriter::saveBatch(const application::model::MetadataWriteBatch &batch)
{
    using Result = application::result::ServiceResult<application::model::MetadataWriteResult>;
    try {
        auto future = boost::asio::co_spawn(pool_->executor(), executeBatch(this, batch), boost::asio::use_future);
        return future.get().value();
    } catch (const std::exception &) {
        return Result::failure(application::result::ServiceErrorType::InternalError, "metadata transaction could not be executed");
    }
}

boost::asio::awaitable<application::result::ServiceResult<application::model::MetadataWriteResult>> MySqlMetadataBatchWriter::coSaveBatch(application::model::MetadataWriteBatch batch)
{
    using Result = application::result::ServiceResult<application::model::MetadataWriteResult>;
    if (batch.singers.empty() && batch.albums.empty() && batch.songs.empty() && batch.profiles.empty()) {
        co_return Result::failure(application::result::ServiceErrorType::InvalidParam, "metadata batch is empty");
    }
    for (int attempt = 0; attempt < 5; ++attempt) {
        auto connection = co_await pool_->coAcquire();
        bool started = false;
        bool committing = false;
        std::string error;
        try {
            boost::mysql::results result;
            co_await connection->async_execute("START TRANSACTION", result, boost::asio::use_awaitable);
            started = true;
            detail::MetadataWriteSession session(connection, scripts_);
            co_await session.normalizeAlbumIdentities(batch);
            for (const auto &singer : batch.singers) co_await session.saveSinger(singer);
            for (const auto &profile : batch.profiles) co_await session.saveSingerProfile(profile);
            for (const auto &album : batch.albums) co_await session.saveAlbum(album);
            for (const auto &song : batch.songs) co_await session.saveSong(song);
            co_await session.mergeAlbumAliases();
            for (const auto &membership : batch.memberships) co_await session.saveMembership(membership);
            for (const auto &singer : batch.singers) co_await session.saveSingerResource(singer);
            for (const auto &album : batch.albums) co_await session.saveAlbumRelations(album);
            for (const auto &song : batch.songs) co_await session.saveSongRelations(song);
            committing = true;
            co_await connection->async_execute("COMMIT", result, boost::asio::use_awaitable);
            co_return Result::success({session.replacedCoverPaths(), session.savedSongs()});
        } catch (const std::exception &failure) {
            error = failure.what();
        }
        if (committing) co_return Result::failure(application::result::ServiceErrorType::CommitUncertain, "metadata commit needs reconciliation");
        if (started) {
            try {
                boost::mysql::results result;
                co_await connection->async_execute("ROLLBACK", result, boost::asio::use_awaitable);
            } catch (const std::exception &) {
                co_return Result::failure(application::result::ServiceErrorType::InternalError, "metadata transaction rollback failed");
            }
        }
        const bool retryable = error.find("deadlock") != std::string::npos || error.find("Deadlock") != std::string::npos || error.find("1205") != std::string::npos || error.find("1213") != std::string::npos;
        if (!retryable || attempt == 4) {
            co_return Result::failure(application::result::ServiceErrorType::InternalError, "metadata transaction failed: " + error);
        }
        boost::asio::steady_timer timer(pool_->executor(), std::chrono::milliseconds(25 * (attempt + 1)));
        co_await timer.async_wait(boost::asio::use_awaitable);
    }
    co_return Result::failure(application::result::ServiceErrorType::InternalError, "metadata retry limit reached");
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
