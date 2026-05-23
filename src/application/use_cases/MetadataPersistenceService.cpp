#include "crawler/application/use_cases/MetadataPersistenceService.h"

#include <algorithm>
#include <variant>

namespace orchestrator::modules::crawler::application::use_cases {

namespace {

constexpr int kDefaultLimit = 20;

// 归一化业务 limit，避免单次保存无限扩张。
int normalizeLimit(int limit)
{
    if (limit <= 0) {
        return kDefaultLimit;
    }
    return (std::min)(limit, 500);
}

// 复制最多 limit 条记录，保持调用方输入不被修改。
template <typename T>
std::vector<T> limitedCopy(const std::vector<T> &items, int limit)
{
    std::vector<T> limited;
    const auto count = (std::min)(items.size(), static_cast<std::size_t>(normalizeLimit(limit)));
    limited.reserve(count);
    limited.insert(limited.end(), items.begin(), items.begin() + static_cast<std::ptrdiff_t>(count));
    return limited;
}

} // namespace

MetadataPersistenceService::MetadataPersistenceService(
    std::shared_ptr<application::ports::FileStore> file_store,
    std::shared_ptr<application::ports::SongRepository> song_repository,
    std::shared_ptr<application::ports::AlbumRepository> album_repository,
    std::shared_ptr<application::ports::SingerRepository> singer_repository,
    std::shared_ptr<application::ports::SongSourceRepository> song_source_repository,
    std::shared_ptr<application::ports::SingerSourceRepository> singer_source_repository,
    std::shared_ptr<application::ports::AlbumSourceWriter> album_source_writer,
    std::shared_ptr<application::ports::TaskExecutor> persistence_executor
) : file_store_(std::move(file_store)),
    song_repository_(std::move(song_repository)),
    album_repository_(std::move(album_repository)),
    singer_repository_(std::move(singer_repository)),
    song_source_repository_(std::move(song_source_repository)),
    singer_source_repository_(std::move(singer_source_repository)),
    album_source_writer_(std::move(album_source_writer)),
    persistence_runner_(std::move(persistence_executor))
{}

bool MetadataPersistenceService::saveParseResults(const std::vector<domain::ParseResult> &results, int limit)
{
    if (results.empty()) {
        return false;
    }

    std::vector<domain::Song> songs;
    std::vector<domain::Album> albums;
    std::vector<domain::Singer> singers;

    for (const auto &result : results) {
        if (!result.audio_data.empty() && file_store_ && !result.suggested_filename.empty()) {
            file_store_->save(result.suggested_filename, result.audio_data);
        }

        if (std::holds_alternative<domain::Song>(result.meta_info)) {
            songs.push_back(std::get<domain::Song>(result.meta_info));
        } else if (std::holds_alternative<domain::Album>(result.meta_info)) {
            albums.push_back(std::get<domain::Album>(result.meta_info));
        } else if (std::holds_alternative<domain::Singer>(result.meta_info)) {
            singers.push_back(std::get<domain::Singer>(result.meta_info));
        }
    }

    bool saved = false;
    saved = saveSongs(songs, limit) || saved;
    saved = saveAlbums(albums, limit) || saved;
    saved = saveSingers(singers) || saved;
    return saved;
}

bool MetadataPersistenceService::saveSongSources(const std::vector<application::model::SongSourceMetadata> &songs, int limit)
{
    if (!song_source_repository_ || songs.empty()) {
        return false;
    }

    auto repository = song_source_repository_;
    auto limited = limitedCopy(songs, limit);
    return persistence_runner_.run<bool>(
        [repository, limited = std::move(limited)]() {
            return repository ? repository->saveBatch(limited) : false;
        },
        false,
        "save song sources"
    );
}

bool MetadataPersistenceService::saveSingerSources(const std::vector<application::model::SingerSourceMetadata> &singers, int limit)
{
    if (!singer_source_repository_ || singers.empty()) {
        return false;
    }

    auto repository = singer_source_repository_;
    auto limited = limitedCopy(singers, limit);
    return persistence_runner_.run<bool>(
        [repository, limited = std::move(limited)]() {
            return repository ? repository->saveBatch(limited) : false;
        },
        false,
        "save singer sources"
    );
}

bool MetadataPersistenceService::saveAlbumSources(const std::vector<application::model::AlbumSourceMetadata> &albums, int limit)
{
    if (!album_source_writer_ || albums.empty()) {
        return false;
    }

    auto writer = album_source_writer_;
    auto limited = limitedCopy(albums, limit);
    return persistence_runner_.run<bool>(
        [writer, limited = std::move(limited)]() {
            return writer ? writer->saveBatch(limited) : false;
        },
        false,
        "save album sources"
    );
}

bool MetadataPersistenceService::saveSongs(const std::vector<domain::Song> &songs, int limit)
{
    if (!song_repository_ || songs.empty()) {
        return false;
    }

    auto repository = song_repository_;
    auto limited = limitedCopy(songs, limit);
    return persistence_runner_.run<bool>(
        [repository, limited = std::move(limited)]() {
            return repository ? repository->saveBatch(limited) : false;
        },
        false,
        "save songs"
    );
}

bool MetadataPersistenceService::saveAlbums(const std::vector<domain::Album> &albums, int limit)
{
    if (!album_repository_ || albums.empty()) {
        return false;
    }

    auto repository = album_repository_;
    auto limited = limitedCopy(albums, limit);
    bool saved = persistence_runner_.run<bool>(
        [repository, limited]() {
            return repository ? repository->saveBatch(limited) : false;
        },
        false,
        "save albums"
    );

    for (const auto &album : limited) {
        saved = saveSongs(album.vaild_songs, limit) || saved;
    }

    return saved;
}

bool MetadataPersistenceService::saveSingers(const std::vector<domain::Singer> &singers)
{
    if (!singer_repository_ || singers.empty()) {
        return false;
    }

    auto repository = singer_repository_;
    return persistence_runner_.run<bool>(
        [repository, singers]() {
            return repository ? repository->saveBatch(singers) : false;
        },
        false,
        "save singers"
    );
}

} // namespace orchestrator::modules::crawler::application::use_cases
