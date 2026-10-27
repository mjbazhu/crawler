#include "crawler/application/use_cases/MetadataPersistenceService.h"

#include "crawler/application/use_cases/MetadataBatchBuilder.h"

#include <utility>
#include <vector>

namespace orchestrator::modules::crawler::application::use_cases {

bool MetadataPersistenceService::saveMetadataBatch(application::model::MetadataWriteBatch batch)
{
    batch = buildMetadataWriteBatch(std::move(batch));
    auto dependencies = ingestion_;
    return persistence_runner_.run<bool>([dependencies, batch = std::move(batch)]() mutable {
        if (!dependencies.writer) return false;
        const auto outcome = dependencies.writer->saveBatch(batch);
        if (!outcome.isSuccess() && dependencies.diagnostics) dependencies.diagnostics->error("MetadataPersistenceService", outcome.message());
        return outcome.isSuccess();
    },
                                         false,
                                         "save metadata batch");
}

bool MetadataPersistenceService::saveAlbumDetails(const std::vector<application::model::AlbumSourceMetadata> &albums, const std::vector<application::model::SongSourceMetadata> &songs)
{
    application::model::MetadataWriteBatch batch;
    batch.albums = albums;
    batch.songs = songs;
    return saveAlbumDetails(std::move(batch));
}

bool MetadataPersistenceService::saveAlbumDetails(application::model::MetadataWriteBatch batch)
{
    if (!ingestion_.writer) {
        return (batch.singers.empty() || saveSingerSources(batch.singers, static_cast<int>(batch.singers.size()))) && (batch.albums.empty() || saveAlbumSources(batch.albums, static_cast<int>(batch.albums.size()))) && (batch.songs.empty() || saveSongSources(batch.songs, static_cast<int>(batch.songs.size())));
    }
    return (!batch.albums.empty() || !batch.songs.empty() || !batch.singers.empty()) && saveMetadataBatch(std::move(batch));
}

bool MetadataPersistenceService::hasAtomicWriter() const
{
    return static_cast<bool>(ingestion_.writer);
}

bool MetadataPersistenceService::saveSources(application::model::MetadataWriteBatch batch, int limit)
{
    if (limit > 0) {
        const auto count = static_cast<std::size_t>(limit);
        if (batch.singers.size() > count) batch.singers.resize(count);
        if (batch.albums.size() > count) batch.albums.resize(count);
        if (batch.songs.size() > count) batch.songs.resize(count);
    }
    if (!ingestion_.writer || (batch.singers.empty() && batch.albums.empty() && batch.songs.empty() && batch.profiles.empty())) return false;
    return saveMetadataBatch(std::move(batch));
}

} // namespace orchestrator::modules::crawler::application::use_cases
