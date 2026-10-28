#include "crawler/application/use_cases/CatalogMetadataCompletion.h"
#include "crawler/application/use_cases/MetadataPersistenceService.h"
#include "crawler/application/use_cases/SingerCatalogWorkflow.h"
#include "crawler/infrastructure/music/migu/parser/MiguParser.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMetadataBatchWriter.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMusicCatalogQueryRepository.h"
#include "migu/support/MetadataTestDiagnostics.h"
#include "migu/support/MySqlTestSupport.h"
#include "mocks/MockMusicProvider.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace c = orchestrator::modules::crawler;
namespace model = c::application::model;
namespace use = c::application::use_cases;
namespace mysql = c::infrastructure::music::persistence::mysql;
namespace support = c::tests::migu::support;
using Json = nlohmann::json;

namespace {

// 集成断言不依赖构建模式。
void require(bool value, const char *message)
{
    if (!value) throw std::runtime_error(message);
}

// 独立 SQL 观察落库结果，不以缓存或写入返回数量代替数据核验。
std::uint64_t scalar(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, const std::string &sql)
{
    const auto result = support::runOnExecutor(pool->executor(), pool->coExecute(sql), "catalog association verification");
    return mysql::MySqlValueReader::uint64At(result.rows().front(), 0);
}

// 平台响应全部为离线 fixture，不装配真实 HTTP 或资源下载器。
model::ProviderResponse response(const Json &json)
{
    model::ProviderResponse value;
    value.status_code = 200;
    const auto body = json.dump();
    value.body.assign(body.begin(), body.end());
    return value;
}

// 记录请求边界，关联歌手只能由已有响应入库，不能触发其目录或资源请求。
class Provider final : public c::tests::MockMusicProvider {
public:
    int profiles = 0; // 整份歌手资料请求数
    int details = 0;  // 整份专辑详情请求数
    int pages = 0;    // 缺页请求数，本用例详情已返回完整曲目

    // 只有目录入口允许获取歌手资料，关联歌手直接使用响应字段。
    std::optional<Response> fetchSingerInfo(std::uint64_t id) override
    {
        ++profiles;
        require(id == 777001, "associated singer triggered another profile request");
        return MockMusicProvider::fetchSingerInfo(id);
    }

    // 详情与曲目共用一次请求。
    std::optional<Response> fetchAlbumDetail(std::string id, std::string type) override
    {
        ++details;
        return MockMusicProvider::fetchAlbumDetail(std::move(id), std::move(type));
    }

    // 完整详情不应再调用分页接口。
    std::optional<Response> fetchAlbumSongPage(std::string, int, int) override
    {
        ++pages;
        return std::nullopt;
    }
};

} // namespace

