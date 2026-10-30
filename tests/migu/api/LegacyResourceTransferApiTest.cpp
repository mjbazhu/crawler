#include "crawler/api/controllers/CrawlerController.h"
#include "crawler/api/routing/CrawlerRoutes.h"
#include "crawler/api/routing/ResourceParallelLegacyRouteSink.h"
#include "crawler/application/use_cases/LegacyResourceTransferService.h"

#include <iostream>
#include <map>
#include <nlohmann/json.hpp>

namespace c = orchestrator::modules::crawler;
namespace model = c::application::model;
namespace ports = c::application::ports;
namespace use = c::application::use_cases;
template <class T>
using Result = c::application::result::ServiceResult<T>;
using Json = nlohmann::json;
namespace {
void require(bool value, const char *message)
{
    if (!value) throw std::runtime_error(message);
}
struct Runtime final : ports::ResourceTransferRuntimeControl {
    bool enabled = true;
    std::vector<model::ResourceTransferTargetProfile> targets;
    Runtime()
    {
        for (auto d : {model::ResourceDestination::Local, model::ResourceDestination::Baidu, model::ResourceDestination::Gdrive}) {
            model::ResourceTransferTargetProfile p;
            p.destination = d;
            p.policy.timeout_ms = 500;
            p.policy.retries = 2;
            targets.push_back(p);
        }
    }
    bool parallelEnabled() const override
    {
        return enabled;
    }
    std::vector<model::ResourceTransferTargetProfile> profiles() const override
    {
        return targets;
    }
    Result<bool> updatePolicy(model::ResourceDestination, model::ResourceTransferJobPolicy) override
    {
        return Result<bool>::success(true);
    }
};
struct Access final : ports::ResourceTransferAccess {
    bool allowed = true, local_ready = true;
    Result<bool> writesAllowed() override
    {
        return Result<bool>::success(allowed);
    }
    Result<bool> destinationReady(model::ResourceDestination) override
    {
        return Result<bool>::success(local_ready);
    }
    Result<ports::ResourceTransferAdmission> inspect(model::ResourceIdentity, model::ResourceDestination) override
    {
        throw std::logic_error("legacy must not inspect content");
    }
};
struct Repository final : ports::LegacyResourceTransferRepository {
    model::LegacyResourceTransferRequest request;
    model::LegacyResourceTransferBatch batch;
    bool terminal = true, timed_out = false, stop_runtime = false;
    int starts = 0, cancels = 0;
    std::shared_ptr<Runtime> runtime;
    Result<model::LegacyResourceTransferBatch> start(model::LegacyResourceTransferRequest value) override
    {
        ++starts;
        request = value;
        batch = {};
        batch.batch_id = value.owner == model::ResourceOwner::Song && !value.songs.batch_id.empty() ? value.songs.batch_id : "legacy_rt_fixture";
        batch.operation_ids = {"rt_one", "rt_two"};
        batch.owner = value.owner;
        batch.terminal = terminal;
        batch.timed_out = timed_out;
        batch.progress = {batch.batch_id, terminal ? "Completed" : "Running", 1, terminal ? 1 : 0, terminal ? 1 : 0, 0, "", {}, 0};
        batch.songs.batch_id = batch.batch_id;
        batch.songs.claimed_count = 1;
        batch.songs.succeeded_count = terminal ? 1 : 0;
        model::MetadataCoverItem item;
        item.record.resource_id = 1;
        item.success = terminal;
        batch.covers = {item};
        if (stop_runtime) runtime->enabled = false;
        return Result<model::LegacyResourceTransferBatch>::success(batch);
    }
    Result<std::optional<model::LegacyResourceTransferBatch>> find(std::string id) override
    {
        return Result<std::optional<model::LegacyResourceTransferBatch>>::success(id == batch.batch_id ? std::optional {batch} : std::nullopt);
    }
    Result<bool> cancel(std::string) override
    {
        ++cancels;
        batch.terminal = true;
        batch.progress.state_str = "Cancelled";
        return Result<bool>::success(true);
    }
    Result<bool> pollNext() override
    {
        return Result<bool>::success(false);
    }
    Result<model::MetadataCoverPage> listCovers(model::LegacyResourceTransferRequest) override
    {
        return Result<model::MetadataCoverPage>::success({});
    }
};
struct Routes final : c::api::routing::RouteSink {
    std::map<std::string, c::api::routing::RequestHandler> handlers;
    void route(const std::string &m, const std::string &p, c::api::routing::RequestHandler h) override
    {
        handlers[m + " " + p] = std::move(h);
    }
    c::api::http::HttpResponse call(std::string method, std::string path, Json body = Json::object(), std::unordered_map<std::string, std::string> query = {})
    {
        return handlers.at(method + " " + path)({method, path, std::move(query), {}, body.dump()});
    }
};
} // namespace
int main()
{
    try {
        auto runtime = std::make_shared<Runtime>();
        auto access = std::make_shared<Access>();
        auto repository = std::make_shared<Repository>();
        repository->runtime = runtime;
        auto legacy = std::make_shared<use::LegacyResourceTransferService>(repository, runtime, access, model::ResourceDestination::Baidu, true);
        auto crawler = std::make_shared<use::CrawlerService>(use::CrawlerServiceDependencies {});
        auto controller = std::make_shared<c::api::controllers::CrawlerController>(crawler);
        controller->setLegacyResourceTransfers(legacy);
        Routes routes;
        c::api::routing::ResourceParallelLegacyRouteSink guard(routes, true);
        c::api::routing::CrawlerRoutes(controller).registerRoutes(guard);
        auto response = routes.call("POST", "/api/migu/resources/process", {{"async", false}, {"batch_id", "Client-Batch_1"}, {"options", {{"resource_download_concurrency", 2}, {"resource_download_retries", 0}, {"resource_download_timeout_ms", 10000}, {"auto_refresh_pacmtoken", false}, {"refresh_pacmtoken_on_440013", false}, {"pacmtoken_refresh_interval_seconds", 0}, {"process_related_resources", false}}}});
        require(response.status_code == 200 && Json::parse(response.body)["succeeded_count"] == 1, "legacy synchronous song shape lost");
        require(repository->request.songs.batch_id == "Client-Batch_1" && Json::parse(response.body)["batch_id"] == "Client-Batch_1", "custom legacy batch id lost at API boundary");
        require(repository->request.songs.options.resource_download_concurrency == 2 && !repository->request.songs.options.auto_refresh_pacmtoken && !repository->request.songs.options.process_related_resources, "legacy effective options lost");
        require(repository->request.profiles[0].policy.timeout_ms == 10000 && repository->request.profiles[1].policy.timeout_ms == 500, "song override changed extra cloud policy");
        require(Json::parse(response.headers.at("X-Resource-Operation-Ids")).size() == 2, "multi-operation mapping not exposed");
        repository->terminal = false;
        response = routes.call("POST", "/api/migu/resources/process", {{"async", true}});
        require(response.status_code == 202 && Json::parse(response.body)["accepted"] == true, "legacy async shape lost");
        response = routes.call("GET", "/api/migu/tasks/progress", Json::object(), {{"batch_id", "legacy_rt_fixture"}});
        require(response.status_code == 200, "legacy persisted progress unavailable");
        repository->stop_runtime = true;
        response = routes.call("POST", "/api/migu/covers/process", {{"owner_type", "singer"}, {"resource_ids", {1}}, {"async", false}});
        require(response.status_code == 503 && Json::parse(response.body)["error_code"] == "legacy_completion_pending" && Json::parse(response.body).contains("message") && Json::parse(response.body).contains("error") && Json::parse(response.body)["batch_id"] == "legacy_rt_fixture", "pending cover misreported synchronous completion");
        runtime->enabled = true;
        repository->stop_runtime = false;
        repository->terminal = true;
        repository->timed_out = true;
        response = routes.call("POST", "/api/migu/resources/process", {{"async", false}});
        require(response.status_code == 503 && Json::parse(response.body)["error_code"] == "legacy_batch_timeout", "timeout misreported synchronous success");
        const auto calls = repository->starts;
        access->allowed = false;
        require(routes.call("POST", "/api/migu/resources/process").status_code == 503 && repository->starts == calls, "maintenance accepted old write");
        response = routes.call("POST", "/api/migu/resources/process");
        require(Json::parse(response.body)["error_code"] == "resource_maintenance" && Json::parse(response.body).contains("message") && Json::parse(response.body).contains("error"), "legacy maintenance error envelope diverged");
        response = routes.call("POST", "/api/migu/covers/process", {{"owner_type", "singer"}});
        require(response.status_code == 503 && Json::parse(response.body)["error_code"] == "resource_maintenance", "cover maintenance error envelope diverged");
        access->allowed = true;
        access->local_ready = false;
        require(routes.call("POST", "/api/migu/resources/process").status_code == 503 && repository->starts == calls, "paused local admitted old processing");
        access->local_ready = true;
        runtime->targets.erase(runtime->targets.begin());
        require(!legacy->songs({}, false).isSuccess(), "missing required local silently downgraded");
        require(routes.call("POST", "/api/migu/covers/recover-stale", {{"owner_type", "singer"}}).status_code == 503, "old writer recovery escaped guard");
        std::cout << "legacy API: original response shapes, frozen options, actual operation IDs, durable progress, timeout and write guards passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "legacy API test failed: " << error.what() << '\n';
        return 1;
    }
}
