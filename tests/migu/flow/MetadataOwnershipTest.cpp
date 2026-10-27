#include "crawler/application/use_cases/MetadataBatchBuilder.h"
#include "crawler/application/use_cases/MetadataPersistenceService.h"
#include "crawler/infrastructure/music/migu/parser/MiguParser.h"

#include <iostream>
#include <optional>
#include <vector>
#include <stdexcept>
#include <string>

namespace crawler = orchestrator::modules::crawler;
namespace model = crawler::application::model;
namespace cases = crawler::application::use_cases;

// 断言不受构建配置影响。
void require(bool value, const char *message)
{
    if (!value) throw std::runtime_error(message);
}

// 顺序测试替身，记录事务输入并模拟数据库失败。
class RecordingWriter final : public crawler::application::ports::MetadataBatchWriter {
public:
    model::MetadataWriteBatch last;                         // 最近一次事务输入
    std::vector<model::PersistedSongSource> bound_identity; // 数据库已绑定的实际身份
    bool fail = false;                                      // 是否模拟整批回滚

    // 保留分离后的实体及图片地址供断言检查。
    crawler::application::result::ServiceResult<model::MetadataWriteResult> saveBatch(const model::MetadataWriteBatch &batch) override
    {
        last = batch;
        using Result = crawler::application::result::ServiceResult<model::MetadataWriteResult>;
        return fail ? Result::failure(crawler::application::result::ServiceErrorType::InternalError, "fixture rollback") : Result::success({{}, bound_identity});
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

        auto writer = std::make_shared<RecordingWriter>();
        cases::MetadataPersistenceService service(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, {writer, {}});
        model::SingerSourceMetadata singer;
        singer.source = "migu";
        singer.source_singer_id = "artist";
        singer.name = "歌手";
        singer.avatar_url = "https://example.test/singer.png";
        singer.source_index_tab = "a";
        auto second = singer;
        second.source_index_tab = "b";
        require(service.saveSingerSources({singer, second}, 2), "singer metadata ingestion failed");
        require(writer->last.singers.size() == 1 && writer->last.memberships.size() == 2, "multi-tab singer was duplicated or membership lost");
        require(writer->last.singers.front().avatar_url == singer.avatar_url && !writer->last.singers.front().cover_resource, "singer URL was lost or turned into an inline download result");
        model::AlbumSourceMetadata album;
        album.source = "migu";
        album.source_album_id = "album";
        album.title = "元数据专辑";
        album.cover_url = "https://example.test/album.png";
        require(service.saveAlbumSources({album}, 0), "album metadata ingestion failed");
        require(writer->last.albums.front().cover_url == album.cover_url && !writer->last.albums.front().cover_resource, "album URL was lost or turned into an inline download result");
        auto song = songs.front();
        song.cover_url = "https://example.test/song.png";
        song.lyric_url = "https://example.test/song.lrc";
        song.mrc_url = "https://example.test/song.mrc";
        song.trc_url = "https://example.test/song.trc";
        writer->bound_identity = {{77, "migu", song.source_song_id, song.source_content_id}};
        require(service.saveSongSources({song}, 0), "song metadata ingestion failed");
        const auto &saved = writer->last.songs.front();
        require(saved.lyric_url == song.lyric_url && saved.mrc_url == song.mrc_url && saved.trc_url == song.trc_url && saved.cover_url == song.cover_url, "song resource task inputs were removed");
        writer->fail = true;
        require(!service.saveSingerSources({singer}, 1), "database failure was hidden");
        std::cout << "PASS: detail aliases, table ownership, metadata URLs and download-free ingestion\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
