#include "crawler/application/use_cases/CatalogMetadataCompletion.h"

#include <atomic>
#include <algorithm>
#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

#include "crawler/application/use_cases/MetadataPersistenceService.h"
#include "crawler/application/use_cases/SingerCatalogWorkflow.h"
#include "crawler/infrastructure/music/migu/parser/MiguParser.h"
#include "mocks/MockMusicProvider.h"

namespace c = orchestrator::modules::crawler;
namespace model = c::application::model;
namespace use = c::application::use_cases;
namespace {

// 回归结果不能依赖 assert 是否启用。
void require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

// 所有网络响应均为离线 fixture，不连接平台或资源地址。
model::ProviderResponse response(const std::string &text)
{
    model::ProviderResponse value;
    value.status_code = 200;
    value.body.assign(text.begin(), text.end());
    return value;
}

// 统计整份实体请求，证明拼音、简介和总数不会各触发一次补查。
class Provider final : public c::tests::MockMusicProvider {
public:
    std::atomic<int> profiles {0}; // 歌手资料请求数
    std::atomic<int> details {0};  // 专辑详情请求数
    std::atomic<int> pages {0};    // 曲目补页请求数
    std::atomic<int> song_pages {0};  // 歌手歌曲目录请求数
    std::atomic<int> album_pages {0}; // 歌手专辑目录请求数
    // 目录必须重取时仍统计实际分页，不把详情缓存复用当成目录未执行。
    std::optional<Response> fetchSingerSongsPage(std::uint64_t id, int page) override
    {
        ++song_pages;
        return MockMusicProvider::fetchSingerSongsPage(id, page);
    }
    // 普通专辑目录与歌曲目录共用整份资料，但各自获取当前列表。
    std::optional<Response> fetchSingerAlbumsPage(std::uint64_t id, int page) override
    {
        ++album_pages;
        return MockMusicProvider::fetchSingerAlbumsPage(id, page);
    }
    // 同一份资料同时包含歌手实体及两个声明总数。
    std::optional<Response> fetchSingerInfo(std::uint64_t id) override
    {
        ++profiles;
        std::this_thread::sleep_for(std::chrono::milliseconds(15));
        return MockMusicProvider::fetchSingerInfo(id);
    }
    // 同一份详情同时包含拼音、简介、日期、总数及可能尚未选择的曲目。
    std::optional<Response> fetchAlbumDetail(std::string id, std::string type) override
    {
        ++details;
        return MockMusicProvider::fetchAlbumDetail(std::move(id), std::move(type));
    }
    // 共享归档不能自动补页；本测试的真实专辑详情已经完整。
    std::optional<Response> fetchAlbumSongPage(std::string, int, int) override
    {
        ++pages;
        return std::nullopt;
    }
};

// 记录事务输入，核验补齐与主体数据一起保存。
class Writer final : public c::application::ports::MetadataBatchWriter {
public:
    std::vector<model::MetadataWriteBatch> batches; // 目录入口提交的原子批次
    // 这个替身只观察边界；数据库事务由集成测试覆盖。
    c::application::result::ServiceResult<model::MetadataWriteResult> saveBatch(const model::MetadataWriteBatch &batch) override
    {
        batches.push_back(batch);
        return c::application::result::ServiceResult<model::MetadataWriteResult>::success({});
    }
};

// 目标专辑的其他曲目必须入库，其他专辑的推荐内容仍排除。
std::shared_ptr<Provider> provider()
{
    auto value = std::make_shared<Provider>();
    value->mock_singer_info_response = response(R"({"data":{"contents":[{"view":"ZJ-SingerDetail-Item","resId":"7","txt":"入口歌手"},{"view":"ZJ-Tab-Scroll","contents":[{"view":"ZJ-Tab-Item","action":"song","txt2":"2"},{"view":"ZJ-Tab-Item","action":"album","txt2":"1"}]}]}})");
    value->mock_singer_albums_response = response(R"({"data":{"contents":[{"view":"ZJ-Album-Item","resId":"a1","resType":"0","txt":"专辑一","txt2":"入口歌手"},{"view":"ZJ-Album-Item","resId":"unselected","resType":"0","txt":"未选专辑"}]}})");
    value->mock_singer_songs_response = response(R"([{"contentId":"c1","songId":"s1","songName":"歌曲一","albumId":"a1","album":"专辑一","singerList":[{"id":"7","name":"入口歌手","nameSpelling":"rukou"}]}])");
    value->mock_album_detail_response = response(R"({"resource":[{"albumId":"a1","title":"专辑一","summary":"完整简介","totalCount":"2","resourceType":"2003","publishDate":"2020-01-01","albumPinyin":"zhuanjiyi","songItems":[{"contentId":"c1","songId":"s1","songName":"歌曲一","songPinyin":"gequyi","albumId":"a1","album":"专辑一","singerList":[{"id":"7","name":"入口歌手"}]},{"contentId":"c2","songName":"专辑其他曲目","albumId":"a1","album":"专辑一","singerList":[{"id":"8","name":"合唱歌手"}]}]},{"albumId":"other-album","title":"推荐专辑","totalCount":"1","songItems":[{"contentId":"recommendation","songName":"其他专辑曲目","albumId":"other-album","album":"推荐专辑","singerList":[{"id":"unknown","name":"推荐歌手"}]}]}]})");
    return value;
}

} // namespace

