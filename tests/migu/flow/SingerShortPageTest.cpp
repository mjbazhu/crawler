#include "crawler/application/use_cases/SingerCatalogWorkflow.h"
#include "crawler/infrastructure/music/migu/parser/MiguParser.h"
#include "mocks/MockMusicProvider.h"

#include <nlohmann/json.hpp>

#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace c = orchestrator::modules::crawler;
namespace model = c::application::model;
namespace cases = c::application::use_cases;

namespace {

// 所有断言在发布构建中也执行。
void require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

// 原始响应经过生产 parser 和 application，避免测试只复制终止条件。
class Provider final : public c::tests::MockMusicProvider {
public:
    std::map<int, std::string> songs;  // 歌曲分页原始响应
    std::map<int, std::string> albums; // 专辑分页原始响应
    std::vector<int> song_requests;    // 实际请求的歌曲页
    std::vector<int> album_requests;   // 实际请求的专辑页
    int failed_page = 0;               // 需要返回 HTTP 503 的页

    // 缺失 fixture 返回明确空页，不能把不存在的分页当成网络错误。
    std::optional<Response> fetchSingerSongsPage(std::uint64_t, int page) override
    {
        song_requests.push_back(page);
        return make(songs, page);
    }

    // 专辑目录复用同一有界采集规则。
    std::optional<Response> fetchSingerAlbumsPage(std::uint64_t, int page) override
    {
        album_requests.push_back(page);
        return make(albums, page);
    }

private:
    // 只控制外部响应，不代替分页决策或模型解析。
    Response make(const std::map<int, std::string> &pages, int page) const
    {
        Response result;
        result.status_code = page == failed_page ? 503 : 200;
        const auto found = pages.find(page);
        const std::string raw = found == pages.end() ? R"({"data":{"contents":[]}})" : found->second;
        result.body.assign(raw.begin(), raw.end());
        return result;
    }
};

// 事务替身只记录实际经过生产 batch 构建的数量，不参与分页判断。
class Writer final : public c::application::ports::MetadataBatchWriter {
public:
    std::size_t songs = 0;  // 实际交给 writer 的歌曲数
    std::size_t albums = 0; // 实际交给 writer 的专辑数

    // 入库失败路径已有独立事务测试，本测试专注分页完整度与请求上限。
    c::application::result::ServiceResult<model::MetadataWriteResult> saveBatch(const model::MetadataWriteBatch &batch) override
    {
        songs += batch.songs.size();
        albums += batch.albums.size();
        return c::application::result::ServiceResult<model::MetadataWriteResult>::success({});
    }
};

// 保留真实的歌曲列表与专辑卡片结构，中间页可以短于固定页大小。
std::string page(bool songs, int number, int count)
{
    auto entries = nlohmann::json::array();
    for (int index = 0; index < count; ++index) {
        const auto id = std::to_string(number * 100 + index);
        if (songs)
            entries.push_back({{"songName", "分页歌曲" + id}, {"contentId", id}, {"songId", id}});
        else
            entries.push_back({{"view", "ZJ-Album-Item"}, {"resId", id}, {"txt", "分页专辑" + id}, {"resType", "0"}});
    }
    nlohmann::json contents = songs ? nlohmann::json::array({{{"contents", entries}}}) : entries;
    return nlohmann::json({{"data", {{"contents", contents}}}}).dump();
}

// 每个用例独占 workflow 状态，测试时不启用线程池或真实网络。
class Fixture {
public:
    Fixture()
    {
        auto parser = std::make_shared<c::infrastructure::music::migu::parser::MiguParser>();
        auto dispatch = std::make_shared<cases::ParserDispatch>(parser, parser, parser, parser, parser, parser, nullptr, nullptr);
        auto persistence = std::make_shared<cases::MetadataPersistenceService>(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, cases::MetadataIngestionDependencies {writer, {}});
        workflow = std::make_unique<cases::SingerCatalogWorkflow>(provider, dispatch, persistence, nullptr, nullptr);
        provider->songs = {{1, page(true, 1, 50)}, {2, page(true, 2, 49)}, {3, page(true, 3, 25)}};
        provider->albums = {{1, page(false, 1, 10)}, {2, page(false, 2, 9)}, {3, page(false, 3, 2)}};
    }

    std::shared_ptr<Provider> provider = std::make_shared<Provider>(); // 唯一外部接口替身
    std::shared_ptr<Writer> writer = std::make_shared<Writer>();       // 事务输入记录
    std::unique_ptr<cases::SingerCatalogWorkflow> workflow;            // 生产目录采集流程
};

// 已知总数时不能在中间短页停止，顺序和并发窗口都必须得到完整结果。
void verifyShortPages()
{
    for (int window : {1, 4}) {
        Fixture songs;
        const auto result = songs.workflow->crawlSingerSongsAndSaveResult(42, 0, window, 124, 21);
        require(result.success && !result.partial && result.written_count == 124 && songs.writer->songs == 124, "intermediate 49-song page truncated known catalog");
        require(songs.provider->song_requests.back() == (window == 1 ? 3 : 4), "short-page collection made unnecessary page windows");
        Fixture albums;
        const auto album_result = albums.workflow->crawlSingerAlbumsAndSaveResult(42, 0, window, 124, 21);
        require(album_result.success && album_result.written_count == 21 && albums.writer->albums == 21, "intermediate 9-album page truncated known catalog");
    }
}

// 真实空页仍终止，未满足的平台总数必须保持部分完成，不能编造缺失对象。
void verifyIncompleteAndLimit()
{
    Fixture incomplete;
    const auto result = incomplete.workflow->crawlSingerSongsAndSaveResult(42, 0, 1, 125);
    require(!result.success && result.partial && result.written_count == 124 && incomplete.provider->song_requests.back() == 4, "empty final page or unknown missing item was treated as complete");
    Fixture limited;
    const auto limited_result = limited.workflow->crawlSingerSongsAndSaveResult(42, 75, 1, 124);
    require(!limited_result.success && limited_result.partial && limited.writer->songs == 75 && limited.provider->song_requests.back() == 2, "explicit limit was bypassed");
    Fixture failed;
    failed.provider->failed_page = 2;
    const auto failed_result = failed.workflow->crawlSingerSongsAndSaveResult(42, 0, 1, 124);
    require(!failed_result.success && failed_result.partial && failed_result.error == "failed to fetch singer song page", "HTTP failure was mistaken for a valid short page");
}

} // namespace

int main()
{
    try {
        verifyShortPages();
        verifyIncompleteAndLimit();
        std::cout << "PASS: short middle pages, empty termination, known incomplete counts, explicit limits and HTTP failures\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
