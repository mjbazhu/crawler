#include "crawler/api/routing/ResourceTransferRoutes.h"
#include "crawler/api/routing/ResourceParallelLegacyRouteSink.h"

#include <nlohmann/json.hpp>

#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>

namespace c = orchestrator::modules::crawler;
namespace model = c::application::model;
namespace ports = c::application::ports;
namespace result = c::application::result;
using Json = nlohmann::json;
template <typename T>
using Result = result::ServiceResult<T>;
namespace {
// 固定断言文本，不打印请求或私有配置。
void require(bool value, const char *message)
{
    if (!value) throw std::runtime_error(message);
}
// 仅内存路由，按实际 HTTP 语义构造请求。
struct Routes final : c::api::routing::RouteSink {
    std::map<std::string, c::api::routing::RequestHandler> handlers;
    void route(const std::string &method, const std::string &path, c::api::routing::RequestHandler handler) override
    {
        handlers[method + " " + path] = std::move(handler);
    }
    c::api::http::HttpResponse call(std::string method, std::string path, Json body = Json::object(), std::unordered_map<std::string, std::string> query = {})
    {
        auto key = method + " " + path;
        if (!handlers.contains(key) && method == "GET" && path.starts_with("/api/v2/migu/tasks/")) key = path.ends_with("/producers") ? "GET /api/v2/migu/tasks/{1}/producers" : "GET /api/v2/migu/tasks/{1}";
        return handlers.at(key)({method, path, std::move(query), {}, body.dump()});
    }
};
// 只统计受理/取消，不执行任何传输。
struct Jobs final : ports::ResourceTransferJobRepository {
    int enqueues = 0, cancels = 0;
    bool write_admission_required = false;
    model::ResourceTransferOperation stored;
    std::vector<model::ResourceDestination> cancelled;
    Result<model::ResourceTransferOperation> enqueue(model::ResourceTransferRequest request, std::vector<model::ResourceTransferTargetProfile> profiles) override
    {
        if (request.idempotency_key == "queue-full") return Result<model::ResourceTransferOperation>::failure(result::ServiceErrorType::ExternalApiError, "resource_queue_full");
        write_admission_required = request.require_write_admission;
        if (profiles.empty()) {
            if (request.idempotency_key != "fixture" || stored.operation_id.empty()) return Result<model::ResourceTransferOperation>::failure(result::ServiceErrorType::ExternalApiError, "resource_destination_disabled");
            if (request.resources != std::vector {stored.tasks.front().resource} || request.destinations != std::vector {model::ResourceDestination::Baidu}) return Result<model::ResourceTransferOperation>::failure(result::ServiceErrorType::Conflict, "fixture");
            auto replay = stored;
            replay.replayed = true;
            return Result<model::ResourceTransferOperation>::success(std::move(replay));
        }
        ++enqueues;
        stored = {};
        stored.operation_id = "rt_fixture";
        stored.logical_resource_count = request.resources.size();
        stored.state = "running";
        model::ResourceTransferJob task;
        task.task_id = 1;
        task.destination = model::ResourceDestination::Baidu;
        task.resource = request.resources.front();
        task.state = "pending";
        task.adapter = "rclone";
        task.source_identity = "private-source";
        task.attempt_path = "private-attempt-path";
        stored.tasks = {task};
        return Result<model::ResourceTransferOperation>::success(stored);
    }
    Result<model::ResourceTransferOperation> find(std::string id) override
    {
        return id == stored.operation_id ? Result<model::ResourceTransferOperation>::success(stored) : Result<model::ResourceTransferOperation>::failure(result::ServiceErrorType::NotFound, "fixture");
    }
    Result<std::optional<model::ResourceTransferJob>> claim(model::ResourceDestination, std::string, std::string) override
    {
        throw std::logic_error("API must not claim");
    }
    Result<bool> shouldStop(const model::ResourceTransferJob &) override
    {
        throw std::logic_error("API must not transfer");
    }
    Result<bool> beginAttempt(const model::ResourceTransferJob &, model::ResourceTransferAttempt) override
    {
        throw std::logic_error("API must not transfer");
    }
    Result<bool> complete(const model::ResourceTransferJob &, model::ResourceTransferReceipt) override
    {
        throw std::logic_error("API must not transfer");
    }
    Result<bool> fail(const model::ResourceTransferJob &, model::ResourceTransferJobFailure) override
    {
        throw std::logic_error("API must not transfer");
    }
    Result<bool> cancel(std::string, std::vector<model::ResourceDestination> selected) override
    {
        ++cancels;
        cancelled = std::move(selected);
        stored.state = "cancelling";
        return Result<bool>::success(true);
    }
    Result<int> recover(model::ResourceDestination, int) override
    {
        throw std::logic_error("API must not recover");
    }
};
struct Access final : ports::ResourceTransferAccess {
    bool allowed = true;
    Result<ports::ResourceTransferAdmission> inspect(model::ResourceIdentity, model::ResourceDestination) override
    {
        throw std::logic_error("API must not inspect each content item");
    }
    Result<bool> writesAllowed() override
    {
        return Result<bool>::success(allowed);
    }
};
struct Producers final : ports::ResourceProducerTaskRepository {
    int retries = 0;
    Result<std::optional<model::ResourceProducerTask>> claim(model::ResourceDestination, std::string, std::string, std::string, int, bool) override
    {
        throw std::logic_error("API must not claim producer");
    }
    Result<bool> shouldStop(const model::ResourceProducerTask &) override
    {
        throw std::logic_error("API must not execute producer");
    }
    Result<std::string> complete(const model::ResourceProducerTask &, const model::LyricArtifactBundle &, std::uint64_t) override
    {
        throw std::logic_error("API must not produce content");
    }
    Result<bool> fail(const model::ResourceProducerTask &, model::ResourceTransferJobFailure) override
    {
        throw std::logic_error("API must not execute producer");
    }
    Result<int> recover(model::ResourceDestination, int) override
    {
        throw std::logic_error("API must not recover producer");
    }
    Result<bool> retry(std::uint64_t id) override
    {
        ++retries;
        return Result<bool>::success(id == 1);
    }
    Result<std::vector<model::ResourceProducerTask>> list(std::string id) override
    {
        model::ResourceProducerTask task;
        task.producer_id = 1;
        task.source_job.operation_id = id;
        task.source_job.task_id = 1;
        task.source_job.source_identity = "private-source";
        task.state = "waiting_input";
        task.result_code = "producer_key_missing";
        return Result<std::vector<model::ResourceProducerTask>>::success({task});
    }
};
// 验证无前缀/长旧编号只读映射，不调用传输或producer。
struct Legacy final : ports::LegacyResourceTransferRepository {
    std::string id = "Client-Batch_" + std::string(100, 'x');
    Result<model::LegacyResourceTransferBatch> start(model::LegacyResourceTransferRequest) override
    {
        throw std::logic_error("API must not start legacy");
    }
    Result<std::optional<model::LegacyResourceTransferBatch>> find(std::string value) override
    {
        model::LegacyResourceTransferBatch batch;
        batch.batch_id = id;
        batch.terminal = true;
        batch.progress.state_str = "Completed";
        batch.operation_ids = {"rt_one", "rt_two"};
        return Result<std::optional<model::LegacyResourceTransferBatch>>::success(value == id ? std::optional {batch} : std::nullopt);
    }
    Result<bool> cancel(std::string) override
    {
        throw std::logic_error("API must not cancel legacy through real stop");
    }
    Result<bool> pollNext() override
    {
        throw std::logic_error("API must not poll");
    }
    Result<model::MetadataCoverPage> listCovers(model::LegacyResourceTransferRequest) override
    {
        throw std::logic_error("API must not list covers");
    }
};
struct Queries final : ports::ResourceTransferQueryRepository {
    bool schema = true;
    bool weak_confirmed = false;
    int reads = 0;
    int status_reads = 0;
    Result<model::ResourceStatusBatch> status(model::ResourceStatusQuery query) override
    {
        ++status_reads;
        model::ResourceStatusBatch value;
        value.requested_targets = query.destinations;
        for (const auto identity : query.resources) {
            model::ResourceStatusView item;
            item.resource = identity;
            item.found = identity.resource_id != 999;
            if (item.found) item.local = {2, {1, 1}, {1, 1}, 3, std::string(64, 'a'), true, true, false};
            for (auto destination : {model::ResourceDestination::Baidu, model::ResourceDestination::Gdrive}) {
                model::ResourceCloudReceiptView cloud;
                cloud.id = 10 + static_cast<std::uint64_t>(destination);
                cloud.path_resolved = true;
                cloud.size_bytes = 3;
                cloud.checksum = std::string(64, 'a');
                cloud.resource = identity;
                cloud.provider = destination;
                cloud.current = item.found;
                cloud.verification = destination == model::ResourceDestination::Baidu ? "hash_matched" : "sha256_verified";
                item.cloud_receipts.push_back(cloud);
            }
            value.items.push_back(item);
        }
        return Result<model::ResourceStatusBatch>::success(value);
    }
    Result<bool> compatible() override
    {
        return Result<bool>::success(schema);
    }
    Result<model::ResourceCloudReceiptPage> receipts(model::ResourceCloudReceiptQuery query) override
    {
        ++reads;
        model::ResourceCloudReceiptView view;
        view.id = 8;
        view.resource = {query.owner, 3};
        view.provider = model::ResourceDestination::Baidu;
        view.verification = "operator_attested";
        view.assumed_stored = view.transfer_skipped = true;
        if (weak_confirmed) {
            view.verification = "hash_matched";
            view.current = true;
            view.assumed_stored = view.transfer_skipped = false;
        }
        return Result<model::ResourceCloudReceiptPage>::success({{view}, {}});
    }
};
struct Runtime final : ports::ResourceTransferRuntimeControl {
    bool enabled = true;
    model::ResourceTransferTargetProfile target;
    Runtime()
    {
        target.destination = model::ResourceDestination::Baidu;
        target.adapter = "rclone";
        target.fingerprint = "private-fingerprint";
    }
    bool parallelEnabled() const override
    {
        return enabled;
    }
    std::vector<model::ResourceTransferTargetProfile> profiles() const override
    {
        return {target};
    }
    Result<bool> updatePolicy(model::ResourceDestination destination, model::ResourceTransferJobPolicy policy) override
    {
        if (destination != target.destination) return Result<bool>::failure(result::ServiceErrorType::InvalidParam, "fixture");
        target.policy = policy;
        return Result<bool>::success(true);
    }
};
} // namespace

