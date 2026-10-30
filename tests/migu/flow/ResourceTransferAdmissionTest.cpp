#include "crawler/api/routing/ResourceWriteRouteSink.h"
#include "crawler/application/use_cases/MetadataCoverCloudWorkflow.h"
#include "crawler/application/use_cases/MetadataCoverDownloadWorkflow.h"
#include "crawler/application/use_cases/MetadataCoverService.h"
#include "migu/support/MetadataCoverCloudTestSupport.h"
#include "migu/support/ResourceTransferAccessTestSupport.h"

#include <iostream>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

namespace c = orchestrator::modules::crawler;
namespace test = c::tests::cover;
namespace model = c::application::model;
namespace use = c::application::use_cases;

namespace {
// 显式计数内容和元信息读取，历史 skip 不应调用其中任何一种。
class Reader final : public c::application::ports::MetadataCoverReader {
public:
    mutable int reads = 0;  // 内容读取次数
    mutable int probes = 0; // 存在性检查次数
    // 假设旧本地文件缺失；跳过仍必须成立。
    bool available(const model::MetadataResourceFile &) const override
    {
        ++probes;
        return false;
    }
    // 不读取任何文件。
    std::optional<model::MetadataCoverContent> read(const model::MetadataResourceFile &) const override
    {
        ++reads;
        return std::nullopt;
    }
};

// 保存实际注册的 handler，在装饰器释放后继续调用。
class Routes final : public c::api::routing::RouteSink {
public:
    std::map<std::pair<std::string, std::string>, c::api::routing::RequestHandler> handlers; // 已注册路由
    // 不改变方法/路径或 handler 的所有权。
    void route(const std::string &method, const std::string &path, c::api::routing::RequestHandler handler) override
    {
        handlers[{method, path}] = std::move(handler);
    }
};
} // namespace