// 验证既定采集顺序、整份响应复用、并发去重、失败重试及字段携带。
int main()
{
    try {
        auto source = provider();
        auto parser = std::make_shared<c::infrastructure::music::migu::parser::MiguParser>();
        auto dispatch = std::make_shared<use::ParserDispatch>(parser, parser, parser, parser, parser, parser, nullptr, nullptr);
        auto completion = std::make_shared<use::CatalogMetadataCompletion>(use::CatalogMetadataCompletionDependencies {source, source, nullptr, dispatch});
        auto writer = std::make_shared<Writer>();
        auto persistence = std::make_shared<use::MetadataPersistenceService>(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, use::MetadataIngestionDependencies {writer, nullptr});
        use::SingerCatalogWorkflow workflow(use::SingerCatalogWorkflowDependencies {source, dispatch, persistence, nullptr, nullptr, completion});
        const auto albums = workflow.crawlSingerAlbumsAndSaveResult(7, 1, 1);
        require(albums.success && albums.written_count == 1, "album collection did not complete");
        const auto &first = writer->batches.back();
        require(first.albums.size() == 1 && first.songs.size() == 2, "album tracks were discarded or recommendations were admitted");
        require(first.albums.front().songs_sync && first.albums.front().songs_sync->status == 2 && first.albums.front().songs_sync->received_count == 2, "album tracks did not carry their verified completion state");
        require(first.albums.front().summary == "完整简介" && first.albums.front().source_title_pinyin == "zhuanjiyi" && first.albums.front().total_count == "2", "album response fields were not carried");
        require(first.profiles.size() == 1 && first.profiles.front().source_song_total == 2 && first.profiles.front().source_album_total == 1, "profile totals were not committed with albums");
        const auto songs = workflow.crawlSingerSongsAndSaveResult(7, 1, 1);
        require(songs.partial && songs.written_count == 1, "a limited subset was reported as a complete catalog");
        const auto &second = writer->batches.back();
        require(second.songs.size() == 2 && second.songs.front().source_title_pinyin == "gequyi", "cached album tracks were lost or the selected song was duplicated");
        require(second.singers.size() == 2 && std::none_of(second.singers.begin(), second.singers.end(), [](const auto &singer) { return singer.source_singer_id == "unknown"; }), "associated singers were lost or recommendation singers were admitted");
        require(source->profiles == 1 && source->details == 1, "album/song phases requested each metadata field separately");
        (void)workflow.crawlSingerAlbumsAndSaveResult(7, 1, 1);
        (void)workflow.crawlSingerSongsAndSaveResult(7, 1, 1);
        require(source->profiles == 1 && source->details == 1, "repeated collection repeated complete entity requests");
        require(source->song_pages == 2 && source->album_pages == 2 && source->pages == 0, "normal catalogs or complete album track requests were unexpectedly repeated");
        std::cout << "NORMAL_REQUEST_COUNTS profiles=" << source->profiles << " details=" << source->details << " song_pages=" << source->song_pages << " album_pages=" << source->album_pages << " track_pages=" << source->pages << '\n';

        const auto refreshed = completion->singerContext(7, 99, 99, true);
        require(refreshed.isSuccess() && refreshed.data()->source_song_total == 2 && refreshed.data()->source_album_total == 1, "forced singer refresh reused stale totals");
        require(source->profiles == 2, "forced refresh did not bypass the completed profile cache");
        const auto known = completion->singerContext(7, 2, 1);
        require(known.isSuccess() && source->profiles == 2, "normal collection repeated a completed profile request");
        const auto forced_albums = workflow.crawlSingerAlbumsAndSaveResult(7, 1, 1, 99, 99, true);
        require(forced_albums.success && source->profiles == 3, "album workflow did not propagate forced profile refresh");
        const auto forced_songs = workflow.crawlSingerSongsAndSaveResult(7, 1, 1, 99, 99, true);
        require(forced_songs.partial && source->profiles == 4 && source->details == 1, "song workflow forced unrelated album requests or reused stale singer profile");

        auto parallel = provider();
        use::CatalogMetadataCompletion shared({parallel, parallel, nullptr, dispatch});
        auto a = std::async(std::launch::async, [&] { return shared.singerContext(7, {}, {}); });
        auto b = std::async(std::launch::async, [&] { return shared.singerContext(7, {}, {}); });
        require(a.get().isSuccess() && b.get().isSuccess() && parallel->profiles == 1, "concurrent profile requests were not merged");

        auto broken = provider();
        broken->mock_album_detail_response.reset();
        use::CatalogMetadataCompletion retry({broken, broken, nullptr, dispatch});
        model::MetadataWriteBatch batch;
        model::AlbumSourceMetadata album;
        album.source = "migu";
        album.source_album_id = "a1";
        album.title = "专辑一";
        batch.albums = {album};
        require(!retry.completeAlbums(batch).isSuccess(), "missing detail response was hidden");
        broken->mock_album_detail_response = source->mock_album_detail_response;
        require(retry.completeAlbums(batch).isSuccess() && broken->details == 2, "failed request prevented retry");

        auto mismatched = provider();
        mismatched->mock_album_detail_response = response(R"({"data":{"albumId":"wrong","title":"其他专辑","totalCount":"0"}})");
        use::CatalogMetadataCompletion guarded({mismatched, mismatched, nullptr, dispatch});
        require(!guarded.completeAlbums(batch).isSuccess(), "another album's metadata was accepted");
        mismatched->mock_album_detail_response = source->mock_album_detail_response;
        require(guarded.completeAlbums(batch).isSuccess() && mismatched->details == 2, "mismatched response poisoned later retries");

        const auto combined = dispatch->parseSources(source->mock_singer_info_response->body);
        require(combined.profiles.size() == 1 && combined.singers.size() == 1, "single-response profile/identity split failed");

        // 曲目只携带歌手 ID 时，也要保留同响应中独立返回的完整歌手实体。
        auto attached = batch;
        attached.albums.front().detail_sync_status = 2;
        attached.albums.front().total_count = "1";
        model::SongSourceMetadata track;
        track.source = "migu";
        track.source_content_id = "attached-track";
        track.title = "独立歌手资料曲目";
        track.source_album_id = "a1";
        track.source_album_name = "专辑一";
        model::SongSourceArtistRef artist;
        artist.source_artist_id = "9";
        track.artist_refs.push_back(artist);
        attached.songs.push_back(track);
        model::SingerSourceMetadata singer;
        singer.source = "migu";
        singer.source_singer_id = "9";
        singer.name = "独立歌手资料";
        singer.source_country = "中国";
        attached.singers.push_back(singer);
        const auto independent = completion->completeAlbums(attached);
        require(independent.isSuccess() && independent.data()->singers.size() == 1 && independent.data()->singers.front().source_country == "中国", "a referenced standalone singer was discarded");

        // 公共归档只保存当前响应；即使没有详情 provider 也能正常完成歌曲元数据批次。
        const auto shared_input = dispatch->parseSources(response(R"([
            {"songId":"shared-song-1","contentId":"shared-content-1","songName":"当前单曲一","albumId":"1","album":"单曲发行","singerList":[{"id":"7","name":"入口歌手"}]},
            {"songId":"shared-song-2","contentId":"shared-content-2","songName":"当前单曲二","albumId":"1","album":"单曲发行","singerList":[{"id":"7","name":"入口歌手"}]}
        ])").body);
        use::CatalogMetadataCompletion shared_only({nullptr, nullptr, nullptr, dispatch});
        const auto kept = shared_only.completeAlbums(shared_input);
        require(kept.isSuccess() && kept.data()->songs.size() == 2 && kept.data()->albums.size() == 1 && kept.data()->albums.front().source_album_id == "1", "shared archive discarded current songs or required a detail request");
        require(!kept.data()->albums.front().songs_sync && kept.data()->albums.front().detail_sync_status == 0, "shared summary incorrectly claimed a full collection");

        auto shared_provider = provider();
        use::CatalogMetadataCompletion mixed_completion({shared_provider, shared_provider, nullptr, dispatch});
        auto mixed = shared_input;
        auto ordinary = dispatch->parseSources(shared_provider->mock_singer_songs_response->body).songs.front();
        ordinary.source_album_name = "单曲发行"; // 同名但 ID 不同的真实专辑仍补齐全部曲目。
        mixed.songs.push_back(ordinary);
        for (int repeat = 0; repeat < 2; ++repeat) {
            const auto result = mixed_completion.completeAlbums(mixed);
            require(result.isSuccess() && result.data()->songs.size() == 4 && result.data()->albums.size() == 2, "mixed response expanded shared archive or truncated real album");
            require(shared_provider->details == 1 && shared_provider->pages == 0, "shared archive triggered detail or page requests");
        }

        // 已携带的归档详情也不能因声明 1100 首而补齐整个集合。
        auto supplied_shared = *kept.data();
        supplied_shared.albums.front().detail_sync_status = 2;
        supplied_shared.albums.front().total_count = "1100";
        const auto supplied_result = mixed_completion.completeAlbums(supplied_shared);
        require(supplied_result.isSuccess() && supplied_result.data()->songs.size() == 2 && shared_provider->details == 1 && shared_provider->pages == 0, "supplied shared archive details expanded the response");

        // 同一 ID 在其他平台或数字专辑中不代表这个公共归档。
        const auto real_same_id = [&](const std::string &platform, bool digital) {
            auto input = *kept.data();
            auto &item = input.albums.front();
            item.source = platform;
            item.detail_sync_status = 2;
            item.total_count = "2";
            if (digital) {
                item.source_album_kind = 2;
                item.source_res_type = "1";
                item.source_resource_type = "5";
                item.source_material_id = "real-material";
            }
            for (auto &song : input.songs) song.source = platform;
            const auto result = shared_only.completeAlbums(input);
            require(result.isSuccess() && result.data()->albums.front().songs_sync && result.data()->albums.front().songs_sync->status == 2, "shared archive policy suppressed another platform or digital album");
        };
        real_same_id("other-platform", false);
        real_same_id("migu", true);
        std::cout << "[PASS] ordered metadata completion, shared responses, field carry, limits and retries" << std::endl;
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "[FAIL] " << error.what() << std::endl;
        return 1;
    }
}
