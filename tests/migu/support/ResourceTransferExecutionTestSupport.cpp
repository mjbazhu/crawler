#include "migu/support/ResourceTransferExecutionTestSupport.h"

#include "crawler/application/use_cases/ResourceTransferWorker.h"
#include "crawler/infrastructure/app/ResourceTransferFactory.h"
#include "crawler/infrastructure/music/migu/parser/MiguSongResourceParser.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceTransferJobs.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceTransferSources.h"
#include "crawler/infrastructure/storage/filesystem/ResourceSnapshotSource.h"
#include "crawler/infrastructure/storage/filesystem/ResourceFileDigest.h"
#include "migu/support/MetadataCoverTestSupport.h"
#include "infrastructure/storage/support/BaiduTransferTestSupport.h"
#include "infrastructure/storage/support/RcloneTransferTestSupport.h"

#include <atomic>
#include <chrono>
#include <fstream>
#include <future>
#include <thread>

namespace orchestrator::modules::crawler::tests::transfer_execution {
namespace model = application::model;
namespace ports = application::ports;
namespace use = application::use_cases;
namespace result = application::result;
namespace mysql = infrastructure::music::persistence::mysql;
namespace files = infrastructure::storage::filesystem;
namespace test = tests::cover;

// 所有平台响应都来自本机替身，计数证明三个目标各自请求新 URL。
class Provider final : public ports::SongResourceProvider {
public:
    std::atomic<int> calls {0}; // 实际 listen 次数
    std::optional<Response> fetchSongResource(model::SongResourceFetchRequest) override
    {
        const auto sequence = calls.fetch_add(1);
        const auto text = std::string(R"({"code":"000000","data":{"url":"https://example.test/audio.flac","mrcUrl":"https://example.test/source-)") + std::to_string(sequence) + R"(.mrc"}})";
        Response response;
        response.status_code = 200;
        response.body.assign(text.begin(), text.end());
        return response;
    }
};

// 不实现完整响应聚合；超过 8 MiB 的内容只以 32 KiB 分片交付。
class Content final : public ports::SongResourceContentFetcher {
public:
    std::atomic<int> calls {0};                                // 实际内容请求数
    static constexpr std::uint64_t size = 9 * 1024 * 1024 + 7; // 大于旧云端 bytes 上限
    model::SongResourceContentFetchResult fetchSongResourceContent(model::SongResourceContentFetchRequest) override
    {
        throw std::runtime_error("whole-body fallback used for source");
    }
    model::SongResourceContentStreamResult streamSongResourceContent(model::SongResourceContentFetchRequest request, model::SongResourceContentStreamCallbacks callbacks) override
    {
        test::require(request.url.find("/source-") != std::string::npos, "fresh source URL missing");
        ++calls;
        model::SongResourceContentStreamInfo info {200, "application/octet-stream"};
        test::require(!callbacks.on_response || callbacks.on_response(info), "source response rejected");
        const std::string chunk(32 * 1024, 'x');
        std::uint64_t total = 0;
        while (total < size) {
            const auto count = static_cast<std::size_t>((std::min)(size - total, std::uint64_t(chunk.size())));
            if (callbacks.on_chunk && !callbacks.on_chunk(std::string_view(chunk.data(), count))) {
                model::SongResourceContentStreamResult value;
                value.result_code = "cancelled";
                return value;
            }
            total += count;
        }
        model::SongResourceContentStreamResult value;
        value.ok = true;
        value.status_code = 200;
        value.content_type = info.content_type;
        value.bytes_received = total;
        return value;
    }
};

// 可观察屏障，模拟百度已进入上传，但不发送真实网盘请求。
struct Barrier {
    std::promise<void> entered;          // 发布已经开始
    std::atomic<bool> released {false};  // 允许结束
    std::atomic<bool> signalled {false}; // 只发一次信号
};

// 在真实隔离库中登记同一个三路操作，实际写本地文件，云端使用可控替身。
void verify(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, model::ResourceIdentity identity, std::vector<model::ResourceTransferTargetProfile> profiles)
{
    test::TempRoot root;
    auto jobs = std::make_shared<mysql::MySqlResourceTransferJobs>(pool);
    auto provider = std::make_shared<Provider>();
    auto content = std::make_shared<Content>();
    files::ResourceSnapshotDependencies dependencies;
    dependencies.descriptions = std::make_shared<mysql::MySqlResourceTransferSources>(pool);
    dependencies.songs = provider;
    dependencies.content = content;
    dependencies.parser = std::make_shared<infrastructure::music::migu::parser::MiguSongResourceParser>();
    auto source = std::make_shared<files::ResourceSnapshotSource>(std::move(dependencies), std::array<std::filesystem::path, 3> {root.path / "local-temp", root.path / "baidu-temp", root.path / "google-temp"});
    infrastructure::app::ServerApplication::Config config;
    config.storage_path = (root.path / "formal").string();
    config.baidupcs.executable_path = "pcs-fixture";
    config.baidupcs.cloud_root = "/Archive";
    config.rclone.rc_url = "http://127.0.0.1:5572";
    config.rclone.target_fs = "archive:Media";
    auto rc = std::make_shared<tests::cloud_transfer::RcClient>();
    tests::cloud_transfer::writeFile(root.path / "expected.bin", Content::size, 'x');
    const auto expected = files::digestResourceFile(root.path / "expected.bin", Content::size, {std::chrono::steady_clock::now() + std::chrono::seconds(10), {}});
    test::require(expected.isSuccess(), "expected fixture digest missing");
    rc->expected = *expected.data();
    rc->expected_byte = 'x';
    auto native = std::make_shared<tests::cloud_transfer::BaiduRunner>(root.path / "remote-fixture");
    native->publish_before_barrier = true;
    auto assembly = infrastructure::app::ResourceTransferFactory(config).build(true, {{model::ResourceDestination::Baidu, root.path / "baidu-readback", 8ULL * 1024 * 1024 * 1024, "baidupcs"}, {model::ResourceDestination::Gdrive, {}, 8ULL * 1024 * 1024 * 1024, "rclone"}}, rc, [native](const auto &line, auto timeout, const auto &cancelled) { return (*native)(line, timeout, cancelled); });
    test::require(assembly.local_transfer && !std::filesystem::exists(root.path / "formal"), "factory created formal root before actual local work");
    auto source_budget = std::make_shared<use::ResourceTransferBudget>(3), transfer_budget = std::make_shared<use::ResourceTransferBudget>(3);
    auto barrier = std::make_shared<Barrier>();
    auto entered = barrier->entered.get_future();
    use::ResourceTransferWorker local(jobs, source, assembly.local_transfer, profiles[0], source_budget, transfer_budget);
    native->on_upload = [barrier](const auto &stopped) {
        if (!barrier->signalled.exchange(true)) barrier->entered.set_value();
        while (!barrier->released.load() && !stopped()) std::this_thread::sleep_for(std::chrono::milliseconds(10));
    };
    use::ResourceTransferWorker baidu(jobs, source, assembly.destination(model::ResourceDestination::Baidu), profiles[1], source_budget, transfer_budget);
    use::ResourceTransferWorker google(jobs, source, assembly.destination(model::ResourceDestination::Gdrive), profiles[2], source_budget, transfer_budget);
    model::ResourceTransferRequest request;
    request.resources = {identity};
    request.idempotency_key = "real-three-lane-execution";
    const auto accepted = jobs->enqueue(request, profiles);
    test::require(accepted.isSuccess() && accepted.data()->tasks.size() == 3, "execution registration failed");
    auto slow = std::async(std::launch::async, [&] { return baidu.runOne(); });
    auto first = std::async(std::launch::async, [&] { return local.runOne(); });
    auto second = std::async(std::launch::async, [&] { return google.runOne(); });
    // 先释放屏障再析构 future，保证失败路径也能退出本机线程。
    struct Release {
        std::shared_ptr<Barrier> barrier;
        ~Release()
        {
            barrier->released.store(true);
        }
    } release {barrier};
    test::require(entered.wait_for(std::chrono::seconds(15)) == std::future_status::ready, "Baidu did not enter isolated transfer");
    test::require(first.wait_for(std::chrono::seconds(15)) == std::future_status::ready && second.wait_for(std::chrono::seconds(15)) == std::future_status::ready, "slow Baidu held local or Google");
    test::require(first.get().isSuccess() && second.get().isSuccess(), "independent fast branch failed");
    const auto state = jobs->find(accepted.data()->operation_id);
    test::require(state.isSuccess(), "execution progress missing");
    int succeeded = 0, running = 0;
    std::string local_path;
    for (const auto &item : state.data()->tasks) {
        succeeded += item.state == "succeeded";
        running += item.state == "running";
        if (item.destination == model::ResourceDestination::Local) local_path = item.attempt_path;
    }
    test::require(succeeded == 2 && running == 1 && provider->calls == 3 && content->calls == 3, "three branches did not acquire independent source snapshots");
    test::require(assembly.local->exists(local_path) && std::filesystem::file_size(assembly.local->getFullPath(local_path)) == Content::size, "local streaming did not commit complete file");
    test::require(jobs->cancel(accepted.data()->operation_id, {model::ResourceDestination::Baidu}).isSuccess(), "selected cancellation failed");
    test::require(slow.wait_for(std::chrono::seconds(5)) == std::future_status::ready && slow.get().isSuccess(), "selected cancel did not stop stalled branch");
    const auto finished = jobs->find(accepted.data()->operation_id);
    test::require(finished.isSuccess() && finished.data()->state == "partial", "mixed cancellation incorrectly reported completed");
    test::require(assembly.local->exists(local_path), "cloud cancellation removed local file");
    test::require(native->uploads == 1 && rc->uploads == 1, "concrete cloud adapters did not publish independent files");
    request.destinations = {model::ResourceDestination::Baidu};
    request.action = model::ResourceTransferAction::Repair;
    request.idempotency_key = "explicit-cloud-repair-after-cancel";
    const auto repaired = jobs->enqueue(request, profiles);
    test::require(repaired.isSuccess() && baidu.runOne().isSuccess(), "concrete Baidu inspection did not reconcile late object");
    const auto repaired_state = jobs->find(repaired.data()->operation_id);
    test::require(repaired_state.isSuccess() && repaired_state.data()->tasks.front().state == "succeeded" && provider->calls == 3 && content->calls == 3 && native->uploads == 1, "repair redownloaded or uploaded instead of inspecting precise object");
    test::require(std::filesystem::is_empty(root.path / "local-temp" / ".resource-snapshots") && std::filesystem::is_empty(root.path / "baidu-temp" / ".resource-snapshots") && std::filesystem::is_empty(root.path / "google-temp" / ".resource-snapshots"), "one branch leaked or retained another branch snapshot");
}
} // namespace orchestrator::modules::crawler::tests::transfer_execution
