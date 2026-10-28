#include "crawler/application/use_cases/CatalogMetadataCompletion.h"
#include "crawler/api/controllers/CrawlerController.h"
#include "crawler/application/use_cases/CrawlerService.h"
#include "crawler/application/use_cases/MetadataPersistenceService.h"
#include "crawler/application/use_cases/SingerCatalogWorkflow.h"
#include "crawler/infrastructure/music/migu/parser/MiguParser.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMetadataBatchWriter.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlAlbumSourceSyncQueue.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMusicCatalogQueryRepository.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlSingerSourceSyncQueue.h"
#include "migu/support/MetadataTestDiagnostics.h"
#include "migu/support/MySqlTestSupport.h"
#include "mocks/MockMusicProvider.h"

#include <nlohmann/json.hpp>

#include <chrono>
#include <algorithm>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

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
    int album_pages = 0; // 歌手专辑目录请求数
    // 提供与生产 Migu 相同的容量提示，验证真实 writer 前的窗口收缩。
    std::size_t singerAlbumPageSize() const override
    {
        return 10;
    }
    // 目录会重新获取当前列表，不能因资料复用而跳过入口。
    std::optional<Response> fetchSingerAlbumsPage(std::uint64_t id, int page) override
    {
        ++album_pages;
        return MockMusicProvider::fetchSingerAlbumsPage(id, page);
    }

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

// 顺序驱动批次执行，避免元数据状态回归依赖线程时序。
class Executor final : public c::application::ports::TaskExecutor {
public:
    std::vector<std::function<void()>> tasks; // 本轮批次调度的任务

    // 捕获批次工作，待测试显式执行。
    void submit(std::function<void()> task) override
    {
        tasks.push_back(std::move(task));
    }

