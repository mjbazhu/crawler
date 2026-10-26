#include "crawler/application/use_cases/MetadataBatchBuilder.h"
#include "crawler/application/use_cases/MetadataPersistenceService.h"
#include "crawler/infrastructure/music/migu/parser/MiguParser.h"
#include "crawler/infrastructure/storage/filesystem/MetadataCoverStore.h"
#include "mocks/MockHttpClient.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace crawler = orchestrator::modules::crawler;
namespace model = crawler::application::model;
namespace cases = crawler::application::use_cases;
namespace storage = crawler::infrastructure::storage::filesystem;
namespace http = crawler::infrastructure::http::client;

// 断言不受构建配置影响。
void require(bool value, const char *message)
{
    if (!value) throw std::runtime_error(message);
}

// 顺序测试替身，记录事务输入并模拟数据库失败。
class RecordingWriter final : public crawler::application::ports::MetadataBatchWriter {
public:
    model::MetadataWriteBatch last; // 最近一次事务输入
    bool fail = false;              // 是否模拟整批回滚

    // 保留分离后的实体及本地封面结果供断言检查。
    crawler::application::result::ServiceResult<model::MetadataWriteResult> saveBatch(const model::MetadataWriteBatch &batch) override
    {
        last = batch;
        using Result = crawler::application::result::ServiceResult<model::MetadataWriteResult>;
        return fail ? Result::failure(crawler::application::result::ServiceErrorType::InternalError, "fixture rollback") : Result::success({});
    }
};

int main()
{
    try {
        crawler::infrastructure::music::migu::parser::MiguParser parser;
        const std::string json = R"({"resource":[{"contentId":"digital","materialId":"material","resourceType":"5","title":"专辑","singer":"歌手","singerId":"artist","songItems":[{"contentId":"content","songName":"歌曲","songNamePinyin":"gequ","albumId":"material","album":"专辑","albumNamePinyin":"zhuanji","length":"00:03:42","disc":"2","trackNumber":"7","artists":[{"id":"artist","name":"歌手","nameSpelling":"geshou"}],"newRateFormats":[{"formatType":"SQ","resourceType":"E","iosFormat":"011003","androidFormat":"011002","iosSize":"101","androidSize":"99"}]}]}]})";
        const std::vector<char> raw(json.begin(), json.end());
        const auto songs = parser.parseSongSources(raw);
        const auto albums = parser.parseAlbumSources(raw);
        require(songs.size() == 1 && albums.size() == 1, "detail entities missing");
        require(songs[0].source_duration_seconds == 222, "detail duration is not parsed");
        require(songs[0].album_track_no == 7 && songs[0].album_disc_no == 2, "explicit disc/track lost");
        require(songs[0].source_album_id == "digital" && songs[0].source_album_material_id == "material", "album context identity lost");
        require(albums[0].source_title_pinyin == "zhuanji", "album pinyin did not reach album input");
        require(songs[0].audio_formats.size() == 1 && songs[0].audio_formats[0].iformat == "011003" && songs[0].audio_formats[0].asize == 99, "iOS/Android aliases lost");
        model::MetadataWriteBatch input;
        input.songs = songs;
        input.albums = albums;
        const auto split = cases::buildMetadataWriteBatch(std::move(input));
        require(split.singers.size() == 1 && split.albums.size() == 1 && split.songs.size() == 1, "entity separation duplicates or loses parents");
        require(split.singers[0].source_name_pinyin == "geshou", "artist pinyin missing from singer input");
        model::MetadataWriteBatch band_input;
        auto band_song = songs[0];
        band_song.artist_refs[0].source_artist_name = "Earth, Wind & Fire";
        band_input.songs = {band_song};
        require(cases::buildMetadataWriteBatch(std::move(band_input)).singers.size() == 1, "an individual artist name containing commas was discarded");
        const std::string list = R"({"songName":"列表歌曲","contentId":"list","albumId":"album","album":"专辑"})";
        const auto list_songs = parser.parseSongSources({list.begin(), list.end()});
        require(list_songs.size() == 1 && list_songs[0].album_track_no == 0 && list_songs[0].album_song_order == 0, "list position became an album track");

        auto client = std::make_shared<http::MockHttpClient>();
        http::HttpResponse image;
        image.status_code = 200;
        image.content_type = "image/png";
        const std::string bytes = "\x89PNG\r\n\x1a\nfixture-cover";
        image.body.assign(bytes.begin(), bytes.end());
        client->mock_responses.emplace("https://example.test/cover.png", image);
        auto html = image;
        html.body = {'<', 'h', 't', 'm', 'l', '>'};
        client->mock_responses.emplace("https://example.test/error", html);
        const auto directory = std::filesystem::temp_directory_path() / ("crawler_cover_ownership_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        auto files = std::make_shared<storage::FileStorage>(directory.string());
        auto covers = std::make_shared<storage::MetadataCoverStore>(client, files, std::vector<std::string> {});
        auto writer = std::make_shared<RecordingWriter>();
        cases::MetadataPersistenceService service(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, {writer, covers, {}});
        model::SingerSourceMetadata singer;
        singer.source = "migu";
        singer.source_singer_id = "artist";
        singer.name = "歌手";
        singer.avatar_url = "https://example.test/cover.png";
        singer.source_index_tab = "a";
        auto second = singer;
        second.source_index_tab = "b";
        require(service.saveSingerSources({singer, second}, 2), "cover ingestion failed");
        require(writer->last.singers.size() == 1 && writer->last.memberships.size() == 2, "multi-tab singer was duplicated or membership lost");
        auto saved = writer->last.singers[0].cover_resource.value();
        require(saved.status == 2 && files->exists(saved.local_path) && saved.size_bytes == bytes.size() && !saved.checksum.empty(), "cover was marked complete without a committed file");
        writer->fail = true;
        require(!service.saveSingerSources({singer}, 1), "database failure was hidden");
        require(!files->exists(writer->last.singers[0].cover_resource->local_path), "failed database batch left a new cover file");
        require(files->exists(saved.local_path), "rollback deleted a previous successful cover");
        writer->fail = false;
        singer.avatar_url = "https://example.test/error";
        require(service.saveSingerSources({singer}, 1), "cover error prevented metadata persistence");
        require(writer->last.singers[0].cover_resource->status == 3, "HTML response was treated as a saved image");
        require(covers->discard(saved.local_path), "cover cleanup failed");
        std::cout << "PASS: detail aliases, table ownership, multi-tab, cover commit and rollback\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
