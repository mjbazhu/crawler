#include "crawler/infrastructure/music/persistence/mysql/MySqlAlbumSourceWriter.h"

#include "crawler/application/use_cases/MetadataBatchBuilder.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlWriteRunner.h"

#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {

MySqlAlbumSourceWriter::MySqlAlbumSourceWriter(std::shared_ptr<MySqlConnectionPool> pool) : pool_(std::move(pool)), writer_(pool_)
{}

MySqlAlbumSourceWriter::MySqlAlbumSourceWriter(std::shared_ptr<MySqlConnectionPool> pool, MySqlAlbumSourceSqlPaths) : MySqlAlbumSourceWriter(std::move(pool))
{}

bool MySqlAlbumSourceWriter::saveBatch(const std::vector<application::model::AlbumSourceMetadata> &albums)
{
    return MySqlWriteRunner::waitForWrite(pool_->executor(), coSaveBatch(albums), "MySqlAlbumSourceWriter", "saveBatch");
}

boost::asio::awaitable<bool> MySqlAlbumSourceWriter::coSaveBatch(std::vector<application::model::AlbumSourceMetadata> albums)
{
    if (albums.empty()) co_return true;
    application::model::MetadataWriteBatch batch;
    batch.albums = std::move(albums);
    const auto result = co_await writer_.coSaveBatch(application::use_cases::buildMetadataWriteBatch(std::move(batch)));
    co_return result.isSuccess();
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
