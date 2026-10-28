#include "crawler/application/use_cases/AlbumDetailBackfillWorkflow.h"
#include "crawler/application/use_cases/BatchTaskManager.h"
#include "crawler/application/use_cases/MetadataBatchBuilder.h"
#include "crawler/application/use_cases/SearchCollectionWorkflow.h"
#include "crawler/application/use_cases/UrlIngestionWorkflow.h"
#include "crawler/infrastructure/music/migu/parser/MiguParser.h"
#include "mocks/MockAlbumSourceRepository.h"
#include "mocks/MockMusicProvider.h"

#include <iostream>
#include <stdexcept>
#include <utility>

namespace crawler = orchestrator::modules::crawler;
namespace model = crawler::application::model;
namespace cases = crawler::application::use_cases;
namespace parser = crawler::infrastructure::music::migu::parser;

// 测试失败不依赖 assert 或构建配置。
void require(bool value, const char *message)
{
    if (!value) throw std::runtime_error(message);
}

// 记录真实分流后的事务单位，不提供第二套公共表仓储。
class Writer final : public crawler::application::ports::MetadataBatchWriter {
public:
    std::vector<model::MetadataWriteBatch> batches; // 按调用顺序记录输入
    bool fail = false;                              // 整批回滚 fixture
    // 验证调用方不会因成功取数而忽略写入失败。
    crawler::application::result::ServiceResult<model::MetadataWriteResult> saveBatch(const model::MetadataWriteBatch &batch) override
    {
        batches.push_back(batch);
        using Result = crawler::application::result::ServiceResult<model::MetadataWriteResult>;
        return fail ? Result::failure(crawler::application::result::ServiceErrorType::InternalError, "fixture failure") : Result::success({});
    }
};

// 生产写入路径不应再解析第二套领域结果。
class SourceOnlyParser final : public parser::MiguParser {
public:
    // 旧领域结果解析一旦被调用就让测试失败。
    std::vector<crawler::domain::ParseResult> parse(const std::vector<char> &) const override
    {
        throw std::runtime_error("duplicate domain parse");
    }
};

// 保留真实响应形状，但所有身份、名称和地址均为离线 fixture。
model::ProviderResponse response(std::string_view text)
{
    model::ProviderResponse value;
    value.status_code = 200;
    value.body.assign(text.begin(), text.end());
    return value;
}

// 观察详情请求，验证公共归档在发出请求之前就被排除。
class SearchProvider final : public crawler::tests::MockMusicProvider {
public:
    int details = 0; // 搜索触发的专辑详情请求数

    // 真实专辑继续使用同一份离线详情响应。
    std::optional<Response> fetchAlbumDetail(std::string id, std::string type) override
    {
        ++details;
        return MockMusicProvider::fetchAlbumDetail(std::move(id), std::move(type));
    }
};

