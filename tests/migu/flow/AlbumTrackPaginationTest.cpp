#include "crawler/application/use_cases/AlbumDetailBackfillWorkflow.h"
#include "crawler/application/use_cases/CatalogMetadataCompletion.h"
#include "crawler/infrastructure/music/migu/parser/MiguParser.h"
#include "mocks/MockAlbumSourceRepository.h"
#include "mocks/MockMusicProvider.h"

#include <nlohmann/json.hpp>

#include <iostream>
#include <algorithm>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <tuple>
#include <utility>
#include <vector>

namespace c = orchestrator::modules::crawler;
namespace model = c::application::model;
namespace use = c::application::use_cases;
using Json = nlohmann::json;

// 离线断言不依赖 assert 开关。
void require(bool value, const char *message)
{
    if (!value) throw std::runtime_error(message);
}

// 使用实测 data.songList/totalCount/hasNext=false 形状，不包含平台账户或资源地址。
model::ProviderResponse response(const Json &json)
{
    model::ProviderResponse result;
    result.status_code = 200;
    const auto text = json.dump();
    result.body.assign(text.begin(), text.end());
    return result;
}

// 生成稳定身份和真实字段结构，支持独立验证全局顺序。
Json tracks(int first, int last, std::string album = "album-fixture")
{
    Json result = Json::array();
    for (int number = first; number <= last; ++number) {
        result.push_back({{"contentId", "track-" + std::to_string(number)}, {"songName", "曲目 " + std::to_string(number)}, {"albumId", album}, {"album", "分页专辑"}, {"duration", 180}, {"singerList", {{{"id", "singer-fixture"}, {"name", "歌手"}}}}, {"ext", {{"disc", "Disc 2"}}}});
    }
    return result;
}

// 测试替身线程不安全，仅由当前用例顺序调用，记录页号避免重复第一页。
class Provider final : public c::tests::MockMusicProvider {
public:
    int details = 0;                                              // 详情请求数，防止目录阶段重复获取
    std::map<int, model::ProviderResponse> pages;                 // 页号对应原始响应
    std::vector<std::tuple<std::string, int, int>> page_requests; // 实际身份、页号、页大小
    // 统计目录补齐是否复用已经取得的详情。
    std::optional<Response> fetchAlbumDetail(std::string id, std::string type) override
    {
        ++details;
        return MockMusicProvider::fetchAlbumDetail(std::move(id), std::move(type));
    }

    std::optional<Response> fetchAlbumSongPage(std::string id, int page, int size) override
    {
        page_requests.emplace_back(std::move(id), page, size);
        const auto found = pages.find(page);
        return found == pages.end() ? std::nullopt : std::optional<Response> {found->second};
    }
};

// 记录一次最终事务 batch，不能按页各写一份最终完成状态。
class Writer final : public c::application::ports::MetadataBatchWriter {
public:
    std::vector<model::MetadataWriteBatch> batches; // 实际提交输入
    c::application::result::ServiceResult<model::MetadataWriteResult> saveBatch(const model::MetadataWriteBatch &batch) override
    {
        batches.push_back(batch);
        return c::application::result::ServiceResult<model::MetadataWriteResult>::success({});
    }
};

// 生产 parser、调度和持久化编排配合离线 provider，无网络或数据库依赖。
class Fixture {
public:
    std::shared_ptr<Provider> provider = std::make_shared<Provider>();                                                                                                                                                                        // 当前场景的分页响应
    std::shared_ptr<Writer> writer = std::make_shared<Writer>();                                                                                                                                                                              // 原子 batch 记录器
    std::shared_ptr<c::tests::MockAlbumSourceRepository> queue = std::make_shared<c::tests::MockAlbumSourceRepository>();                                                                                                                     // 状态适配替身
    std::shared_ptr<c::infrastructure::music::migu::parser::MiguParser> parser = std::make_shared<c::infrastructure::music::migu::parser::MiguParser>();                                                                                      // 真实平台解析器
    std::shared_ptr<use::ParserDispatch> dispatch = std::make_shared<use::ParserDispatch>(parser, parser, parser, parser, parser, parser, nullptr, nullptr);                                                                                  // 同步测试调度
    std::shared_ptr<use::MetadataPersistenceService> saver = std::make_shared<use::MetadataPersistenceService>(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, use::MetadataIngestionDependencies {writer, nullptr}); // 仅启用原子写入

    // 配置详情首段和平台声明总数；数字专辑同时携带明确物料映射。
    void detail(int total, bool digital = false)
    {
        Json album = {{"albumId", "album-fixture"}, {"title", "分页专辑"}, {"totalCount", total}, {"songItems", tracks(1, (std::min)(total, 50))}, {"singerId", "singer-fixture"}, {"singer", "歌手"}, {"singerImgs", {{{"img", "https://example.test/avatar.webp"}}}}};
        if (digital) album["materialId"] = "material-fixture";
        provider->mock_album_detail_response = response({{"resource", Json::array({album})}});
    }

