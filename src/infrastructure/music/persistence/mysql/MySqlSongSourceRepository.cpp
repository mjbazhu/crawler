#include "crawler/infrastructure/music/persistence/mysql/MySqlSongSourceRepository.h"

#include "crawler/application/use_cases/MetadataBatchBuilder.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlWriteRunner.h"

#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {

MySqlSongSourceRepository::MySqlSongSourceRepository(std::shared_ptr<MySqlConnectionPool> pool) : pool_(std::move(pool)), writer_(pool_)
{}

bool MySqlSongSourceRepository::saveBatch(const std::vector<application::model::SongSourceMetadata> &songs)
{
    return MySqlWriteRunner::waitForWrite(pool_->executor(), coSaveBatch(songs), "MySqlSongSourceRepository", "saveBatch");
}

boost::asio::awaitable<bool> MySqlSongSourceRepository::coSaveBatch(std::vector<application::model::SongSourceMetadata> songs)
{
    if (songs.empty()) co_return true;
    application::model::MetadataWriteBatch batch;
    batch.songs = std::move(songs);
    const auto result = co_await writer_.coSaveBatch(application::use_cases::buildMetadataWriteBatch(std::move(batch)));
    co_return result.isSuccess();
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