// 公共归档只保留搜索响应；同名真实专辑和明确数字身份仍完整写入返回曲目。
void verifySearchArchiveBoundary()
{
    auto provider = std::make_shared<SearchProvider>();
    auto parser = std::make_shared<SourceOnlyParser>();
    auto dispatch = std::make_shared<cases::ParserDispatch>(parser, parser, parser, parser, parser, parser, nullptr, nullptr);
    auto writer = std::make_shared<Writer>();
    auto persistence = std::make_shared<cases::MetadataPersistenceService>(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, cases::MetadataIngestionDependencies {writer, nullptr});
    cases::SearchCollectionWorkflow search(provider, provider, provider, dispatch, persistence, nullptr, nullptr);
    provider->mock_song_search_response = response(R"([{"songId":"carried-song","contentId":"carried-content","songName":"当前单曲","albumId":"1","album":"单曲发行","singerList":[{"id":"7","name":"入口歌手"}]}])");
    provider->mock_album_search_response = response(R"({"data":{"result":[{"id":"1","name":"单曲发行","resourceType":"2003"}]}})");
    provider->mock_album_detail_response = response(R"({"resource":[{"albumId":"1","title":"单曲发行","totalCount":"1100","songItems":[{"songId":"unrelated-song","contentId":"unrelated-content","songName":"其他歌手单曲"}]}]})");
    for (int repeat = 0; repeat < 2; ++repeat) {
        writer->batches.clear();
        require(search.searchAndSave("单曲发行", 1, 1), "shared archive search failed");
        require(provider->details == 0 && writer->batches.size() == 2, "search expanded the shared archive detail response");
        require(writer->batches.front().songs.size() == 1 && writer->batches.front().songs.front().source_song_id == "carried-song" && writer->batches.front().songs.front().source_album_id == "1", "shared archive guard discarded current-response songs or relationships");
        require(writer->batches.back().albums.size() == 1 && writer->batches.back().albums.front().source_album_id == "1" && writer->batches.back().songs.empty(), "shared archive summary was dropped or contaminated");
    }
    provider->mock_song_search_response = response("[]");
    provider->mock_album_search_response = response(R"({"data":{"result":[{"id":"real-album","name":"单曲发行","resourceType":"2003"}]}})");
    provider->mock_album_detail_response = response(R"({"resource":[{"albumId":"real-album","title":"单曲发行","totalCount":"2","songItems":[{"songId":"real-1","songName":"真实曲目一"},{"songId":"real-2","songName":"真实曲目二"}]}]})");
    writer->batches.clear();
    require(search.searchAndSave("单曲发行", 1, 1) && provider->details == 1 && writer->batches.back().songs.size() == 2, "shared archive guard suppressed a same-name real album");
    provider->mock_album_search_response = response(R"({"data":{"result":[{"id":"1","name":"单曲发行","resourceType":"5"}]}})");
    provider->mock_album_detail_response = response(R"({"resource":[{"contentId":"1","materialId":"digital-material","resourceType":"5","title":"单曲发行","totalCount":"2","songItems":[{"songId":"digital-1","songName":"数字曲目一"},{"songId":"digital-2","songName":"数字曲目二"}]}]})");
    writer->batches.clear();
    require(search.searchAndSave("单曲发行", 1, 1) && provider->details == 2 && writer->batches.back().songs.size() == 2, "shared archive guard suppressed an explicit digital identity");
    std::cout << "SEARCH_ARCHIVE_COUNTS archive_details=0 real_and_digital_details=" << provider->details << '\n';
}

// 错误状态或其他专辑的响应不能成为成功入库依据；修正后可正常重试。
void verifySearchResponseFailures()
{
    auto provider = std::make_shared<SearchProvider>();
    auto parser = std::make_shared<SourceOnlyParser>();
    auto dispatch = std::make_shared<cases::ParserDispatch>(parser, parser, parser, parser, parser, parser, nullptr, nullptr);
    auto writer = std::make_shared<Writer>();
    auto persistence = std::make_shared<cases::MetadataPersistenceService>(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, cases::MetadataIngestionDependencies {writer, nullptr});
    cases::SearchCollectionWorkflow search(provider, provider, provider, dispatch, persistence, nullptr, nullptr);
    provider->mock_song_search_response = response(R"([{"songId":"error-song","songName":"错误响应中的歌曲"}])");
    provider->mock_song_search_response->status_code = 503;
    provider->mock_album_search_response = response(R"({"data":{"result":[{"id":"wanted-album","name":"目标专辑","resourceType":"2003"}]}})");
    require(!search.searchAndSave("fixture", 1, 1) && writer->batches.empty(), "non-2xx song search response was persisted");
    provider->mock_song_search_response = response("[]");
    provider->mock_album_search_response->status_code = 503;
    require(!search.searchAndSave("fixture", 1, 1) && writer->batches.empty(), "non-2xx album search response was persisted");
    provider->mock_album_search_response->status_code = 200;
    provider->mock_album_detail_response = response(R"({"resource":[{"albumId":"wanted-album","title":"目标专辑","totalCount":"1","songItems":[{"songId":"wanted-song","songName":"目标曲目"}]}]})");
    provider->mock_album_detail_response->status_code = 503;
    require(!search.searchAndSave("fixture", 1, 1) && writer->batches.size() == 1 && writer->batches.back().songs.empty(), "non-2xx album detail response was persisted");
    writer->batches.clear();
    provider->mock_album_detail_response = response(R"({"resource":[{"albumId":"wrong-album","title":"其他专辑","totalCount":"1","songItems":[{"songId":"wrong-song","songName":"其他专辑曲目"}]}]})");
    require(!search.searchAndSave("fixture", 1, 1) && writer->batches.size() == 1 && writer->batches.back().songs.empty(), "another album's detail was accepted by search");
    writer->batches.clear();
    provider->mock_album_detail_response = response(R"({"resource":[{"albumId":"wanted-album","title":"目标专辑","totalCount":"1","songItems":[{"songId":"wanted-song","songName":"目标曲目"}]}]})");
    require(search.searchAndSave("fixture", 1, 1) && writer->batches.size() == 2 && writer->batches.back().songs.front().source_song_id == "wanted-song", "search did not recover after a corrected response");
    writer->batches.clear();
    provider->mock_album_search_response = response(R"({"data":{"result":[{"id":"material-alias","name":"数字专辑","resourceType":"5"}]}})");
    provider->mock_album_detail_response = response(R"({"resource":[{"contentId":"canonical-album","materialId":"material-alias","resourceType":"5","title":"数字专辑","totalCount":"1","songItems":[{"songId":"digital-song","songName":"数字专辑曲目"}]}]})");
    require(search.searchAndSave("fixture", 1, 1) && writer->batches.back().albums.front().source_album_id == "canonical-album", "explicit digital material mapping failed search identity validation");
}

