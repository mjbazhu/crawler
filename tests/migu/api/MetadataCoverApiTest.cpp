#include "crawler/api/controllers/CrawlerController.h"
#include "crawler/api/dto/MetadataCoverDto.h"
#include "crawler/api/routing/CrawlerRoutes.h"
#include "crawler/application/use_cases/MetadataCoverCloudWorkflow.h"
#include "migu/support/MetadataCoverTaskSupport.h"
#include "migu/support/MetadataCoverTestSupport.h"
#include "migu/support/MetadataCoverCloudTestSupport.h"

#include <iostream>
#include <chrono>

namespace crawler = orchestrator::modules::crawler;
namespace test = crawler::tests::cover;
namespace model = test::model;
namespace api = crawler::api;
namespace use = crawler::application::use_cases;
using json = nlohmann::json;

// 实际注册表派发请求，验证路由和控制器装配。
class Routes final : public api::routing::RouteSink {
public:
    std::map<std::string, api::routing::RequestHandler> handlers; // 已注册处理器
    // 保留生产注册的函数。
    void route(const std::string &method, const std::string &path, api::routing::RequestHandler handler) override
    {
        handlers[method + ' ' + path] = std::move(handler);
    }
    // 按注册模板调用实际请求路径。
    api::http::HttpResponse call(const std::string &pattern, api::http::HttpRequest request)
    {
        return handlers.at(request.method + ' ' + pattern)(std::move(request));
    }
};

