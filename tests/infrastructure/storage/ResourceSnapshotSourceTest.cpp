#include "crawler/infrastructure/storage/filesystem/ResourceSnapshotSource.h"
#include "crawler/infrastructure/storage/filesystem/ResourceFileDigest.h"
#include "crawler/application/use_cases/ResourceTransferBudget.h"
#include "migu/support/MetadataCoverTestSupport.h"

#include <atomic>
#include <future>
#include <iostream>

namespace c = orchestrator::modules::crawler;
namespace test = c::tests::cover;
namespace model = c::application::model;
namespace result = c::application::result;
namespace files = c::infrastructure::storage::filesystem;

namespace {
// 只返回本机受控 fixture 描述，不发请求或进行原生认领。
class Descriptions final : public c::application::ports::ResourceTransferSources {
public:
    model::ResourceTransferSourceInput input; // 当前单项描述
    int calls = 0;                            // 实际描述读取次数
    result::ServiceResult<model::ResourceTransferSourceInput> describe(const model::ResourceTransferJob &) override
    {
        ++calls;
        return result::ServiceResult<model::ResourceTransferSourceInput>::success(input);
    }
};
// 失败只输出固定消息，避免打印路径和正文。
template <typename T>
T get(result::ServiceResult<T> value)
{
    if (!value.isSuccess()) throw std::runtime_error(value.message());
    return *value.data();
}
// 每项测试使用独立、足够但有界的执行时限。
model::ResourceTransferContext context()
{
    return {std::chrono::steady_clock::now() + std::chrono::seconds(5), {}};
}
} // namespace

// 本机文件及替身 HTTP 验证来源租约、空间配额、图片边界、旧摘要和派生等待。
int main()
{
    try {
        test::TempRoot root;
        auto descriptions = std::make_shared<Descriptions>();
        auto http = std::make_shared<test::Http>();
        descriptions->input = test::target(model::CoverOwner::Singer);
        files::ResourceSnapshotDependencies dependencies;
        dependencies.descriptions = descriptions;
        dependencies.covers = http;
        dependencies.artifacts = std::make_shared<files::FileStorage>(root.path / "artifacts", false);
        const std::array<std::filesystem::path, 3> roots {root.path / "local-temp", root.path / "baidu-temp", root.path / "google-temp"};
        files::ResourceSnapshotSource source(dependencies, roots, 2 * 1024 * 1024);
        test::require(!std::filesystem::exists(roots[0]) && !std::filesystem::exists(roots[1]) && !std::filesystem::exists(root.path / "artifacts"), "source constructor opened storage");
        model::ResourceTransferJob job;
        job.resource = {model::ResourceOwner::Singer, 1};
        job.destination = model::ResourceDestination::Baidu;
        job.policy.max_bytes = 1024 * 1024;
        auto first = get(source.acquire(job, context())), second = get(source.acquire(job, context()));
        const auto first_path = first->filePath(), second_path = second->filePath();
        test::require(first_path != second_path && first->checksum() == second->checksum() && first->sizeBytes() == test::png().size(), "snapshots were not independent validated images");
        const auto exhausted = source.acquire(job, context());
        test::require(!exhausted.isSuccess() && exhausted.message() == "snapshot_quota_exhausted" && http->calls == 2, "space quota requested content or overcommitted");
        first.reset();
        test::require(!std::filesystem::exists(first_path) && std::filesystem::exists(second_path), "lease release deleted sibling snapshot");
        auto third = get(source.acquire(job, context()));
        second.reset();
        third.reset();
        test::require(std::filesystem::is_empty(roots[1] / ".resource-snapshots"), "snapshot lease left private files");
        test::require(!std::filesystem::exists(roots[0]), "cloud acquisition touched local temporary root");
        const int before_cancel = http->calls, described = descriptions->calls;
        auto stopped = context();
        stopped.cancelled = [] { return true; };
        test::require(!source.acquire(job, stopped).isSuccess() && http->calls == before_cancel && descriptions->calls == described, "cancelled acquisition reached source");
        job.policy.max_bytes = 8;
        test::require(!source.acquire(job, context()).isSuccess() && std::filesystem::is_empty(roots[1] / ".resource-snapshots"), "small frozen image budget was ignored");
        job.policy.max_bytes = 1024 * 1024;
        descriptions->input = test::target(model::CoverOwner::Album);
        job.resource.owner = model::ResourceOwner::Album;
        job.destination = model::ResourceDestination::Gdrive;
        auto album = get(source.acquire(job, context()));
        test::require(album->relativePath().starts_with("migu/albums/"), "album identity became singer");
        album.reset();

        model::ResourceArtifactInput input;
        input.artifact_type = "word_json";
        input.local_path = "generated/words.json";
        const std::string bytes = R"({"segments":[]})";
        model::SongResourceBlobSaveRequest save;
        save.relative_path_hint = input.local_path;
        save.data.assign(bytes.begin(), bytes.end());
        const auto saved = dependencies.artifacts->saveSongResourceBlob(std::move(save));
        test::require(saved.ok, "artifact file fixture failed");
        const auto digest = get(files::digestResourceFile(dependencies.artifacts->getFullPath(input.local_path), 1024, context()));
        input.size_bytes = bytes.size();
        job.resource.owner = model::ResourceOwner::SongArtifact;
        const int calls = http->calls;
        for (const auto &checksum : {saved.checksum, "rclone:md5:" + digest.md5, "rclone:sha1:" + digest.sha1, "rclone:size:" + std::to_string(bytes.size()), std::string("opaque-legacy")}) {
            input.checksum = checksum;
            descriptions->input = input;
            auto artifact = get(source.acquire(job, context()));
            test::require(artifact->checksum() == saved.checksum && artifact->sizeBytes() == bytes.size() && http->calls == calls, "legacy checksum forced network or manufactured reference");
        }
        input.checksum = std::string(64, 'f');
        descriptions->input = input;
        const auto mismatch = source.acquire(job, context());
        test::require(!mismatch.isSuccess() && mismatch.error() == result::ServiceErrorType::Conflict, "mismatched artifact checksum reused");
        input.local_path = "missing/words.json";
        descriptions->input = input;
        const auto missing = source.acquire(job, context());
        test::require(!missing.isSuccess() && missing.message() == "waiting_input" && http->calls == calls, "missing artifact ran source or producer");
        input.local_path = "../escape";
        descriptions->input = input;
        test::require(!source.acquire(job, context()).isSuccess(), "artifact escaped formal root");

        c::application::use_cases::ResourceTransferBudget budget(1);
        auto held = budget.acquire(model::ResourceDestination::Baidu, context());
        std::atomic<bool> cancel {false};
        auto waiting_context = context();
        waiting_context.cancelled = [&] { return cancel.load(); };
        auto waiting = std::async(std::launch::async, [&] { return budget.acquire(model::ResourceDestination::Gdrive, waiting_context); });
        cancel.store(true);
        test::require(waiting.wait_for(std::chrono::seconds(2)) == std::future_status::ready && !waiting.get(), "waiting permit ignored independent cancellation");
        held.reset();
        test::require(bool(budget.acquire(model::ResourceDestination::Local, context())), "cancelled waiter leaked shared permit");
        std::cout << "resource snapshots: independent image leases, per-target disk quota, frozen limits, image/artifact source forms, legacy checksum verification, waiting input and permit cancellation passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "resource snapshot test failed: " << error.what() << '\n';
        return 1;
    }
}