int main()
{
    try {
        verifySearchArchiveBoundary();
        verifySearchResponseFailures();
        auto provider = std::make_shared<crawler::tests::MockMusicProvider>();
        auto source_parser = std::make_shared<SourceOnlyParser>();
        auto dispatch = std::make_shared<cases::ParserDispatch>(source_parser, source_parser, source_parser, source_parser, source_parser, source_parser, nullptr, nullptr);
        auto writer = std::make_shared<Writer>();
        auto persistence = std::make_shared<cases::MetadataPersistenceService>(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, cases::MetadataIngestionDependencies {writer, nullptr});
        const auto song_response = response(R"([{"contentId":"content-1","songId":"song-1","songName":"歌曲一","albumId":"album-1","album":"关联专辑","singerList":[{"id":"singer-1","name":"歌手一","img":"https://example.test/singer.webp"}],"ext":{"lrcUrl":"https://example.test/lrc"}},{"contentId":"content-2","songId":"song-2","songName":"歌曲二"}])");
        const auto album_response = response(R"({"code":"000000","data":{"result":[{"id":"album-2","name":"专辑一","resourceType":"2003","singer":"展示名称","albumPinyin":"zhuanji","publishDate":"2020-01-01","imgItems":[{"img":"https://example.test/album.webp"}]},{"id":"album-3","name":"专辑二","resourceType":"5"}]}})");
        provider->mock_song_search_response = song_response;
        provider->mock_album_search_response = album_response;
        cases::SearchCollectionWorkflow search(provider, provider, nullptr, dispatch, persistence, nullptr, nullptr);
        require(search.searchAndSave("fixture", 1, 1), "search persistence failed");
        require(writer->batches.size() == 2, "search wrote an extra set of domain entities");
        require(writer->batches[0].songs.size() == 1 && writer->batches[0].songs[0].source_content_id == "content-1", "search limit was applied after persistence");
        require(writer->batches[0].singers.size() == 1 && !writer->batches[0].singers[0].avatar_url.empty(), "search singer image was not routed to its owner");
        require(writer->batches[0].songs[0].lyric_url == "https://example.test/lrc", "nested temporary lyric address was lost");
        require(writer->batches[1].albums.size() == 1 && writer->batches[1].albums[0].source_album_id == "album-2", "data.result album shape or search limit was lost");
        require(writer->batches[1].albums[0].source_title_pinyin == "zhuanji", "album search pinyin was lost");

        provider->mock_url_response = song_response;
        cases::UrlIngestionWorkflow url(provider, dispatch, persistence);
        writer->batches.clear();
        require(url.processUrl("fixture-url", 1) && writer->batches.size() == 1, "URL ingress did not use a single source transaction");
        writer->fail = true;
        require(!url.processUrl("fixture-url", 1), "URL ingress hid a failed transaction");
        writer->fail = false;

        model::MetadataWriteBatch sparse;
        model::AlbumSourceMetadata rich;
        rich.source_album_id = "album-2";
        rich.title = "专辑一";
        rich.summary = "完整资料";
        rich.source_title_pinyin = "zhuanji";
        rich.detail_sync_status = 2;
        auto summary = rich;
        summary.summary.clear();
        summary.source_title_pinyin.clear();
        summary.detail_sync_status = 0;
        sparse.albums = {rich, summary};
        const auto merged = cases::buildMetadataWriteBatch(sparse);
        require(merged.albums.size() == 1 && merged.albums[0].summary == "完整资料" && merged.albums[0].source_title_pinyin == "zhuanji", "sparse duplicate overwrote complete metadata");

        auto queue = std::make_shared<crawler::tests::MockAlbumSourceRepository>();
        provider->mock_album_detail_response = response(R"({"resource":[{"contentId":"album-2","title":"专辑一","totalCount":"3","songItems":[{"contentId":"detail-content","songName":"详情歌曲"}]}],"data":{"contents":[{"view":"ZJ-Singer-Item","resId":"independent-singer","txt":"独立歌手"},{"view":"ZJ-Album-Item","resId":"related-album","resType":"0","txt":"推荐专辑"}]}})");
        cases::AlbumDetailBackfillWorkflow detail(provider, queue, dispatch, persistence, nullptr, nullptr);
        require(detail.crawlAlbumDetailAndSave(rich), "detail ingress failed");
        require(writer->batches.back().singers.size() == 1 && writer->batches.back().singers[0].source_singer_id == "independent-singer", "detail workflow discarded an independent singer");
        require(writer->batches.back().albums[0].detail_sync_status == 2 && writer->batches.back().albums[0].songs_sync->status == 4, "detail and partial song result were not submitted together");
        require(queue->update_detail_calls == 1 && queue->update_songs_calls == 0, "atomic detail result was redundantly rewritten after commit");
        const auto &related = writer->batches.back().albums.back();
        require(related.source_album_id == "related-album" && related.detail_sync_status == 0 && !related.songs_sync, "recommended album summary was falsely marked complete");
        require(detail.crawlAlbumSongsAndSave(rich), "album song ingress failed");
        require(writer->batches.back().albums[0].songs_sync->status == 4 && writer->batches.back().albums[0].songs_sync->received_count == 1, "partial song result was inferred from existing relations");
        writer->fail = true;
        require(!detail.crawlAlbumDetailAndSave(rich) && queue->last_detail_status == 3, "failed detail transaction was reported as completed");
        writer->fail = false;

        provider->mock_album_detail_response = response(R"({"resource":[{"albumId":"album-2","title":"专辑一","totalCount":"1","songItems":[{"contentId":"same-song","songName":"重复卡片"},{"contentId":"same-song","songName":"重复卡片"}]}]})");
        require(detail.crawlAlbumDetailAndSave(rich), "complete response failed");
        require(writer->batches.back().albums[0].songs_sync->status == 2 && writer->batches.back().albums[0].songs_sync->received_count == 1, "duplicate cards inflated the completed song count");
        provider->mock_album_detail_response = response(R"({"resource":[{"albumId":"album-2","title":"专辑一","totalCount":"0","songItems":[]}]})");
        require(detail.crawlAlbumDetailAndSave(rich) && writer->batches.back().albums[0].songs_sync->status == 2, "declared empty album was not completed");
        provider->mock_album_detail_response = response(R"({"resource":[{"albumId":"album-2","title":"专辑一","songItems":[]}]})");
        require(detail.crawlAlbumSongsAndSave(rich), "unknown-total response persistence failed");
        require(writer->batches.back().albums[0].songs_sync->status == 4 && !writer->batches.back().albums[0].songs_sync->error.empty(), "unknown total was falsely marked complete");
        const auto before_mismatch = writer->batches.size();
        provider->mock_album_detail_response = response(R"({"resource":[{"albumId":"different-album","title":"错身份"}]})");
        require(!detail.crawlAlbumDetailAndSave(rich) && writer->batches.size() == before_mismatch, "response for another album was persisted");

        cases::BatchTaskManager manager(nullptr, nullptr, nullptr, [] { return 1; }, [] { return std::chrono::milliseconds(1000); });
        bool called = false;
        const auto handler = [&](std::string, int) { called = true; return cases::BatchTaskManager::TargetResult{}; };
        const auto empty_id = manager.start({}, 1, "validated-queue", "{}", "{}", "", handler, true);
        const auto empty = manager.progress(empty_id);
        require(empty && empty->state_str == "Completed" && empty->total == 0 && empty->failed == 0 && !called, "empty validated queue became a failed task");
        const auto invalid_id = manager.start({}, 1, "invalid-explicit-input", "{}", "{}", "", handler);
        require(manager.progress(invalid_id)->state_str == "Failed", "invalid explicit empty input lost validation");

        auto profile = source_parser->parseSingerSourceProfile(response(R"({"data":{"contents":[{"view":"ZJ-SingerDetail-Item","resId":"singer-1","txt":"歌手一","img":"https://example.test/avatar.png"},{"view":"ZJ-Tab-Scroll","contents":[{"view":"ZJ-Tab-Item","action":"song","txt2":"10"}]}]}})").body);
        require(profile.metadata && profile.metadata->name == "歌手一", "profile ignored entity data in the current response");
        require(persistence->saveSingerSourceProfile(profile), "profile persistence failed");
        require(writer->batches.back().profiles.size() == 1 && writer->batches.back().singers.size() == 1, "profile and entity were not committed together");
        std::cout << "PASS: ingress limits, rollback, sparse merge, full detail batch, shared song result, unknown totals and empty queue semantics\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