int main()
{
    try {
        auto jobs = std::make_shared<Jobs>();
        auto access = std::make_shared<Access>();
        auto queries = std::make_shared<Queries>();
        auto runtime = std::make_shared<Runtime>();
        auto producers = std::make_shared<Producers>();
        auto service = std::make_shared<c::application::use_cases::ResourceTransferService>(jobs, access, queries, runtime, producers);
        Routes routes;
        c::api::routing::ResourceTransferRoutes(service).registerRoutes(routes);
        auto legacy_repository = std::make_shared<Legacy>();
        service->setLegacyRepository(legacy_repository, false);
        const auto mapping = routes.call("GET", "/api/v2/migu/tasks/" + legacy_repository->id);
        require(mapping.status_code == 200 && Json::parse(mapping.body)["operation_ids"].size() == 2 && Json::parse(mapping.body)["batch_id"] == legacy_repository->id, "custom legacy id did not resolve through v2 read");
        require(routes.call("GET", "/api/v2/migu/tasks/unknown_custom").status_code == 404, "missing legacy mapping changed not-found status");
        access->allowed = false;
        runtime->enabled = false;
        runtime->target.policy.strict_sha256 = true;
        Json status_request = {{"resources", {{{"owner_type", "song"}, {"resource_id", 3}}, {{"owner_type", "album"}, {"resource_id", 999}}}}};
        const auto status_response = routes.call("POST", "/api/v2/migu/resources/status", status_request);
        const auto status = Json::parse(status_response.body);
        require(status_response.status_code == 200 && status["requested_targets"].size() == 3 && status["items"][0]["cloud_receipts"]["baidu"]["current"] == false && status["items"][0]["all_requested_targets_current"] == false && status["items"][1]["found"] == false && jobs->enqueues == 0, "status read ignored strict policy or entered writer in maintenance");
        status_request["destinations"] = {"local"};
        require(Json::parse(routes.call("POST", "/api/v2/migu/resources/status", status_request).body)["items"][0]["all_requested_targets_current"] == true, "status aggregation did not honor explicit subset");
        auto invalid_status = status_request;
        invalid_status["resources"].push_back(invalid_status["resources"][0]);
        const auto before_status_reads = queries->status_reads;
        require(routes.call("POST", "/api/v2/migu/resources/status", invalid_status).status_code == 400 && queries->status_reads == before_status_reads, "duplicate status identity reached repository");
        invalid_status = status_request;
        invalid_status["source_url"] = "https://example.test/private";
        require(routes.call("POST", "/api/v2/migu/resources/status", invalid_status).status_code == 400, "status accepted arbitrary source");
        queries->schema = false;
        require(routes.call("POST", "/api/v2/migu/resources/status", status_request).status_code == 503, "status ignored incompatible schema");
        queries->schema = true;
        runtime->target.policy.strict_sha256 = false;
        status_request.erase("destinations");
        require(Json::parse(routes.call("POST", "/api/v2/migu/resources/status", status_request).body)["items"][0]["all_requested_targets_current"] == true, "complete three-target facts did not aggregate");
        queries->schema = true;
        access->allowed = true;
        runtime->enabled = true;
        runtime->target.policy.strict_sha256 = false;
        const Json request = {{"resources", Json::array({{{"owner_type", "song"}, {"resource_id", 3}}})}, {"destinations", {"baidu"}}, {"idempotency_key", "fixture"}};
        auto full_request = request;
        full_request["idempotency_key"] = "queue-full";
        const auto full_response = routes.call("POST", "/api/v2/migu/resources/process", full_request);
        require(full_response.status_code == 503 && Json::parse(full_response.body)["error_code"] == "resource_queue_full" && full_response.headers.at("Retry-After") == "2" && full_response.headers.at("Access-Control-Expose-Headers") == "Retry-After", "full queue lost retryable API contract");
        const auto accepted = routes.call("POST", "/api/v2/migu/resources/process", request);
        require(accepted.status_code == 202 && Json::parse(accepted.body)["state"] == "running" && jobs->enqueues == 1 && jobs->write_admission_required, "operation was not durably accepted with atomic admission");
        require(accepted.body.find("private-") == std::string::npos, "internal identity/path leaked");
        require(Json::parse(accepted.body)["parent_batch_id"].is_null() && Json::parse(accepted.body)["child_batch_ids"].is_array(), "operation relationship projection missing");
        const auto plans = routes.call("GET", "/api/v2/migu/tasks/rt_fixture/producers");
        require(plans.status_code == 200 && Json::parse(plans.body)["items"][0]["state"] == "waiting_input" && plans.body.find("private-") == std::string::npos, "producer plan missing or leaked source identity");
        require(routes.call("POST", "/api/v2/migu/producers/retry", {{"producer_id", 1}}).status_code == 202 && producers->retries == 1 && jobs->enqueues == 1, "producer retry duplicated parent transfer");
        require(routes.call("POST", "/api/v2/migu/producers/retry", {{"producer_id", 2}}).status_code == 409, "rejected producer retry accepted");
        access->allowed = false;
        runtime->enabled = false;
        require(routes.call("GET", "/api/v2/migu/tasks/rt_fixture/producers").status_code == 200, "maintenance legacy mode blocked producer history");
        require(routes.call("POST", "/api/v2/migu/producers/retry", {{"producer_id", 1}}).status_code == 503 && producers->retries == 2, "unavailable producer retry reached repository");
        access->allowed = true;
        runtime->enabled = true;
        require(routes.call("POST", "/api/v2/migu/covers/process", {{"resources", Json::array({{{"owner_type", "singer"}, {"resource_id", 1}}})}, {"action", "produce_artifacts"}}).status_code == 400, "cover accepted MRC-only produce action");
        auto bad = request;
        bad["destinations"] = {"baidu", "baidu"};
        require(routes.call("POST", "/api/v2/migu/resources/process", bad).status_code == 400 && jobs->enqueues == 1, "duplicate target accepted");
        bad = request;
        bad["resources"][0]["resource_id"] = 1.25;
        require(routes.call("POST", "/api/v2/migu/resources/process", bad).status_code == 400, "fractional resource ID accepted");
        require(routes.call("POST", "/api/v2/migu/covers/process", request).status_code == 400, "cover endpoint accepted song owner");
        bad = request;
        bad["path"] = "untrusted-file";
        require(routes.call("POST", "/api/v2/migu/resources/process", bad).status_code == 400, "arbitrary field silently accepted");
        bad = request;
        bad["parent_task_id"] = 1;
        require(routes.call("POST", "/api/v2/migu/resources/process", bad).status_code == 400, "HTTP request forged internal producer relation");
        access->allowed = false;
        bad = request;
        bad["idempotency_key"] = "new-during-maintenance";
        require(routes.call("POST", "/api/v2/migu/resources/process", bad).status_code == 503 && jobs->enqueues == 1, "maintenance accepted new task");
        const auto replay = routes.call("POST", "/api/v2/migu/resources/process", request);
        require(replay.status_code == 200 && Json::parse(replay.body)["replayed"] == true && jobs->enqueues == 1, "maintenance blocked frozen idempotent replay");
        bad = request;
        bad["destinations"] = {"gdrive"};
        require(routes.call("POST", "/api/v2/migu/resources/process", bad).status_code == 409 && jobs->enqueues == 1, "maintenance hid idempotency conflict");
        require(routes.call("POST", "/api/v2/migu/tasks/stop", {{"batch_id", "rt_fixture"}, {"destinations", {"baidu"}}}).status_code == 200 && jobs->cancels == 1 && jobs->cancelled == std::vector {model::ResourceDestination::Baidu}, "maintenance blocked precise cancellation");
        require(routes.call("POST", "/api/v2/migu/tasks/stop", {{"batch_id", "rt_fixture"}, {"destinations", {"gdrive"}}}).status_code == 400 && jobs->cancels == 1, "foreign target cancellation accepted");
        runtime->enabled = false;
        require(routes.call("GET", "/api/v2/migu/tasks/rt_fixture").status_code == 200, "legacy mode blocked task history");
        require(routes.call("POST", "/api/v2/migu/resources/process", request).status_code == 503, "legacy mode accepted v2 write");
        const auto receipt = routes.call("GET", "/api/v2/migu/cloud/receipts", {}, {{"owner_type", "song"}, {"provider", "baidu"}});
        const auto fact = Json::parse(receipt.body)["items"][0];
        require(receipt.status_code == 200 && fact["assumed_stored"] == true && fact["current"] == false && fact["matches_local"].is_null() && fact["uploaded_at"].is_null(), "attestation was promoted to verified current content");
        require(routes.call("GET", "/api/v2/migu/cloud/receipts", {}, {{"owner_type", "song"}, {"limit", "18446744073709551616"}}).status_code == 400 && queries->reads == 1, "overflow page limit reached repository");
        require(routes.call("GET", "/api/v2/migu/settings").body.find("private-") == std::string::npos, "settings leaked target identity");
        runtime->enabled = true;
        runtime->target.policy.strict_sha256 = true;
        queries->weak_confirmed = true;
        const auto strict_receipt = Json::parse(routes.call("GET", "/api/v2/migu/cloud/receipts", {}, {{"owner_type", "song"}}).body)["items"][0];
        require(strict_receipt["current"] == false && strict_receipt["verification"] == "hash_matched", "strict current view promoted weak persisted evidence");
        const Json policy = {{"timeout_ms", 1000}, {"retries", 0}, {"max_bytes", 1048576}, {"bytes_per_second", 0}};
        require(routes.call("PATCH", "/api/v2/migu/settings", {{"destination", "baidu"}, {"policy", policy}}).status_code == 200 && runtime->target.policy.timeout_ms == 1000 && runtime->target.policy.strict_sha256 && jobs->stored.tasks.front().policy.timeout_ms == 120000, "policy update changed accepted task or downgraded verification");
        queries->schema = false;
        require(routes.call("GET", "/api/v2/migu/tasks/rt_fixture").status_code == 503, "incompatible schema was read");
        require(routes.call("GET", "/api/v2/migu/settings").status_code == 503, "incompatible schema exposed v2 settings");
        Routes legacy;
        c::api::routing::ResourceParallelLegacyRouteSink guard(legacy);
        int old_writes = 0;
        guard.route("POST", "/api/migu/resources/process", [&](auto) { ++old_writes; return c::api::http::HttpResponse::json(200, "{}"); });
        require(legacy.call("POST", "/api/migu/resources/process").status_code == 503 && old_writes == 0, "parallel mode ran legacy writer");
        std::cout << "resource transfer API: admission, precise stop, historical reads, bounded input, evidence levels and configuration isolation passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "resource transfer API test failed: " << error.what() << '\n';
        return 1;
    }
}
