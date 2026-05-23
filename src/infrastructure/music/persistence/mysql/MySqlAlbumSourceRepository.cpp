#include "crawler/infrastructure/music/persistence/mysql/MySqlAlbumSourceRepository.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlAlbumSourceSyncQueue.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlAlbumSourceWriter.h"
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {

MySqlAlbumSourceRepository::MySqlAlbumSourceRepository(std::shared_ptr<MySqlConnectionPool> pool) : MySqlAlbumSourceRepository(std::move(pool), SqlPaths {})
{}

MySqlAlbumSourceRepository::MySqlAlbumSourceRepository(std::shared_ptr<MySqlConnectionPool> pool, SqlPaths paths) : writer_(std::make_unique<MySqlAlbumSourceWriter>(pool, paths)),
                                                                                                                    sync_queue_(std::make_unique<MySqlAlbumSourceSyncQueue>(std::move(pool), std::move(paths)))
{}

MySqlAlbumSourceRepository::~MySqlAlbumSourceRepository() = default;

bool MySqlAlbumSourceRepository::saveBatch(const std::vector<application::model::AlbumSourceMetadata> &albums)
{
    return writer_->saveBatch(albums);
}

std::vector<application::model::AlbumSourceMetadata> MySqlAlbumSourceRepository::findPendingDetails(std::string source, std::size_t limit, std::size_t offset)
{
    return sync_queue_->findPendingDetails(std::move(source), limit, offset);
}

bool MySqlAlbumSourceRepository::updateDetailSyncStatus(std::string source, std::string source_album_id, int status, std::string error)
{
    return sync_queue_->updateDetailSyncStatus(std::move(source), std::move(source_album_id), status, std::move(error));
}

std::vector<application::model::AlbumSourceMetadata> MySqlAlbumSourceRepository::findPendingSongs(std::string source, std::size_t limit, std::size_t offset)
{
    return sync_queue_->findPendingSongs(std::move(source), limit, offset);
}

bool MySqlAlbumSourceRepository::updateSongsSyncStatus(std::string source, std::string source_album_id, int status, int count, std::string error)
{
    return sync_queue_->updateSongsSyncStatus(std::move(source), std::move(source_album_id), status, count, std::move(error));
}

boost::asio::awaitable<bool> MySqlAlbumSourceRepository::coSaveBatch(std::vector<application::model::AlbumSourceMetadata> albums)
{
    return writer_->coSaveBatch(std::move(albums));
}

boost::asio::awaitable<std::vector<application::model::AlbumSourceMetadata>> MySqlAlbumSourceRepository::coFindPendingDetails(std::string source, std::size_t limit, std::size_t offset)
{
    return sync_queue_->coFindPendingDetails(std::move(source), limit, offset);
}

boost::asio::awaitable<bool> MySqlAlbumSourceRepository::coUpdateDetailSyncStatus(std::string source, std::string source_album_id, int status, std::string error)
{
    return sync_queue_->coUpdateDetailSyncStatus(std::move(source), std::move(source_album_id), status, std::move(error));
}

boost::asio::awaitable<std::vector<application::model::AlbumSourceMetadata>> MySqlAlbumSourceRepository::coFindPendingSongs(std::string source, std::size_t limit, std::size_t offset)
{
    return sync_queue_->coFindPendingSongs(std::move(source), limit, offset);
}

boost::asio::awaitable<bool> MySqlAlbumSourceRepository::coUpdateSongsSyncStatus(std::string source, std::string source_album_id, int status, int count, std::string error)
{
    return sync_queue_->coUpdateSongsSyncStatus(std::move(source), std::move(source_album_id), status, count, std::move(error));
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
