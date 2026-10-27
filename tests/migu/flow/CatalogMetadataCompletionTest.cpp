#include "crawler/application/use_cases/CatalogMetadataCompletion.h"

#include <atomic>
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

// 保持真实平台响应形状，同时给详情放入一首不在本批范围内的曲目。
std::shared_ptr<Provider> provider()
{
    auto value = std::make_shared<Provider>();
    value->mock_singer_info_response = response(R"({"data":{"contents":[{"view":"ZJ-SingerDetail-Item","resId":"7","txt":"入口歌手"},{"view":"ZJ-Tab-Scroll","contents":[{"view":"ZJ-Tab-Item","action":"song","txt2":"2"},{"view":"ZJ-Tab-Item","action":"album","txt2":"1"}]}]}})");
    value->mock_singer_albums_response = response(R"({"data":{"contents":[{"view":"ZJ-Album-Item","resId":"a1","resType":"0","txt":"专辑一","txt2":"入口歌手"},{"view":"ZJ-Album-Item","resId":"unselected","resType":"0","txt":"未选专辑"}]}})");
    value->mock_singer_songs_response = response(R"([{"contentId":"c1","songId":"s1","songName":"歌曲一","albumId":"a1","album":"专辑一","singerList":[{"id":"7","name":"入口歌手","nameSpelling":"rukou"}]}])");
    value->mock_album_detail_response = response(R"({"data":{"albumId":"a1","title":"专辑一","summary":"完整简介","totalCount":"2","resourceType":"2003","publishDate":"2020-01-01","albumPinyin":"zhuanjiyi","songItems":[{"contentId":"c1","songId":"s1","songName":"歌曲一","songPinyin":"gequyi","albumId":"a1","album":"专辑一","singerList":[{"id":"7","name":"入口歌手"}]},{"contentId":"unselected","songName":"不在本批的曲目","albumId":"a1","album":"专辑一","singerList":[{"id":"unknown","name":"推荐歌手"}]}]}})");
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
        require(first.albums.size() == 1 && first.songs.empty(), "album collection expanded into unselected songs");
        require(first.albums.front().summary == "完整简介" && first.albums.front().source_title_pinyin == "zhuanjiyi" && first.albums.front().total_count == "2", "album response fields were not carried");
        require(first.profiles.size() == 1 && first.profiles.front().source_song_total == 2 && first.profiles.front().source_album_total == 1, "profile totals were not committed with albums");
        const auto songs = workflow.crawlSingerSongsAndSaveResult(7, 1, 1);
        require(songs.partial && songs.written_count == 1, "a limited subset was reported as a complete catalog");
        const auto &second = writer->batches.back();
        require(second.songs.size() == 1 && second.songs.front().source_title_pinyin == "gequyi", "cached album track fields did not enrich the selected song");
        require(second.singers.size() == 1 && second.singers.front().source_singer_id == "7", "detail introduced an unselected singer");
        require(source->profiles == 1 && source->details == 1, "album/song phases requested each metadata field separately");
        (void)workflow.crawlSingerAlbumsAndSaveResult(7, 1, 1);
        (void)workflow.crawlSingerSongsAndSaveResult(7, 1, 1);
        require(source->profiles == 1 && source->details == 1, "repeated collection repeated complete entity requests");

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
        std::cout << "[PASS] ordered metadata completion, shared responses, field carry, limits and retries" << std::endl;
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "[FAIL] " << error.what() << std::endl;
        return 1;
    }
}
