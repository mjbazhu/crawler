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

int main()
{
    try {
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