// 验证生产路由、目录封面和后台生命周期，使用真实文件及受控替身。
int main()
{
    try {
        test::TempRoot root;
        auto files = std::make_shared<test::files::FileStorage>(root.path);
        auto http = std::make_shared<test::Http>();
        auto store = std::make_shared<test::files::MetadataCoverStore>(http, files, std::vector<std::string> {});
        auto queue = std::make_shared<test::Queue>();
        for (auto owner : {model::CoverOwner::Singer, model::CoverOwner::Album}) queue->rows[{owner, 1}] = test::target(owner);
        auto workflow = std::make_shared<use::MetadataCoverDownloadWorkflow>(queue, store, store);
        auto executor = std::make_shared<test::Executor>();
        auto batches = std::make_shared<test::Batches>();
        auto service = std::make_shared<use::MetadataCoverService>(queue, store, workflow, executor, nullptr, batches);
        auto controller = std::make_shared<api::controllers::CrawlerController>(nullptr, api::dto::CrawlerSettingsReadonlySnapshot {}, nullptr, service);
        Routes routes;
        api::routing::CrawlerRoutes(controller).registerRoutes(routes);
        api::http::HttpRequest request;
        request.method = "GET";
        request.path = "/api/migu/covers/queue";
        test::require(routes.call(request.path, request).status_code == 400, "missing owner type was accepted");
        request.query_params = {{"owner_type", "singer"}};
        auto response = routes.call(request.path, request);
        test::require(response.status_code == 200 && json::parse(response.body)["items"][0]["local_url"].is_null(), "pending cover exposed local URL");
        request.method = "POST";
        request.path = "/api/migu/covers/process";
        request.query_params.clear();
        request.body = R"({"owner_type":"singer","async":false,"resource_ids":[1]})";
        response = routes.call(request.path, request);
        test::require(response.status_code == 200 && json::parse(response.body)["success_count"] == 1, "synchronous cover route failed");
        const auto receipt = json::parse(response.body)["items"][0];
        test::require(receipt["available"] == true && !receipt.contains("remote_url"), "cover receipt leaked remote URL or lost local availability");
        request.method = "GET";
        request.path = receipt["local_url"].get<std::string>();
        request.body.clear();
        response = routes.call("/api/migu/covers/{1}/{2}/content", request);
        const auto png = test::png();
        test::require(response.status_code == 200 && response.body == std::string(png.begin(), png.end()) && response.headers.at("Content-Type") == "image/png", "content route did not return exact image bytes");
        request.headers["if-none-match"] = response.headers.at("ETag");
        test::require(routes.call("/api/migu/covers/{1}/{2}/content", request).status_code == 304, "ETag condition was ignored");
        request.path = "/api/migu/covers/album/1/content";
        test::require(routes.call("/api/migu/covers/{1}/{2}/content", request).status_code == 404, "same ID of another owner served wrong image");
        request.path = "/api/migu/covers/singer/../content";
        test::require(routes.call("/api/migu/covers/{1}/{2}/content", request).status_code == 400, "non-numeric resource path was accepted");

        json catalog = {{"items", json::array({{{"source_ref_id", 11}, {"avatar_url", "https://example.test/source.png"}}})}};
        test::require(api::dto::attachMetadataCovers(catalog, model::CoverOwner::Singer, service) && catalog["items"][0]["cover"]["available"] == true && catalog["items"][0]["avatar_url"] == "https://example.test/source.png", "catalog local cover enrichment changed platform URL or missed local receipt");
        request.method = "POST";
        request.path = "/api/migu/covers/process";
        request.headers.clear();
        request.body = R"({"owner_type":"album","concurrency":1,"resource_ids":[1]})";
        response = routes.call(request.path, request);
        test::require(response.status_code == 202 && executor->pending.size() == 1, "background request did not use bounded executor");
        const auto batch = json::parse(response.body)["batch_id"].get<std::string>();
        test::require(queue->rows.at({model::CoverOwner::Album, 1}).file.status == 0, "queued worker claimed before it started");
        api::http::HttpRequest stop;
        stop.body = json {{"batch_id", batch}}.dump();
        test::require(controller->handleStopMiguBatch(stop).status_code == 200, "cover batch could not be cancelled through existing control");
        const int calls = http->calls;
        executor->drain();
        test::require(http->calls == calls && queue->rows.at({model::CoverOwner::Album, 1}).file.status == 0, "cancelled worker continued claiming or downloading");
        test::require(batches->items.back().state == "Cancelled", "cancelled batch left its pending item unfinished");
        response = routes.call(request.path, request);
        const auto retry_batch = json::parse(response.body)["batch_id"].get<std::string>();
        executor->drain();
        test::require(service->progress(retry_batch)->state_str == "Completed" && batches->records.at(retry_batch).task_type == "metadataCovers" && batches->items.back().item_type == "albumCover", "cover background progress or persisted owner type was wrong");
        for (const auto &event : batches->events) test::require(json::parse(event.payload).is_object(), "batch event payload was not an object");
        request.body = R"({"owner_type":"singer","url":"https://example.test/arbitrary"})";
        test::require(routes.call(request.path, request).status_code == 400, "arbitrary URL field was accepted");
        request.body = R"({"owner_type":"singer","refresh":true})";
        test::require(routes.call(request.path, request).status_code == 400, "unbounded refresh was accepted");
        request.body = R"({"owner_type":"singer","limit":18446744073709551615})";
        test::require(routes.call(request.path, request).status_code == 400, "oversized integer bypassed request validation");
        const auto singer_file = (**queue->find(model::CoverOwner::Singer, 1).data()).file;
        files->remove(singer_file.local_path);
        // 云端候选中的旧文件丢失必须计为失败，保留 DB 状态和原回执供显式修复。
        auto cloud_queue = std::make_shared<test::CloudQueue>(queue);
        auto remote = std::make_shared<test::CloudStore>();
        auto cloud = std::make_shared<use::MetadataCoverCloudWorkflow>(cloud_queue, store, remote, 1000);
        auto cloud_service = std::make_shared<use::MetadataCoverService>(queue, store, workflow, nullptr, nullptr, nullptr, std::chrono::seconds(20), cloud);
        api::controllers::CrawlerController cloud_controller(nullptr, {}, nullptr, cloud_service);
        request.body = R"({"owner_type":"singer","async":false})";
        response = cloud_controller.handleProcessMiguCovers(request);
        const auto missing = json::parse(response.body);
        test::require(response.status_code == 200 && missing["failed_count"] == 1 && missing["skipped_count"] == 0 && missing["items"][0]["status"] == 2 && missing["items"][0]["available"] == false && missing["items"][0]["local_path"] == singer_file.local_path && remote->calls == 0, "missing local cover was counted as a skip or lost its saved receipt");
        request.body = R"({"owner_type":"singer","resource_ids":[1],"refresh":true,"async":false})";
        response = routes.call(request.path, request);
        test::require(response.status_code == 200 && json::parse(response.body)["success_count"] == 1, "explicit refresh could not repair missing local file");
        // 服务销毁后手动推进仍排队的 worker，不能再引用已销毁管理器或下载新目标。
        queue->rows[{model::CoverOwner::Singer, 3}] = test::target(model::CoverOwner::Singer, 3);
        auto retiring = std::make_shared<use::MetadataCoverService>(queue, store, workflow, executor, nullptr, batches);
        model::MetadataCoverOptions retiring_options;
        retiring_options.query.resource_ids = {3};
        const auto retiring_batch = retiring->start(retiring_options);
        test::require(retiring_batch.isSuccess(), "retiring service did not create pending batch");
        const int retiring_calls = http->calls;
        retiring.reset();
        executor->drain();
        test::require(http->calls == retiring_calls && queue->rows.at({model::CoverOwner::Singer, 3}).file.status == 0 && batches->records.at(*retiring_batch.data()).state == "Cancelled", "destroyed cover service continued queued work");
        // 在下载回调中销毁服务，在途 worker 仍须安全写失败并结束批次。
        queue->rows[{model::CoverOwner::Singer, 4}] = test::target(model::CoverOwner::Singer, 4);
        auto active_service = std::make_shared<use::MetadataCoverService>(queue, store, workflow, executor, nullptr, batches);
        retiring_options.query.resource_ids = {4};
        const auto active_batch = active_service->start(retiring_options);
        test::require(active_batch.isSuccess(), "active lifetime fixture did not start");
        http->on_get = [&] { active_service.reset(); };
        executor->drain();
        http->on_get = {};
        test::require(!active_service && queue->rows.at({model::CoverOwner::Singer, 4}).file.status == 3 && batches->records.at(*active_batch.data()).state == "Cancelled", "in-flight worker used destroyed service state or committed after shutdown");
        batches->reject_create = true;
        request.body = R"({"owner_type":"album"})";
        test::require(routes.call(request.path, request).status_code == 503 && executor->pending.empty(), "unpersisted batch was accepted or executed");
        std::cout << "PASS: registered cover routes, local bytes/ETag/404, strict input, catalog enrichment and background cancellation/persistence\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