// 八首已存在、两首新增；复采和重启后检查来源 ID、字段、关系及请求数。
int main()
{
    try {
        require(support::makeMySqlConfigFromEnv().database.starts_with("crawler_ownership_test_"), "refusing non-test database");
        auto pool = support::makeInitializedPool();
        auto writer = std::make_shared<mysql::MySqlMetadataBatchWriter>(pool);
        auto catalog = std::make_shared<mysql::MySqlMusicCatalogQueryRepository>(pool);
        auto saver = std::make_shared<use::MetadataPersistenceService>(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, use::MetadataIngestionDependencies {writer, std::make_shared<support::MetadataTestDiagnostics>()});
        auto parser = std::make_shared<c::infrastructure::music::migu::parser::MiguParser>();
        auto dispatch = std::make_shared<use::ParserDispatch>(parser, parser, parser, parser, parser, parser, nullptr, nullptr);
        auto provider = std::make_shared<Provider>();
        const auto prefix = "assoc-" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + "-";
        const auto album_id = prefix + "album";
        Json tracks = Json::array();
        for (int number = 0; number < 10; ++number) {
            Json song = {{"contentId", prefix + std::to_string(number)}, {"songId", prefix + "s" + std::to_string(number)}, {"songName", prefix + "歌曲" + std::to_string(number)}, {"songPinyin", "gequ" + std::to_string(number)}, {"albumId", album_id}, {"album", "关联专辑"}, {"duration", 180}, {"singerList", {{{"id", "777001"}, {"name", "入口歌手"}}}}};
            if (number == 9) song["singerList"].push_back({{"id", "777002"}, {"name", "合唱歌手"}, {"nameSpelling", "hechang"}});
            tracks.push_back(std::move(song));
        }
        Json seed = Json::array();
        for (int number = 0; number < 8; ++number) {
            auto song = tracks[number];
            song.erase("songPinyin");
            seed.push_back(std::move(song));
        }
        require(saver->saveSources(dispatch->parseSources(response(seed).body), 0), "initial song response did not create its album/singer relations");
        const auto first_id = scalar(pool, "SELECT id FROM song_source_refs WHERE source='migu' AND source_content_id='" + prefix + "0'");
        require(scalar(pool, "SELECT COUNT(*) FROM album_song_refs r JOIN album_source_refs a ON a.id=r.album_source_ref_id WHERE a.source_album_id='" + album_id + "'") == 8, "song-carried album relations were not created");
        // 旧版本可能只保存详情而丢弃曲目；新响应必须同时更新声明总数和缺失关系。
        support::runOnExecutor(pool->executor(), pool->coExecute("UPDATE album_source_refs SET detail_sync_status=2,detail_synced_at=123,source_song_total=8,summary='old summary' WHERE source_album_id='" + album_id + "'"), "seed legacy detail-only state");
        const Json tabs = Json::array({{{"view", "ZJ-Tab-Item"}, {"action", "song"}, {"txt2", "10"}}, {{"view", "ZJ-Tab-Item"}, {"action", "album"}, {"txt2", "1"}}});
        const Json info = Json::array({{{"view", "ZJ-SingerDetail-Item"}, {"resId", "777001"}, {"txt", "入口歌手"}}, {{"view", "ZJ-Tab-Scroll"}, {"contents", tabs}}});
        provider->mock_singer_info_response = response({{"data", {{"contents", info}}}});
        const Json albums = Json::array({{{"view", "ZJ-Album-Item"}, {"resId", album_id}, {"resType", "0"}, {"txt", "关联专辑"}, {"txt2", "入口歌手"}}});
        provider->mock_singer_albums_response = response({{"data", {{"contents", albums}}}});
        auto returned = tracks;
        returned.push_back(tracks[0]); // 同一响应的重复卡片仍只写一个来源和一条关系。
        auto recommendation = tracks[0];
        recommendation["contentId"] = prefix + "unrelated";
        recommendation["songId"] = prefix + "unrelated-song";
        recommendation["albumId"] = prefix + "other-album";
        const Json target = {{"albumId", album_id}, {"title", "关联专辑"}, {"totalCount", 10}, {"summary", "完整简介"}, {"albumPinyin", "guanlianzhuanji"}, {"singerId", "777001"}, {"singer", "入口歌手"}, {"songItems", returned}};
        const Json other = {{"albumId", prefix + "other-album"}, {"title", "推荐专辑"}, {"totalCount", 1}, {"songItems", Json::array({recommendation})}};
        provider->mock_album_detail_response = response({{"resource", Json::array({target, other})}});
        provider->mock_singer_songs_response = response(Json::array({tracks[0]}));
        auto completion = std::make_shared<use::CatalogMetadataCompletion>(use::CatalogMetadataCompletionDependencies {provider, provider, catalog, dispatch});
        use::SingerCatalogWorkflow workflow({provider, dispatch, saver, nullptr, nullptr, completion});
        require(workflow.crawlSingerAlbumsAndSaveResult(777001, 1, 1).success, "album collection failed");
        require(scalar(pool, "SELECT COUNT(*) FROM song_source_refs WHERE source_content_id LIKE '" + prefix + "%'") == 10, "album tracks were discarded or duplicated");
        require(scalar(pool, "SELECT id FROM song_source_refs WHERE source_content_id='" + prefix + "0'") == first_id, "existing song source was recreated");
        require(scalar(pool, "SELECT COUNT(*) FROM song_source_refs WHERE source_content_id='" + prefix + "0' AND source_title_pinyin='gequ0'") == 1, "existing song fields were not completed");
        require(scalar(pool, "SELECT COUNT(*) FROM album_song_refs r JOIN album_source_refs a ON a.id=r.album_source_ref_id WHERE a.source_album_id='" + album_id + "'") == 10, "album relations were not completed");
        require(scalar(pool, "SELECT COUNT(*) FROM album_artist_refs r JOIN album_source_refs a ON a.id=r.album_source_ref_id JOIN singer_source_refs s ON s.id=r.singer_source_ref_id WHERE a.source_album_id='" + album_id + "' AND s.source_singer_id='777001'") == 1, "album artist relation was lost");
        require(scalar(pool, "SELECT COUNT(*) FROM song_artist_refs r JOIN song_source_refs s ON s.id=r.song_source_ref_id JOIN singer_source_refs a ON a.id=r.singer_source_ref_id WHERE s.source_content_id='" + prefix + "9' AND a.source_singer_id='777002'") == 1, "associated singer was not persisted with its song");
        require(scalar(pool, "SELECT COUNT(*) FROM album_source_refs WHERE source_album_id='" + album_id + "' AND detail_sync_status=2 AND songs_sync_status=2 AND songs_sync_count=10 AND source_song_total=10 AND summary='完整简介'") == 1, "detail and track completion were not saved together");
        require(workflow.crawlSingerAlbumsAndSaveResult(777001, 1, 1).success, "repeat album collection failed");
        (void)workflow.crawlSingerSongsAndSaveResult(777001, 1, 1);
        require(provider->details == 1 && provider->profiles == 1 && provider->pages == 0, "complete responses triggered duplicate metadata requests");

        auto restarted = std::make_shared<use::CatalogMetadataCompletion>(use::CatalogMetadataCompletionDependencies {provider, provider, catalog, dispatch});
        use::SingerCatalogWorkflow after_restart({provider, dispatch, saver, nullptr, nullptr, restarted});
        (void)after_restart.crawlSingerSongsAndSaveResult(777001, 1, 1, 10, 1);
        require(provider->details == 1 && scalar(pool, "SELECT COUNT(*) FROM song_source_refs WHERE source_content_id LIKE '" + prefix + "%'") == 10, "restart fetched completed detail or duplicated songs");

        // 歌曲入口发现新专辑和歌手时随批次建档；第二次仍复用相同来源和关系。
        const auto other_album = prefix + "song-carried-album";
        auto carried = tracks[0];
        carried["contentId"] = prefix + "song-carried";
        carried["songId"] = prefix + "song-carried-id";
        carried["songName"] = prefix + "新歌曲";
        carried["albumId"] = other_album;
        carried["album"] = "歌曲携带专辑";
        carried["singerList"] = Json::array({{{"id", "777003"}, {"name", "歌曲携带歌手"}}});
        provider->mock_singer_songs_response = response(Json::array({carried}));
        provider->mock_album_detail_response = response({{"data", {{"albumId", other_album}, {"title", "歌曲携带专辑"}, {"totalCount", 1}, {"singerId", "777003"}, {"singer", "歌曲携带歌手"}, {"songItems", Json::array({carried})}}}});
        (void)after_restart.crawlSingerSongsAndSaveResult(777001, 1, 1, 10, 1);
        (void)after_restart.crawlSingerSongsAndSaveResult(777001, 1, 1, 10, 1);
        require(provider->details == 2 && provider->profiles == 1 && provider->pages == 0, "song-carried entities triggered redundant or recursive requests");
        require(scalar(pool, "SELECT COUNT(*) FROM album_song_refs r JOIN album_source_refs a ON a.id=r.album_source_ref_id JOIN song_source_refs s ON s.id=r.song_source_ref_id WHERE a.source_album_id='" + other_album + "' AND s.source_content_id='" + prefix + "song-carried'") == 1, "song-carried album relation was not deduplicated");
        require(scalar(pool, "SELECT COUNT(*) FROM song_artist_refs r JOIN song_source_refs s ON s.id=r.song_source_ref_id JOIN singer_source_refs a ON a.id=r.singer_source_ref_id WHERE s.source_content_id='" + prefix + "song-carried' AND a.source_singer_id='777003'") == 1, "song-carried singer relation was not completed");
        require(scalar(pool, "SELECT COUNT(*) FROM song_resources r JOIN song_source_refs s ON s.id=r.song_source_ref_id WHERE s.source_content_id LIKE '" + prefix + "%' AND (r.status NOT IN(0,4) OR r.local_path IS NOT NULL OR r.fetched_at<>0 OR r.size_bytes<>0 OR r.claim_generation<>0)") == 0, "metadata ingestion unexpectedly processed a binary resource");
        std::cout << "PASS: 8 existing + 2 new tracks, fields, artist/album relations, deduplication, response reuse and restart; no downloads\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