    // 执行已认领的单个目录批次。
    void runAll()
    {
        auto pending = std::move(tasks);
        tasks.clear();
        for (auto &task : pending) task();
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
        require(workflow.crawlSingerAlbumsAndSaveResult(777001, 1, 4).success, "album collection failed");
        require(scalar(pool, "SELECT COUNT(*) FROM song_source_refs WHERE source_content_id LIKE '" + prefix + "%'") == 10, "album tracks were discarded or duplicated");
        require(scalar(pool, "SELECT id FROM song_source_refs WHERE source_content_id='" + prefix + "0'") == first_id, "existing song source was recreated");
        require(scalar(pool, "SELECT COUNT(*) FROM song_source_refs WHERE source_content_id='" + prefix + "0' AND source_title_pinyin='gequ0'") == 1, "existing song fields were not completed");
        require(scalar(pool, "SELECT COUNT(*) FROM album_song_refs r JOIN album_source_refs a ON a.id=r.album_source_ref_id WHERE a.source_album_id='" + album_id + "'") == 10, "album relations were not completed");
        require(scalar(pool, "SELECT COUNT(*) FROM album_artist_refs r JOIN album_source_refs a ON a.id=r.album_source_ref_id JOIN singer_source_refs s ON s.id=r.singer_source_ref_id WHERE a.source_album_id='" + album_id + "' AND s.source_singer_id='777001'") == 1, "album artist relation was lost");
        require(scalar(pool, "SELECT COUNT(*) FROM song_artist_refs r JOIN song_source_refs s ON s.id=r.song_source_ref_id JOIN singer_source_refs a ON a.id=r.singer_source_ref_id WHERE s.source_content_id='" + prefix + "9' AND a.source_singer_id='777002'") == 1, "associated singer was not persisted with its song");
        require(scalar(pool, "SELECT COUNT(*) FROM album_source_refs WHERE source_album_id='" + album_id + "' AND detail_sync_status=2 AND songs_sync_status=2 AND songs_sync_count=10 AND source_song_total=10 AND summary='完整简介'") == 1, "detail and track completion were not saved together");
        require(workflow.crawlSingerAlbumsAndSaveResult(777001, 1, 4).success, "repeat album collection failed");
        (void)workflow.crawlSingerSongsAndSaveResult(777001, 1, 1);
        require(provider->details == 1 && provider->profiles == 1 && provider->pages == 0, "complete responses triggered duplicate metadata requests");
        require(provider->album_pages == 2, "limited catalogs fetched unused directory pages");
        std::cout << "NORMAL_INTEGRATION_REQUEST_COUNTS profiles=" << provider->profiles << " details=" << provider->details << " album_pages=" << provider->album_pages << " track_pages=" << provider->pages << '\n';

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

        // 关联数量已达总数时，受 limit 截断的目录仍必须保持部分完成。
        auto queue = std::make_shared<mysql::MySqlSingerSourceSyncQueue>(pool);
        auto executor = std::make_shared<Executor>();
        use::CrawlerServiceDependencies dependencies;
        dependencies.providers.singer_catalog_provider = provider;
        dependencies.providers.album_detail_provider = provider;
        dependencies.parsers.parse_result_parser = parser;
        dependencies.parsers.song_source_parser = parser;
        dependencies.parsers.singer_source_parser = parser;
        dependencies.parsers.album_source_parser = parser;
        dependencies.parsers.singer_profile_parser = parser;
        dependencies.parsers.page_inspector = parser;
        dependencies.persistence.metadata_batch_writer = writer;
        dependencies.persistence.catalog_query_repository = catalog;
        dependencies.persistence.singer_source_sync_queue = queue;
        auto album_queue = std::make_shared<mysql::MySqlAlbumSourceSyncQueue>(pool);
        dependencies.persistence.album_source_sync_queue = album_queue;
        dependencies.executors.batch_executor = executor;
        dependencies.options.runtime_settings.max_retries = 0;
        auto service = std::make_shared<use::CrawlerService>(std::move(dependencies));
        support::runOnExecutor(pool->executor(), pool->coExecute("UPDATE singer_source_refs SET source_song_total=10,source_album_total=2,songs_sync_status=0,songs_sync_error=NULL,albums_sync_status=0,albums_sync_error=NULL WHERE source='migu' AND source_singer_id='777001'"), "prepare uncollected directory states");
        model::SingerSourceSyncQuery query;
        query.keyword = "777001";
        query.limit = 1;
        require(!service->startSingerSongsFromDbAsync(query, 1, "{}").empty(), "song directory batch was not created");
        executor->runAll();
        require(scalar(pool, "SELECT COUNT(*) FROM singer_source_refs WHERE source_singer_id='777001' AND songs_sync_status=4 AND songs_sync_count=1") == 1, "associated songs incorrectly completed a limited directory run");
        require(!service->startSingerAlbumsFromDbAsync(query, 1, "{}").empty(), "album directory batch was not created");
        executor->runAll();
        require(scalar(pool, "SELECT COUNT(*) FROM singer_source_refs WHERE source_singer_id='777001' AND albums_sync_status=4 AND albums_sync_count=1") == 1, "associated albums incorrectly completed a limited directory run");

        // 三张专辑的七首不同歌曲共用内容占位值；重复提交仍保留独立身份和关系。
        Json placeholder_albums = Json::array();
        for (int album_number = 0; album_number < 3; ++album_number) {
            const auto placeholder_album_id = prefix + "placeholder-album-" + std::to_string(album_number);
            Json placeholder_tracks = Json::array();
            for (int track = 0; track < (album_number == 2 ? 1 : 3); ++track) {
                placeholder_tracks.push_back({{"songId", prefix + "placeholder-song-" + std::to_string(album_number) + "-" + std::to_string(track)}, {"contentId", "600000000000000000"}, {"songName", prefix + "占位曲目" + std::to_string(album_number) + "-" + std::to_string(track)}});
            }
            placeholder_albums.push_back({{"albumId", placeholder_album_id}, {"title", prefix + "占位专辑" + std::to_string(album_number)}, {"totalCount", placeholder_tracks.size()}, {"songItems", placeholder_tracks}});
        }
        const auto placeholder_batch = dispatch->parseSources(response({{"resource", placeholder_albums}}).body);
        require(saver->saveSources(placeholder_batch, 0) && saver->saveSources(placeholder_batch, 0), "placeholder album replay failed");
        require(scalar(pool, "SELECT COUNT(*) FROM song_source_refs WHERE source_song_id LIKE '" + prefix + "placeholder-song-%' AND source_content_id IS NULL") == 7, "placeholder tracks were merged or duplicated in MySQL");
        require(scalar(pool, "SELECT COUNT(*) FROM album_song_refs r JOIN album_source_refs a ON a.id=r.album_source_ref_id JOIN song_source_refs s ON s.id=r.song_source_ref_id WHERE a.source_album_id LIKE '" + prefix + "placeholder-album-%' AND s.source_song_id LIKE CONCAT(REPLACE(a.source_album_id,'placeholder-album-','placeholder-song-'),'-%')") == 7, "placeholder tracks were linked to a different album's song");
        // 公共归档保留本次歌曲及关系，不读取旧库中的完整集合，也不发起详情或补页。
        Json shared_songs = Json::array();
        for (int track = 0; track < 2; ++track) {
            shared_songs.push_back({{"songId", prefix + "shared-song-" + std::to_string(track)}, {"contentId", prefix + "shared-content-" + std::to_string(track)}, {"songName", prefix + "当前单曲" + std::to_string(track)}, {"albumId", "1"}, {"album", "单曲发行"}, {"singerList", {{{"id", "777001"}, {"name", "入口歌手"}}}}});
        }
        provider->mock_singer_songs_response = response(shared_songs);
        const int details_before_shared = provider->details;
        for (int repeat = 0; repeat < 2; ++repeat) {
            auto shared_completion = std::make_shared<use::CatalogMetadataCompletion>(use::CatalogMetadataCompletionDependencies {provider, provider, catalog, dispatch});
            use::SingerCatalogWorkflow shared_workflow({provider, dispatch, saver, nullptr, nullptr, shared_completion});
            require(shared_workflow.crawlSingerSongsAndSaveResult(777001, 0, 1, 2, 2).success, "shared archive song collection failed");
        }
        require(provider->details == details_before_shared && provider->pages == 0, "shared archive triggered implicit detail or page requests");
        require(scalar(pool, "SELECT COUNT(*) FROM song_source_refs WHERE source_song_id LIKE '" + prefix + "shared-song-%'") == 2, "shared song replay lost or duplicated sources");
        require(scalar(pool, "SELECT COUNT(*) FROM album_source_refs WHERE source='migu' AND source_album_id='1' AND detail_sync_status=0 AND songs_sync_status=0 AND source_song_total IS NULL") == 1, "shared summary falsely completed an archive sync");
        require(scalar(pool, "SELECT COUNT(*) FROM album_song_refs r JOIN album_source_refs a ON a.id=r.album_source_ref_id JOIN song_source_refs s ON s.id=r.song_source_ref_id WHERE a.source='migu' AND a.source_album_id='1' AND s.source_song_id LIKE '" + prefix + "shared-song-%'") == 2, "shared archive current-response relations were lost or duplicated");
        require(scalar(pool, "SELECT COUNT(*) FROM song_artist_refs r JOIN song_source_refs s ON s.id=r.song_source_ref_id JOIN singer_source_refs a ON a.id=r.singer_source_ref_id WHERE s.source_song_id LIKE '" + prefix + "shared-song-%' AND a.source_singer_id='777001' AND r.is_seed=1") == 2, "shared archive lost singer relations");

        // 公共归档在普通目录仍可见；回补队列、预览、认领和重置使用相同规则。
        c::api::controllers::CrawlerController controller(service);
        const auto call = [&](const std::string &type, const std::string &action, const Json &body) {
            c::api::http::HttpRequest request;
            request.method = "POST";
            request.path = "/api/migu/backfill/" + type + "/" + action;
            request.body = body.dump();
            const auto result = action == "preview" ? controller.handlePreviewMiguBackfill(request) : (action == "retry" ? controller.handleRetryMiguBackfill(request) : controller.handleResetMiguBackfill(request));
            require(result.status_code == (action == "retry" ? 202 : 200), "metadata backfill HTTP result failed");
            return Json::parse(result.body);
        };
        model::MetadataWriteBatch same_name_batch;
        model::AlbumSourceMetadata same_name;
        same_name.source_album_id = prefix + "same-name";
        same_name.title = "单曲发行";
        same_name_batch.albums.push_back(same_name);
        require(saver->saveSources(std::move(same_name_batch), 0), "same-name real album fixture failed");
        model::MiguAlbumCatalogQuery archive_query;
        archive_query.keyword = "单曲发行";
        require(catalog->listAlbums(archive_query).total == 2, "normal catalog hid the shared summary");
        archive_query.exclude_shared_release_archives = true;
        const auto filtered = catalog->listAlbums(archive_query);
        require(filtered.total == 1 && filtered.items.front().source_album_id == same_name.source_album_id, "backfill filtering hid a same-name real album");
        c::api::http::HttpRequest queue_request;
        queue_request.method = "GET";
        queue_request.path = "/api/migu/backfill/album-details/queue";
        queue_request.query_params["q"] = "单曲发行";
        const auto queue_body = Json::parse(controller.handleListMiguBackfillQueue(queue_request).body);
        require(queue_body["total"] == 1 && queue_body["items"][0]["target_id"] == same_name.source_album_id, "HTTP queue included the shared archive");
        const auto bulk_preview = call("album-details", "preview", {{"filter", {{"keyword", "单曲发行"}}}, {"force", true}});
        require(bulk_preview["matched_count"] == 2 && bulk_preview["eligible_count"] == 1, "bulk preview and claim filtering disagree");
        for (int status : {0, 2, 3, 4, 1}) {
            support::runOnExecutor(pool->executor(), pool->coExecute("UPDATE album_source_refs SET detail_sync_status=" + std::to_string(status) + ",songs_sync_status=" + std::to_string(status) + " WHERE source='migu' AND source_album_id='1'"), "prepare shared archive state");
            for (const std::string type : {"album-details", "album-songs"}) {
                for (bool force : {false, true}) {
                    const Json body = {{"source_ids", {"1"}}, {"force", force}};
                    const auto preview = call(type, "preview", body);
                    require(preview["eligible_count"] == 0 && preview["items"][0]["reason"] == "shared_release_archive", "force re-enabled the shared archive in preview");
                    const auto retry = call(type, "retry", body);
                    require(retry["claimed_count"] == 0 && retry["items"][0]["reason"] == "shared_release_archive", "shared archive was claimed or lost its skip reason");
                    executor->runAll();
                }
                const auto reset = call(type, "reset", {{"source_ids", {"1"}}, {"filter", {{"status", {status}}}}});
                require(reset["updated_count"] == 0 && reset["items"][0]["reason"] == "shared_release_archive", "shared archive was reset into backfill");
            }
            require(scalar(pool, "SELECT COUNT(*) FROM album_source_refs WHERE source='migu' AND source_album_id='1' AND detail_sync_status=" + std::to_string(status) + " AND songs_sync_status=" + std::to_string(status)) == 1, "skipping shared archive changed its state");
        }
        require(provider->details == details_before_shared && provider->pages == 0, "shared backfill issued metadata requests");
        support::runOnExecutor(pool->executor(), pool->coExecute("UPDATE album_source_refs SET detail_sync_status=0,songs_sync_status=0 WHERE source='migu' AND source_album_id='1'"), "restore shared summary");
        const auto pending = album_queue->findPendingDetails("migu", 1000, 0);
        require(std::none_of(pending.begin(), pending.end(), [](const auto &album) { return album.source_album_id == "1"; }), "legacy pending queue included shared archive");
        support::runOnExecutor(pool->executor(), pool->coExecute("UPDATE album_source_refs SET detail_sync_status=2 WHERE source='migu' AND source_album_id='1'"), "prepare legacy archive track queue");
        const auto pending_songs = album_queue->findPendingSongs("migu", 1000, 0);
        require(std::none_of(pending_songs.begin(), pending_songs.end(), [](const auto &album) { return album.source_album_id == "1"; }), "legacy track queue included shared archive");
        support::runOnExecutor(pool->executor(), pool->coExecute("UPDATE album_source_refs SET detail_sync_status=0 WHERE source='migu' AND source_album_id='1'"), "restore archive detail state");

        // 有明确物料映射的 ID=1 是独立数字身份，不能按普通归档跳过。
        support::runOnExecutor(pool->executor(), pool->coExecute("INSERT INTO migu_dalbum_details(album_source_ref_id,source_material_id) SELECT id,'explicit-material' FROM album_source_refs WHERE source='migu' AND source_album_id='1'"), "prepare explicit material exception");
        require(call("album-details", "preview", {{"source_ids", {"1"}}})["eligible_count"] == 1, "explicit material identity was treated as shared archive");
        const auto material_sources = catalog->findAlbumSourcesByIds("migu", {"1"});
        require(material_sources.size() == 1 && material_sources.front().source_material_id == "explicit-material", "explicit source lookup lost material identity");
        const auto material_pending = album_queue->findPendingDetails("migu", 1000, 0);
        const auto material_target = std::find_if(material_pending.begin(), material_pending.end(), [](const auto &album) { return album.source_album_id == "1"; });
        require(material_target != material_pending.end() && material_target->source_material_id == "explicit-material", "legacy queue lost material identity");
        model::MiguBackfillTargetQuery material_claim;
        material_claim.source_ids = {"1"};
        const auto claimed_material = catalog->claimAlbumDetails(material_claim);
        require(claimed_material.claimed_count == 1 && claimed_material.albums.front().source_material_id == "explicit-material", "material identity was lost in claim mapping");
        support::runOnExecutor(pool->executor(), pool->coExecute("DELETE d FROM migu_dalbum_details d JOIN album_source_refs a ON a.id=d.album_source_ref_id WHERE a.source='migu' AND a.source_album_id='1'"), "remove material exception fixture");
        support::runOnExecutor(pool->executor(), pool->coExecute("UPDATE album_source_refs SET detail_sync_status=0,detail_claim_token='',detail_claim_owner='',detail_claimed_at=0 WHERE source='migu' AND source_album_id='1'"), "restore shared archive after material check");

        // 真实专辑回补仍接收原响应之外的新歌曲，并保留已有来源主键。
        Json backfill_detail = target;
        auto extra_track = tracks.front();
        extra_track["contentId"] = prefix + "backfill-new-content";
        extra_track["songId"] = prefix + "backfill-new-song";
        backfill_detail["songItems"] = tracks;
        backfill_detail["songItems"].push_back(extra_track);
        backfill_detail["totalCount"] = 11;
        provider->mock_album_detail_response = response({{"resource", Json::array({backfill_detail})}});
        for (int repeat = 0; repeat < 2; ++repeat) {
            const auto retry = call("album-details", "retry", {{"source_ids", {album_id}}, {"force", true}});
            require(retry["claimed_count"] == 1, "real completed album was excluded from forced backfill");
            executor->runAll();
        }
        require(scalar(pool, "SELECT COUNT(*) FROM album_song_refs r JOIN album_source_refs a ON a.id=r.album_source_ref_id WHERE a.source_album_id='" + album_id + "'") == 11, "real album backfill discarded newly returned tracks");
        require(scalar(pool, "SELECT id FROM song_source_refs WHERE source_content_id='" + prefix + "0'") == first_id, "real album backfill replaced an existing song identity");
        require(scalar(pool, "SELECT COUNT(*) FROM song_source_refs WHERE source_content_id='" + prefix + "backfill-new-content'") == 1, "real album backfill duplicated its new track");
        require(scalar(pool, "SELECT COUNT(*) FROM album_song_refs r JOIN album_source_refs a ON a.id=r.album_source_ref_id WHERE a.source='migu' AND a.source_album_id='1'") == 2, "backfill widened or removed carried shared archive relations");
        // 新实体已经写入后故意让歌手端点缺失，必须连同已有字段更新一起回滚。
        auto changed_song = tracks.front();
        changed_song["songPinyin"] = "rollback-probe";
        auto rollback_song = changed_song;
        rollback_song["songId"] = prefix + "rollback-song";
        rollback_song["contentId"] = prefix + "rollback-content";
        rollback_song["songName"] = prefix + "回滚歌曲";
        rollback_song["albumId"] = prefix + "rollback-album";
        rollback_song["album"] = prefix + "回滚专辑";
        rollback_song["singerList"] = Json::array({{{"id", prefix + "missing-artist"}}});
        const auto broken_batch = dispatch->parseSources(response(Json::array({changed_song, rollback_song})).body);
        require(!saver->saveSources(broken_batch, 0), "relation failure did not reject the metadata transaction");
        require(scalar(pool, "SELECT COUNT(*) FROM song_source_refs WHERE source_content_id='" + prefix + "rollback-content'") == 0, "failed relation left a partial song source");
        require(scalar(pool, "SELECT COUNT(*) FROM songs WHERE normalized_title='" + prefix + "回滚歌曲'") == 0, "failed relation left a partial public song");
        require(scalar(pool, "SELECT COUNT(*) FROM album_source_refs WHERE source_album_id='" + prefix + "rollback-album'") == 0, "failed relation left a partial album source");
        require(scalar(pool, "SELECT COUNT(*) FROM song_source_refs WHERE id=" + std::to_string(first_id) + " AND source_title_pinyin='gequ0'") == 1, "rolled-back batch changed an existing nonempty field");
        rollback_song["singerList"][0]["name"] = prefix + "恢复歌手";
        const auto restored_batch = dispatch->parseSources(response(Json::array({changed_song, rollback_song})).body);
        require(saver->saveSources(restored_batch, 0) && saver->saveSources(restored_batch, 0), "metadata retry after corrected relation did not succeed");
        require(scalar(pool, "SELECT COUNT(*) FROM song_source_refs WHERE source_content_id='" + prefix + "rollback-content'") == 1, "retry duplicated the recovered song source");
        require(scalar(pool, "SELECT COUNT(*) FROM song_artist_refs r JOIN song_source_refs s ON s.id=r.song_source_ref_id JOIN singer_source_refs a ON a.id=r.singer_source_ref_id WHERE s.source_content_id='" + prefix + "rollback-content' AND a.source_singer_id='" + prefix + "missing-artist'") == 1, "recovered artist relation was missing or duplicated");
        require(scalar(pool, "SELECT COUNT(*) FROM album_song_refs r JOIN song_source_refs s ON s.id=r.song_source_ref_id JOIN album_source_refs a ON a.id=r.album_source_ref_id WHERE s.source_content_id='" + prefix + "rollback-content' AND a.source_album_id='" + prefix + "rollback-album'") == 1, "recovered album relation was missing or duplicated");
        require(scalar(pool, "SELECT id FROM song_source_refs WHERE source_content_id='" + prefix + "0'") == first_id, "recovery replaced the existing source identity");
        std::cout << "PASS: catalog identity/relations, metadata rollback, corrected replay and idempotent recovery\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
