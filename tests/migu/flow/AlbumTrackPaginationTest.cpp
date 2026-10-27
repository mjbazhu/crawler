#include "crawler/application/use_cases/AlbumDetailBackfillWorkflow.h"
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
    std::map<int, model::ProviderResponse> pages;                 // 页号对应原始响应
    std::vector<std::tuple<std::string, int, int>> page_requests; // 实际身份、页号、页大小
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
    std::shared_ptr<Provider> provider = std::make_shared<Provider>();                                                                                                                                                                                 // 当前场景的分页响应
    std::shared_ptr<Writer> writer = std::make_shared<Writer>();                                                                                                                                                                                       // 原子 batch 记录器
    std::shared_ptr<c::tests::MockAlbumSourceRepository> queue = std::make_shared<c::tests::MockAlbumSourceRepository>();                                                                                                                              // 状态适配替身
    std::shared_ptr<c::infrastructure::music::migu::parser::MiguParser> parser = std::make_shared<c::infrastructure::music::migu::parser::MiguParser>();                                                                                               // 真实平台解析器
    std::shared_ptr<use::ParserDispatch> dispatch = std::make_shared<use::ParserDispatch>(parser, parser, parser, parser, parser, parser, nullptr, nullptr);                                                                                           // 同步测试调度
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
    void run()
    {
        model::AlbumSourceMetadata album;
        album.source_album_id = "album-fixture";
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

        const auto invalid_total = response({{"data", {{"songList", tracks(51, 59)}, {"totalCount", 59.5}}}});
        require(!short_album.parser->parseAlbumSongPage(invalid_total.body).source_song_total, "fractional total was silently truncated");
        std::cout << "PASS: album pages, identity, totals, no-progress bounds and atomic completion\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