    // 即便 hasNext=false，也必须按声明总数继续采集。
    void page(int number, int total, int first, int last, std::string album = "album-fixture")
    {
        provider->pages[number] = response({{"code", "000000"}, {"data", {{"songList", tracks(first, last, std::move(album))}, {"totalCount", total}, {"hasNext", false}}}});
    }

    // 执行单个详情入口，分页结果与详情只提交一次。
    void run(std::string album_id = "album-fixture")
    {
        model::AlbumSourceMetadata album;
        album.source_album_id = std::move(album_id);
        use::AlbumDetailBackfillWorkflow workflow(provider, queue, dispatch, saver, nullptr, nullptr);
        require(workflow.crawlAlbumDetailAndSave(album), "pagination metadata save failed");
        require(writer->batches.size() == 1, "pagination did not use one final transaction");
    }
};

int main()
{
    try {
        Fixture complete;
        complete.detail(59);
        complete.page(2, 59, 51, 59);
        complete.run();
        const auto &batch = complete.writer->batches.front();
        require(batch.songs.size() == 59 && batch.albums.front().songs_sync->status == 2, "59-song catalog was truncated at 50");
        require(batch.singers.size() == 1 && batch.singers[0].avatar_url == "https://example.test/avatar.webp", "album singer image did not reach its independent singer entity");
        require(batch.songs.back().album_song_order == 59 && batch.songs.back().album_track_no == 59 && batch.songs.back().album_disc_no == 2, "page-local order or Disc was persisted");
        require(complete.provider->page_requests == std::vector<std::tuple<std::string, int, int>> {{"album-fixture", 2, 50}}, "complete detail prefix was fetched twice");

        Fixture multiple;
        multiple.detail(151);
        multiple.page(2, 151, 51, 100);
        multiple.page(3, 151, 101, 150);
        multiple.page(4, 151, 151, 151);
        multiple.run();
        require(multiple.writer->batches.front().songs.size() == 151 && multiple.provider->page_requests.size() == 3, "hasNext=false stopped a larger catalog early");

        Fixture repeated;
        repeated.detail(59);
        repeated.page(2, 59, 1, 50);
        repeated.run();
        require(repeated.writer->batches.front().songs.size() == 50 && repeated.writer->batches.front().albums.front().songs_sync->status == 4 && repeated.provider->page_requests.size() == 1, "repeated page caused false completion or unbounded fetch");

        Fixture missing;
        missing.detail(59);
        missing.run();
        require(missing.writer->batches.front().albums.front().songs_sync->status == 4 && missing.writer->batches.front().songs.size() == 50, "failed page lost available data or became complete");

        Fixture wrong;
        wrong.detail(59);
        wrong.page(2, 59, 51, 59, "different-album");
        wrong.run();
        require(wrong.writer->batches.front().songs.size() == 50 && wrong.writer->batches.front().albums.front().songs_sync->status == 4, "different album page contaminated the target");

        Fixture changed;
        changed.detail(59);
        changed.page(2, 60, 51, 60);
        changed.run();
        require(changed.writer->batches.front().albums.front().songs_sync->status == 4, "changed total became complete");

        Fixture digital;
        digital.detail(59, true);
        digital.page(2, 59, 51, 59, "material-fixture");
        digital.run();
        require(std::get<0>(digital.provider->page_requests.front()) == "material-fixture" && digital.writer->batches.front().songs.back().source_album_id == "album-fixture", "digital material identity escaped canonical context");

        Fixture short_album;
        short_album.detail(2);
        short_album.run();
        require(short_album.provider->page_requests.empty(), "complete small album fetched extra pages");

        // 真实响应中的多个不同 songId 共用占位内容 ID，必须全部进入同一最终 batch。
        Fixture placeholder;
        placeholder.detail(3);
        auto placeholder_detail = Json::parse(placeholder.provider->mock_album_detail_response->body);
        for (int index = 0; index < 3; ++index) {
            auto &song = placeholder_detail["resource"][0]["songItems"][index];
            song["songId"] = "placeholder-song-" + std::to_string(index);
            song["contentId"] = "600000000000000000";
        }
        placeholder.provider->mock_album_detail_response = response(placeholder_detail);
        placeholder.run();
        const auto &placeholder_batch = placeholder.writer->batches.front();
        require(placeholder_batch.songs.size() == 3 && placeholder_batch.albums.front().songs_sync->status == 2 && placeholder_batch.albums.front().songs_sync->received_count == 3, "placeholder content ID merged distinct album tracks or left false partial status");
        require(placeholder.provider->page_requests.empty(), "complete placeholder album fetched unnecessary pages");
        require(placeholder_batch.songs.front().source_content_id.empty() && placeholder_batch.songs.back().source_song_id == "placeholder-song-2", "placeholder identities were reintroduced by batch merging");

        // 回补公共归档在请求详情前直接跳过，不能先引入跨歌手首段再停止分页。
        Fixture archive;
        archive.detail(59);
        auto archive_detail = Json::parse(archive.provider->mock_album_detail_response->body);
        auto &archive_album = archive_detail["resource"][0];
        archive_album["albumId"] = "1";
        archive_album["title"] = "单曲发行";
        for (auto &song : archive_album["songItems"]) {
            song["albumId"] = "1";
            song["album"] = "单曲发行";
        }
        archive.provider->mock_album_detail_response = response(archive_detail);
        archive.page(2, 59, 51, 59, "1");
        model::AlbumSourceMetadata archive_source;
        archive_source.source_album_id = "1";
        archive_source.title = "单曲发行";
        use::AlbumDetailBackfillWorkflow archive_workflow(archive.provider, archive.queue, archive.dispatch, archive.saver, nullptr, nullptr);
        require(archive_workflow.crawlAlbumDetailAndSave(archive_source) && archive_workflow.crawlAlbumSongsAndSave(archive_source), "shared archive no-op failed");
        require(archive.provider->details == 0 && archive.provider->page_requests.empty() && archive.writer->batches.empty(), "shared archive backfill fetched or saved unrelated tracks");
        require(archive.queue->update_detail_calls == 0 && archive.queue->update_songs_calls == 0, "shared archive no-op modified persisted completion state");

        // 同名真实专辑仍必须保存详情首段及全部缺页。
        Fixture same_name;
        same_name.detail(59);
        auto same_name_detail = Json::parse(same_name.provider->mock_album_detail_response->body);
        same_name_detail["resource"][0]["title"] = "单曲发行";
        same_name.provider->mock_album_detail_response = response(same_name_detail);
        same_name.page(2, 59, 51, 59);
        same_name.run();
        require(same_name.writer->batches.front().songs.size() == 59 && same_name.provider->details == 1, "same-name real album lost its full catalog");

        const auto invalid_total = response({{"data", {{"songList", tracks(51, 59)}, {"totalCount", 59.5}}}});
        require(!short_album.parser->parseAlbumSongPage(invalid_total.body).source_song_total, "fractional total was silently truncated");

        // 正常目录入口必须复用同一详情和缺页，不再要求另行触发曲目 backfill。
        Fixture catalog;
        catalog.detail(59);
        catalog.page(2, 59, 51, 59);
        use::CatalogMetadataCompletion completion({catalog.provider, catalog.provider, nullptr, catalog.dispatch});
        model::MetadataWriteBatch input;
        model::AlbumSourceMetadata album;
        album.source_album_id = "album-fixture";
        album.title = "分页专辑";
        input.albums.push_back(album);
        const auto completed = completion.completeAlbums(input);
        require(completed.isSuccess() && completed.data()->songs.size() == 59 && completed.data()->albums.front().songs_sync->status == 2, "catalog completion discarded album tracks");
        require(completed.data()->singers.front().avatar_url == "https://example.test/avatar.webp", "associated singer metadata was discarded");
        require(completion.completeAlbums(input).isSuccess() && catalog.provider->details == 1 && catalog.provider->page_requests.size() == 1 && std::get<1>(catalog.provider->page_requests.front()) == 2, "cached catalog completion fetched detail or pages again");

        Fixture inline_detail;
        inline_detail.detail(59, true);
        inline_detail.page(2, 59, 51, 59, "material-fixture");
        use::CatalogMetadataCompletion inline_completion({inline_detail.provider, inline_detail.provider, nullptr, inline_detail.dispatch});
        const auto supplied = inline_completion.completeAlbums(inline_detail.dispatch->parseSources(inline_detail.provider->mock_album_detail_response->body));
        require(supplied.isSuccess() && supplied.data()->songs.size() == 59 && inline_detail.provider->details == 0 && std::get<0>(inline_detail.provider->page_requests.front()) == "material-fixture", "supplied detail was fetched again or material page identity was lost");

        Fixture retry_catalog;
        retry_catalog.detail(59);
        use::CatalogMetadataCompletion retry_completion({retry_catalog.provider, retry_catalog.provider, nullptr, retry_catalog.dispatch});
        const auto partial = retry_completion.completeAlbums(input);
        require(partial.isSuccess() && partial.data()->songs.size() == 50 && partial.data()->albums.front().songs_sync->status == 4, "catalog pagination failure discarded available metadata");
        retry_catalog.page(2, 59, 51, 59);
        const auto retried = retry_completion.completeAlbums(input);
        require(retried.isSuccess() && retried.data()->songs.size() == 59 && retried.data()->albums.front().songs_sync->status == 2, "failed page blocked a later complete response");
        std::cout << "PASS: album pages, identity, totals, no-progress bounds and atomic completion\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
