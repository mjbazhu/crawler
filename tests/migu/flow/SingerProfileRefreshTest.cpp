#include "crawler/application/use_cases/SingerCatalogWorkflow.h"
#include "crawler/application/use_cases/BatchTaskManager.h"
#include "crawler/infrastructure/music/migu/parser/MiguParser.h"
#include "mocks/MockMusicProvider.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <chrono>
#include <fstream>
#include <functional>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace c = orchestrator::modules::crawler;
namespace model = c::application::model;
namespace use = c::application::use_cases;
namespace {

// 断言不依赖 NDEBUG，失败时直接返回非零退出码。
void require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

// 观察原子输入和持久化失败，不写真实数据库。
class Writer final : public c::application::ports::MetadataBatchWriter {
public:
    bool fail = false;                              // 模拟整批写入失败
    std::vector<model::MetadataWriteBatch> batches; // 成功尝试的事务输入

    // 资料获取成功不能掩盖事务提交失败。
    c::application::result::ServiceResult<model::MetadataWriteResult> saveBatch(const model::MetadataWriteBatch &batch) override
    {
        using Result = c::application::result::ServiceResult<model::MetadataWriteResult>;
        batches.push_back(batch);
        return fail ? Result::failure(c::application::result::ServiceErrorType::InternalError, "fixture persistence failed") : Result::success({});
    }
};

// 资料刷新只能请求整份资料，不能顺带抓目录或图片。
class Provider final : public c::tests::MockMusicProvider {
public:
    int profiles = 0; // 整份资料请求数

    // 每次显式刷新都获取当前资料响应。
    std::optional<Response> fetchSingerInfo(std::uint64_t id) override
    {
        ++profiles;
        return MockMusicProvider::fetchSingerInfo(id);
    }

    // 禁止把补资料扩大为歌曲目录采集。
    std::optional<Response> fetchSingerSongsPage(std::uint64_t, int) override
    {
        throw std::runtime_error("profile refresh requested songs");
    }

