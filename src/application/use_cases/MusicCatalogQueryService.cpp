#include "crawler/application/use_cases/MusicCatalogQueryService.h"

#include <utility>

namespace orchestrator::modules::crawler::application::use_cases {

MusicCatalogQueryService::MusicCatalogQueryService(std::shared_ptr<application::ports::MusicCatalogQueryRepository> repository) : repository_(std::move(repository))
{}

application::model::MusicOverview MusicCatalogQueryService::overview() const
{
    if (!repository_) {
        return {};
    }
    return repository_->overview();
}

application::model::MiguSingerCatalogPage MusicCatalogQueryService::listSingers(application::model::MiguSingerCatalogQuery query) const
{
    return repository_ ? repository_->listSingers(std::move(query)) : application::model::MiguSingerCatalogPage {};
}

std::optional<application::model::MiguSingerCatalogRecord> MusicCatalogQueryService::findSinger(std::string source_singer_id) const
{
    return repository_ ? repository_->findSinger("migu", std::move(source_singer_id)) : std::nullopt;
}

application::model::MiguAlbumCatalogPage MusicCatalogQueryService::listAlbums(application::model::MiguAlbumCatalogQuery query) const
{
    return repository_ ? repository_->listAlbums(std::move(query)) : application::model::MiguAlbumCatalogPage {};
}

std::optional<application::model::MiguAlbumCatalogRecord> MusicCatalogQueryService::findAlbum(std::string source_album_id) const
{
    return repository_ ? repository_->findAlbum("migu", std::move(source_album_id)) : std::nullopt;
}

application::model::MiguSongCatalogPage MusicCatalogQueryService::listSongs(application::model::MiguSongCatalogQuery query) const
{
    return repository_ ? repository_->listSongs(std::move(query)) : application::model::MiguSongCatalogPage {};
}

std::optional<application::model::MiguSongCatalogRecord> MusicCatalogQueryService::findSong(std::string source_song_id) const
{
    return repository_ ? repository_->findSong("migu", std::move(source_song_id)) : std::nullopt;
}

application::model::MiguResourceQueuePage MusicCatalogQueryService::listResources(application::model::MiguResourceQueueQuery query) const
{
    return repository_ ? repository_->listResources(std::move(query)) : application::model::MiguResourceQueuePage {};
}

std::optional<application::model::MiguResourceQueueRecord> MusicCatalogQueryService::findResource(std::uint64_t resource_id) const
{
    return repository_ ? repository_->findResource(resource_id) : std::nullopt;
}

application::model::MiguResourcePreviewResult MusicCatalogQueryService::previewResources(application::model::MiguResourceActionQuery query) const
{
    return repository_ ? repository_->previewResources(std::move(query)) : application::model::MiguResourcePreviewResult {};
}

int MusicCatalogQueryService::retryResources(application::model::MiguResourceActionQuery query) const
{
    return repository_ ? repository_->retryResources(std::move(query)) : 0;
}

int MusicCatalogQueryService::resetResources(application::model::MiguResourceActionQuery query) const
{
    return repository_ ? repository_->resetResources(std::move(query)) : 0;
}

int MusicCatalogQueryService::recoverStaleResources(application::model::MiguResourceActionQuery query, std::string error) const
{
    return repository_ ? repository_->recoverStaleResources(std::move(query), std::move(error)) : 0;
}

application::model::MiguIntegritySummary MusicCatalogQueryService::integrity(int stale_after_seconds) const
{
    return repository_ ? repository_->integrity("migu", stale_after_seconds) : application::model::MiguIntegritySummary {};
}

application::model::MiguSchemaStatus MusicCatalogQueryService::schemaStatus() const
{
    return repository_ ? repository_->schemaStatus() : application::model::MiguSchemaStatus {};
}

void MusicCatalogQueryService::appendGovernanceEvents(const std::vector<application::model::MiguGovernanceEventRecord> &events) const
{
    if (repository_) {
        repository_->appendGovernanceEvents(events);
    }
}

application::model::MiguGovernanceEventPage MusicCatalogQueryService::listGovernanceEvents(application::model::MiguGovernanceEventQuery query) const
{
    return repository_ ? repository_->listGovernanceEvents(std::move(query)) : application::model::MiguGovernanceEventPage {};
}

std::vector<application::model::AlbumSourceMetadata> MusicCatalogQueryService::findAlbumSourcesByIds(const std::vector<std::string> &source_album_ids) const
{
    return repository_ ? repository_->findAlbumSourcesByIds("migu", source_album_ids) : std::vector<application::model::AlbumSourceMetadata> {};
}

application::model::MiguBackfillClaimResult MusicCatalogQueryService::claimSingerSongs(application::model::MiguBackfillTargetQuery query) const
{
    return repository_ ? repository_->claimSingerSongs(std::move(query)) : application::model::MiguBackfillClaimResult {};
}

application::model::MiguBackfillClaimResult MusicCatalogQueryService::claimSingerAlbums(application::model::MiguBackfillTargetQuery query) const
{
    return repository_ ? repository_->claimSingerAlbums(std::move(query)) : application::model::MiguBackfillClaimResult {};
}

application::model::MiguBackfillClaimResult MusicCatalogQueryService::claimAlbumDetails(application::model::MiguBackfillTargetQuery query) const
{
    return repository_ ? repository_->claimAlbumDetails(std::move(query)) : application::model::MiguBackfillClaimResult {};
}

application::model::MiguBackfillClaimResult MusicCatalogQueryService::claimAlbumSongs(application::model::MiguBackfillTargetQuery query) const
{
    return repository_ ? repository_->claimAlbumSongs(std::move(query)) : application::model::MiguBackfillClaimResult {};
}

int MusicCatalogQueryService::resetSingerSongs(application::model::MiguBackfillTargetQuery query) const
{
    return repository_ ? repository_->resetSingerSongs(std::move(query)) : 0;
}

int MusicCatalogQueryService::resetSingerAlbums(application::model::MiguBackfillTargetQuery query) const
{
    return repository_ ? repository_->resetSingerAlbums(std::move(query)) : 0;
}

int MusicCatalogQueryService::resetAlbumDetails(application::model::MiguBackfillTargetQuery query) const
{
    return repository_ ? repository_->resetAlbumDetails(std::move(query)) : 0;
}

int MusicCatalogQueryService::resetAlbumSongs(application::model::MiguBackfillTargetQuery query) const
{
    return repository_ ? repository_->resetAlbumSongs(std::move(query)) : 0;
}

int MusicCatalogQueryService::recoverStaleSingerSongs(std::string source, int stale_after_seconds, std::string error) const
{
    return repository_ ? repository_->recoverStaleSingerSongs(std::move(source), stale_after_seconds, std::move(error)) : 0;
}

int MusicCatalogQueryService::recoverStaleSingerAlbums(std::string source, int stale_after_seconds, std::string error) const
{
    return repository_ ? repository_->recoverStaleSingerAlbums(std::move(source), stale_after_seconds, std::move(error)) : 0;
}

int MusicCatalogQueryService::recoverStaleAlbumDetails(std::string source, int stale_after_seconds, std::string error) const
{
    return repository_ ? repository_->recoverStaleAlbumDetails(std::move(source), stale_after_seconds, std::move(error)) : 0;
}

int MusicCatalogQueryService::recoverStaleAlbumSongs(std::string source, int stale_after_seconds, std::string error) const
{
    return repository_ ? repository_->recoverStaleAlbumSongs(std::move(source), stale_after_seconds, std::move(error)) : 0;
}

} // namespace orchestrator::modules::crawler::application::use_cases