// 验证真实用例在认领、文件、上传前跳过，以及写路由对维护和故障失败关闭。
int main()
{
    try {
        auto access = std::make_shared<c::tests::TransferAccess>();
        test::TempRoot root;
        const auto unopened_root = root.path / "unopened";
        auto files = std::make_shared<test::files::FileStorage>(unopened_root, false);
        test::require(!std::filesystem::exists(unopened_root), "read-only storage construction created its root");
        auto http = std::make_shared<test::Http>();
        auto store = std::make_shared<test::files::MetadataCoverStore>(http, files, std::vector<std::string> {});
        auto reader = std::make_shared<Reader>();
        auto queue = std::make_shared<test::Queue>();
        auto target = test::target(model::CoverOwner::Singer);
        target.file.status = 2;
        target.file.local_path = "missing/old.png";
        queue->rows[{target.owner, target.resource_id}] = target;
        use::MetadataCoverDownloadWorkflow local(queue, store, reader, access);
        const auto skipped = local.process(target, {});
        test::require(skipped.skipped && !skipped.success && skipped.result_code == "skip_existing", "historical declaration became local verification or failure");
        auto remote = std::make_shared<test::CloudStore>();
        auto clouds = std::make_shared<test::CloudQueue>(queue);
        use::MetadataCoverCloudWorkflow unclassified(clouds, reader, remote, 1000, access, std::nullopt);
        const auto unclassified_item = unclassified.process(target, 0, "unclassified");
        test::require(unclassified_item.result_code == "legacy_cloud_provider_required" && reader->reads == 0 && reader->probes == 0 && remote->calls == 0, "unknown rclone platform was guessed or performed content IO");
        test::require(unclassified.process(target, 0, "cancelled", [] { return true; }).result_code == "cancelled", "unknown provider masked selected cancellation");
        for (auto provider : {model::ResourceDestination::Baidu, model::ResourceDestination::Gdrive}) {
            use::MetadataCoverCloudWorkflow cloud(clouds, reader, remote, 1000, access, provider);
            const auto item = cloud.process(target, 0, "fixture");
            test::require(item.skipped && !item.success && item.result_code == "skip_existing", "cloud skip required a completed local file or online cloud");
        }
        auto local_workflow = std::make_shared<use::MetadataCoverDownloadWorkflow>(queue, store, reader, access);
        auto legacy_cloud = std::make_shared<use::MetadataCoverCloudWorkflow>(clouds, reader, remote, 1000, access, model::ResourceDestination::Baidu);
        use::MetadataCoverService legacy_service(queue, reader, local_workflow, nullptr, nullptr, nullptr, std::chrono::seconds(5), legacy_cloud);
        const auto before = access->inspections.load();
        const auto legacy_skipped = legacy_service.process({});
        test::require(legacy_skipped.isSuccess() && legacy_skipped.data()->size() == 1 && legacy_skipped.data()->front().skipped && access->inspections.load() == before + 2, "legacy cover skip did not include its required cloud");
        auto paused_cloud = access->value;
        paused_cloud.skip.enabled = false;
        paused_cloud.blocked_code = "destination_paused";
        access->targets[model::ResourceDestination::Baidu] = paused_cloud;
        const auto legacy_blocked = legacy_service.process({});
        test::require(legacy_blocked.isSuccess() && !legacy_blocked.data()->front().success && !legacy_blocked.data()->front().skipped && legacy_blocked.data()->front().result_code == "destination_paused", "local skip concealed an unavailable required legacy cloud");
        access->targets.clear();
        test::require(reader->reads == 0 && reader->probes == 0 && http->calls == 0 && remote->calls == 0, "skip performed content or availability I/O");
        access->value.version.target_revision = 2;
        test::require(local.process(target, {}).result_code == "resource_maintenance", "skip followed a different target revision");
        access->fault = true;
        test::require(local.process(target, {}).result_code == "resource_admission_failed", "unavailable admission allowed content work");
        access->fault = false;

        Routes routes;
        int executed = 0;
        const auto handler = [&](c::api::http::HttpRequest) { ++executed; return c::api::http::HttpResponse::json(200, "{}"); };
        {
            c::api::routing::ResourceWriteRouteSink guard(routes, access);
            guard.route("POST", "/api/migu/covers/process", handler);
            guard.route("POST", "/api/migu/tasks/stop", handler);
            guard.route("POST", "/api/migu/resources/preview", handler);
            guard.route("GET", "/api/migu/schema/status", handler);
            guard.route("POST", "/api/future/write", handler);
        }
        const auto invoke = [&](const std::string &method, const std::string &path) { return routes.handlers.at({method, path})({}); };
        access->allowed = false;
        const auto denied = invoke("POST", "/api/migu/covers/process");
        test::require(denied.status_code == 503 && executed == 0 && denied.headers.contains("Access-Control-Allow-Origin") && denied.body.find("\"error_code\":\"resource_maintenance\"") != std::string::npos, "closed write route returned success or broke the error envelope");
        test::require(invoke("POST", "/api/migu/tasks/stop").status_code == 200 && invoke("POST", "/api/migu/resources/preview").status_code == 200 && invoke("GET", "/api/migu/schema/status").status_code == 200, "maintenance blocked reads or cancellation");
        test::require(invoke("POST", "/api/future/write").status_code == 503, "new write route bypassed admission by default");
        access->allowed = true;
        access->ready = false;
        test::require(invoke("POST", "/api/migu/covers/process").status_code == 503, "paused local was replaced by an active cloud");
        access->ready = true;
        test::require(invoke("POST", "/api/migu/covers/process").status_code == 200, "live reopened state was cached as closed");
        access->fault = true;
        test::require(invoke("POST", "/api/migu/covers/process").status_code == 503, "database error was interpreted as permission");
        access->fault = false;
        access->throws = true;
        const auto error = invoke("POST", "/api/migu/covers/process");
        test::require(error.status_code == 503 && error.body.find("private") == std::string::npos, "dependency diagnostic escaped into maintenance response");
        std::cout << "PASS: cover skip before I/O, target version isolation, lazy root, live write gate and fail-closed routes\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