    // 禁止把补资料扩大为专辑目录采集。
    std::optional<Response> fetchSingerAlbumsPage(std::uint64_t, int) override
    {
        throw std::runtime_error("profile refresh requested albums");
    }
};

// 返回身份、两个总数和头像，分类及拼音保持上游未知。
model::ProviderResponse response(std::string id = "7", int status = 200)
{
    nlohmann::json root = {{"code", "000000"}, {"data", {{"contents", nlohmann::json::array({{{"view", "ZJ-SingerDetail-Item"}, {"resId", id}, {"txt", "关联歌手"}, {"img", "https://example.test/avatar.png"}}, {{"view", "ZJ-Tab-Scroll"}, {"contents", nlohmann::json::array({{{"view", "ZJ-Tab-Item"}, {"action", "song"}, {"txt2", "0"}}, {{"view", "ZJ-Tab-Item"}, {"action", "album"}, {"txt2", "12"}}})}}})}}}};
    model::ProviderResponse value;
    value.status_code = status;
    const auto text = root.dump();
    value.body.assign(text.begin(), text.end());
    return value;
}

// 只过滤已核验的默认歌手头像，其他图片及资料字段保持有效。
void verifyDefaultAvatars()
{
    c::infrastructure::music::migu::parser::MiguParser parser;
    const std::string path = "/prod/file-service/file-down/bcb5ddaf77828caee4eddc172edaa105/33f90367b2251b76a025d9dadf2c6467/31e311fd0affc0ae0809011e265f79a5";
    const std::string absolute = "https://d.musicapp.migu.cn" + path;
    for (const auto &url : {absolute, "http://d.musicapp.migu.cn" + path, "//d.musicapp.migu.cn" + path, path + "?size=240#avatar"}) {
        auto data = response();
        auto root = nlohmann::json::parse(data.body);
        root["data"]["contents"][0]["img"] = url;
        const auto text = root.dump();
        const std::vector<char> bytes(text.begin(), text.end());
        const auto profile = parser.parseSingerSourceProfile(bytes);
        require(profile.metadata && profile.metadata->avatar_url.empty() && profile.source_song_total == 0 && profile.source_album_total == 12, "default avatar was not excluded independently of valid profile totals");
        const auto batch = parser.parseMetadataBatch(bytes);
        require(batch.singers.size() == 1 && batch.singers.front().avatar_url.empty(), "batch parser retained the default avatar");
    }
    nlohmann::json index = {{"data", {{"contents", nlohmann::json::array({{{"view", "ZJ-Singer-Item"}, {"resId", "7"}, {"txt", "关联歌手"}, {"img", absolute}}})}}}};
    auto text = index.dump();
    const auto sources = parser.parseSingerSources(std::vector<char>(text.begin(), text.end()), "huayu-nan");
    require(sources.size() == 1 && sources.front().avatar_url.empty(), "index parser retained the default avatar");
    nlohmann::json song = {{"view", "ZJ-Singer-Song-Item"}, {"songItem", {{"songId", "42"}, {"songName", "头像测试"}, {"img3", absolute}, {"singerList", nlohmann::json::array({{{"id", "7"}, {"name", "关联歌手"}, {"img", absolute}}})}}}};
    text = song.dump();
    const auto songs = parser.parseSongSources(std::vector<char>(text.begin(), text.end()));
    require(songs.size() == 1 && songs.front().artist_refs.size() == 1 && songs.front().artist_refs.front().source_artist_image.empty(), "song/album artist parser retained the default avatar");
    require(songs.front().cover_url == absolute, "singer avatar filtering changed unrelated cover handling");
}

// 手动推进批次，避免离线测试依赖线程调度。
class Executor final : public c::application::ports::TaskExecutor {
public:
    std::vector<std::function<void()>> tasks; // 待执行的批次 worker
    // 由测试显式触发任务。
    void submit(std::function<void()> task) override
    {
        tasks.push_back(std::move(task));
    }
    // 当前 worker 内执行完所有目标。
    void runAll()
    {
        for (auto &task : tasks) task();
        tasks.clear();
    }
};

// 重试计数有上限，取消和超时后不能继续调用外部处理器。
void verifyBoundedBatchRetries()
{
    model::CrawlBatchItemRecord item;
    item.item_key = "singer-profile:7";
    for (int mode = 0; mode < 5; ++mode) {
        auto executor = std::make_shared<Executor>();
        int calls = 0;
        use::BatchTaskManager manager(executor, nullptr, nullptr, [] { return 1; }, [] { return std::chrono::seconds(30); });
        std::string batch;
        batch = manager.start({item}, 1, "singerProfileRefresh", "{}", "{}", {}, [&](std::string, int) {
            ++calls;
            if (mode == 4) manager.cancel(batch);
            if (mode == 0 && calls == 2) return use::BatchTaskManager::TargetResult {true, 1, {}, {}, "ok"};
            return use::BatchTaskManager::TargetResult {false, 0, "fixture request failed", "network", "network_failed", mode != 2, std::chrono::milliseconds(0)}; }, false, mode == 3 ? 0 : 1);
        executor->runAll();
        const auto progress = manager.progress(batch);
        require(progress && progress->processed == 1, "retry counted one target multiple times");
        require(calls == (mode <= 1 ? 2 : 1), "retry ignored its limit, permanent error, disabled setting or cancellation");
        require(mode != 0 || (progress->success == 1 && progress->failed == 0), "recovered retry was counted as a failed target");
        require(mode != 4 || progress->state_str == "Cancelled", "cancelled retry did not preserve batch cancellation");
    }
    auto executor = std::make_shared<Executor>();
    int calls = 0;
    use::BatchTaskManager manager(executor, nullptr, nullptr, [] { return 1; }, [] { return std::chrono::milliseconds(1); });
    const auto batch = manager.start({item}, 1, "singerProfileRefresh", "{}", "{}", {}, [&](std::string, int) {
        ++calls;
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
        return use::BatchTaskManager::TargetResult {false, 0, "fixture request failed", "network", "network_failed", true, std::chrono::milliseconds(0)}; }, false, 1);
    executor->runAll();
    require(calls <= 1 && manager.progress(batch)->state_str == "Failed", "timed-out batch continued retrying");
}

// 复用实际 parser 与事务编排，仅替换网络和存储端口。
void verifyRefresh()
{
    auto provider = std::make_shared<Provider>();
    auto parser = std::make_shared<c::infrastructure::music::migu::parser::MiguParser>();
    auto dispatch = std::make_shared<use::ParserDispatch>(parser, parser, parser, parser, parser, parser, nullptr, nullptr);
    auto writer = std::make_shared<Writer>();
    auto persistence = std::make_shared<use::MetadataPersistenceService>(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, use::MetadataIngestionDependencies {writer, nullptr});
    use::SingerCatalogWorkflow workflow(provider, dispatch, persistence, nullptr, nullptr);
    provider->mock_singer_info_response = response();
    auto profile = workflow.fetchAndSaveSingerSourceProfile(7);
    require(profile.isSuccess() && profile.data()->source_song_total == 0 && profile.data()->source_album_total == 12, "valid zero and album total were not saved");
    require(writer->batches.size() == 1 && writer->batches.front().profiles.size() == 1 && writer->batches.front().singers.size() == 1, "profile fields were not saved in one transaction");
    const auto &singer = writer->batches.front().singers.front();
    require(singer.gender == c::domain::Gender::Unknown && singer.source_country.empty() && singer.source_name_pinyin.empty(), "profile parser inferred unknown classification or pinyin");
    require(!singer.avatar_url.empty() && !singer.cover_resource, "profile avatar was downloaded inline");

    writer->batches.clear();
    provider->mock_singer_info_response = response("7", 503);
    profile = workflow.fetchAndSaveSingerSourceProfile(7);
    require(!profile.isSuccess() && writer->batches.empty(), "HTTP error response was persisted as a successful profile");
    require(profile.error() == c::application::result::ServiceErrorType::TransientExternalApiError, "temporary HTTP failure was not retryable");
    provider->mock_singer_info_response = response("7", 401);
    require(workflow.fetchAndSaveSingerSourceProfile(7).error() == c::application::result::ServiceErrorType::ExternalApiError, "permanent HTTP client error became retryable");
    provider->mock_singer_info_response = response("7", 429);
    require(workflow.fetchAndSaveSingerSourceProfile(7).error() == c::application::result::ServiceErrorType::ExternalRateLimited, "rate limit was not distinguished");
    provider->mock_singer_info_response = response("8");
    profile = workflow.fetchAndSaveSingerSourceProfile(7);
    require(!profile.isSuccess() && writer->batches.empty(), "another singer's profile was persisted");
    require(profile.error() == c::application::result::ServiceErrorType::ExternalApiError, "identity mismatch became retryable");
    auto invalid = response();
    auto rejected = nlohmann::json::parse(invalid.body.begin(), invalid.body.end());
    rejected["code"] = "999999";
    const auto text = rejected.dump();
    invalid.body.assign(text.begin(), text.end());
    provider->mock_singer_info_response = invalid;
    profile = workflow.fetchAndSaveSingerSourceProfile(7);
    require(!profile.isSuccess() && writer->batches.empty(), "business error response was persisted");
    require(profile.error() == c::application::result::ServiceErrorType::ExternalApiError, "explicit business rejection became retryable");
    provider->mock_singer_info_response = response();
    writer->fail = true;
    profile = workflow.fetchAndSaveSingerSourceProfile(7);
    require(!profile.isSuccess(), "persistence failure was reported as success");
    writer->fail = false;
    profile = workflow.fetchAndSaveSingerSourceProfile(7);
    require(profile.isSuccess() && profile.data()->source_song_total == 0 && profile.data()->source_album_total == 12, "profile could not be retried after failure");
}

// 可选核对已保存的只读实采响应，不在测试内请求平台或写库。
void inspectSavedProfiles(const std::filesystem::path &folder)
{
    std::ifstream manifest(folder / "probe-summary.json");
    nlohmann::json summary;
    manifest >> summary;
    c::infrastructure::music::migu::parser::MiguParser parser;
    int inspected = 0;
    for (const auto &expected : summary.at("results")) {
        const auto id = expected.at("source_singer_id").get<std::string>();
        std::ifstream file(folder / "responses" / (id + ".json"), std::ios::binary);
        const std::vector<char> bytes {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
        const auto profile = parser.parseSingerSourceProfile(bytes);
        require(profile.source_singer_id == id && profile.metadata.has_value(), "saved live profile identity was not parsed");
        require(profile.source_song_total == expected.at("source_song_total").get<int>() && profile.source_album_total == expected.at("source_album_total").get<int>(), "C++ parser disagrees with saved live declared totals");
        require(profile.metadata->gender == c::domain::Gender::Unknown && profile.metadata->source_country.empty(), "saved live profile inferred classification");
        ++inspected;
    }
    std::cout << "CPP_PROFILE_PROBE inspected=" << inspected << " network_requests=0 database_writes=0\n";
}

} // namespace

// 默认运行离线回归，显式参数仅用于复核本机保存的原始响应。
int main(int argc, char **argv)
{
    try {
        if (argc == 3 && std::string(argv[1]) == "--inspect-profiles") {
            inspectSavedProfiles(argv[2]);
            return 0;
        }
        verifyRefresh();
        verifyDefaultAvatars();
        verifyBoundedBatchRetries();
        std::cout << "SingerProfileRefreshTest passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
