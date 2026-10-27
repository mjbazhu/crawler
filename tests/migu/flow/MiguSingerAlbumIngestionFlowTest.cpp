#include "crawler/application/use_cases/CrawlerService.h"
#include "crawler/infrastructure/execution/threading/ThreadPool.h"
#include "mocks/MockAlbumSourceRepository.h"
#include "mocks/MockMetadataParser.h"
#include "mocks/MockMusicProvider.h"
#include "mocks/MockSongSourceRepository.h"

#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace crawler = orchestrator::modules::crawler;
namespace use_cases = crawler::application::use_cases;
namespace threading = crawler::infrastructure::execution::threading;

namespace {

// 测试断言失败时抛出异常，避免依赖 assert 宏影响 clangd 检查。
void require(bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

crawler::application::model::ProviderResponse makeResponse(const std::string &body)
{
    crawler::application::model::ProviderResponse response;
    response.status_code = 200;
    response.body = std::vector<char>(body.begin(), body.end());
    response.content_type = "application/json";
    return response;
}

crawler::application::model::AlbumSourceMetadata makeAlbum(std::string album_id, int detail_sync_status, std::string source_res_type = {})
{
    crawler::application::model::AlbumSourceMetadata album;
    album.source = "migu";
    album.title = "太阳之子";
    album.normalized_title = album.title;
    album.artist_name = "周杰伦";
    album.source_album_id = std::move(album_id);
    album.source_res_type = std::move(source_res_type);
    album.detail_sync_status = detail_sync_status;
    return album;
}

crawler::application::model::SongSourceMetadata makeSong(std::string content_id, std::string album_id)
{
    crawler::application::model::SongSourceMetadata song;
    song.source = "migu";
    song.source_song_id = "1142521344";
    song.source_content_id = std::move(content_id);
    song.source_resource_type = "2";
    song.title = "太阳之子";
    song.normalized_title = song.title;
    song.source_album_id = std::move(album_id);
    song.source_album_name = "太阳之子";
    song.source_primary_artist_id = "112";
    song.source_primary_artist_name = "周杰伦";
    return song;
}

void testSingerSongUnlimitedIngestionFlow()
{
    auto provider = std::make_shared<crawler::tests::MockMusicProvider>();
    provider->mock_singer_songs_response = makeResponse("song-list");

    auto parser = std::make_shared<crawler::tests::MockMetadataParser>();
    parser->mock_song_sources_by_body["song-list"] = {
        makeSong("600927000001", "600927015009001083"),
        makeSong("600927000002", "600927015009001083"),
    };

    auto song_source_repository = std::make_shared<crawler::tests::MockSongSourceRepository>();
    auto parse_executor = std::make_shared<threading::ThreadPool>(1);
    auto batch_executor = std::make_shared<threading::ThreadPool>(1);
    auto page_executor = std::make_shared<threading::ThreadPool>(1);

    use_cases::CrawlerService service(
        provider,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        parser,
        parse_executor,
        batch_executor,
        page_executor,
        nullptr,
        1,
        1,
        0,
        std::chrono::minutes(1),
        crawler::application::model::CrawlerRuntimeSettings {},
        song_source_repository
    );

    const bool saved = service.crawlSingerSongsAndSaveSync(112, 0);
    require(saved, "saved singer songs with unlimited limit");
    require(provider->last_singer_id == 112, "singer song provider id");
    require(song_source_repository->save_calls == 1, "song source repository save calls");
    require(song_source_repository->saved_songs.size() == 2, "unlimited singer songs should save all parsed songs");
}

void testSingerAlbumIngestionFlow()
{
    auto provider = std::make_shared<crawler::tests::MockMusicProvider>();
    provider->mock_singer_albums_response = makeResponse("album-list");

    auto parser = std::make_shared<crawler::tests::MockMetadataParser>();
    parser->mock_album_sources_by_body["album-list"] = {
        makeAlbum("600927015009001083", 0, "1"),
        makeAlbum("600927015009001084", 0, "1"),
    };

    auto album_source_repository = std::make_shared<crawler::tests::MockAlbumSourceRepository>();
    auto parse_executor = std::make_shared<threading::ThreadPool>(1);
    auto batch_executor = std::make_shared<threading::ThreadPool>(1);
    auto page_executor = std::make_shared<threading::ThreadPool>(1);

    use_cases::CrawlerService service(
        provider,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        parser,
        parse_executor,
        batch_executor,
        page_executor,
        nullptr,
        1,
        1,
        0,
        std::chrono::minutes(1),
        crawler::application::model::CrawlerRuntimeSettings {},
        nullptr,
        nullptr,
        album_source_repository
    );

    const bool saved = service.crawlSingerAlbumsAndSaveSync(112, 0);
    require(saved, "saved");
    require(provider->last_singer_id == 112, "provider->last_singer_id == 112");
    require(provider->last_album_detail_id.empty(), "album detail should not run in list flow");
    require(album_source_repository->save_calls == 1, "album_source_repository->save_calls == 1");
    require(album_source_repository->saved_albums.size() == 2, "unlimited singer albums should save all parsed albums");
    require(album_source_repository->saved_albums.back().detail_sync_status == 0, "album list detail_sync_status == 0");
    require(album_source_repository->saved_albums.back().source_artist_id == "112", "album list source artist id");
    require(album_source_repository->saved_albums.back().artist_refs.size() == 1, "album list seed relation missing");
    require(album_source_repository->saved_albums.back().artist_refs.front().artist_order == 0, "seed membership must not invent album artist order");
    require(!album_source_repository->saved_albums.back().artist_refs.front().is_primary, "seed membership must not invent primary credit");
}

void testPendingAlbumDetailIngestionFlow()
{
    auto provider = std::make_shared<crawler::tests::MockMusicProvider>();
    provider->mock_album_detail_response = makeResponse("album-detail");

    auto parser = std::make_shared<crawler::tests::MockMetadataParser>();
    auto detail = makeAlbum("600927015009001083", 2);
    detail.total_count = "1";
    detail.source_material_id = "1142521343";
    detail.summary = "专辑简介";
    parser->mock_album_sources_by_body["album-detail"] = {detail};
    parser->mock_song_sources_by_body["album-detail"] = {makeSong("600927000001", "600927015009001083")};

    auto album_source_repository = std::make_shared<crawler::tests::MockAlbumSourceRepository>();
    album_source_repository->pending_details = {makeAlbum("600927015009001083", 0, "1")};
    auto song_source_repository = std::make_shared<crawler::tests::MockSongSourceRepository>();
    auto parse_executor = std::make_shared<threading::ThreadPool>(1);
    auto batch_executor = std::make_shared<threading::ThreadPool>(1);
    auto page_executor = std::make_shared<threading::ThreadPool>(1);

    use_cases::CrawlerService service(
        provider,
        nullptr,
        nullptr,
        nullptr,
        nullptr,
        parser,
        parse_executor,
        batch_executor,
        page_executor,
        nullptr,
        1,
        1,
        0,
        std::chrono::minutes(1),
        crawler::application::model::CrawlerRuntimeSettings {},
        song_source_repository,
        nullptr,
        album_source_repository
    );

    const bool saved = service.crawlPendingAlbumDetailsAndSaveSync("migu", 10);
    require(saved, "saved pending album details");
    require(provider->last_album_detail_id == "600927015009001083", "album detail id");
    require(provider->last_album_detail_source_res_type == "1", "album detail source res type");
    require(album_source_repository->update_detail_calls == 2, "detail task should record start and completion");
    require(album_source_repository->last_detail_status == 2, "detail task should finish its own state");
    require(album_source_repository->update_songs_calls == 1, "complete detail response should also record its actual song result");
    require(album_source_repository->last_songs_status == 2, "declared complete detail response should complete song sync");
    require(album_source_repository->last_songs_count == 1, "song sync count should use this response");
    require(album_source_repository->save_calls == 1, "album detail save calls");
    require(album_source_repository->saved_albums.size() == 1, "album detail saved albums size");
    require(album_source_repository->saved_albums.back().detail_sync_status == 2, "album detail sync status complete");
    require(album_source_repository->saved_albums.back().source_material_id == "1142521343", "detail source material id");
    require(song_source_repository->save_calls == 1, "song_source_repository->save_calls == 1");
    require(song_source_repository->saved_songs.size() == 1, "song source saved songs size");
    require(song_source_repository->saved_songs.back().source_album_id == "600927015009001083", "song source album id");
}

} // namespace

int main()
{
    try {
        testSingerSongUnlimitedIngestionFlow();
        testSingerAlbumIngestionFlow();
        testPendingAlbumDetailIngestionFlow();
        std::cout << "MiguSingerAlbumIngestionFlowTest passed." << std::endl;
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "MiguSingerAlbumIngestionFlowTest failed: " << error.what() << std::endl;
        return 1;
    }
}
