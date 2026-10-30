#include "crawler/api/routing/ResourceTransferRoutes.h"

#include "ResourceTransferApiSupport.h"

#include <algorithm>
#include <exception>
#include <stdexcept>
#include <string_view>
#include <utility>

namespace orchestrator::modules::crawler::api::routing {
namespace api = transfer_api;
namespace model = application::model;
using Error = application::result::ServiceErrorType;
namespace {
// 每个入口统一限制异常边界，错误不得带出请求或数据库内部文本。
RequestHandler guarded(RequestHandler handler)
{
    return [handler = std::move(handler)](http::HttpRequest request) {
        try {
            return handler(std::move(request));
        } catch (const nlohmann::json::exception &) {
            return api::error(Error::InvalidParam);
        } catch (const std::invalid_argument &) {
            return api::error(Error::InvalidParam);
        } catch (const std::exception &) {
            return api::error(Error::InternalError);
        }
    };
}
// 只接受当前接口的 owner 范围，不能通过图片入口误处理歌曲。
model::ResourceTransferRequest processRequest(const http::HttpRequest &request, std::string_view scope)
{
    const auto data = api::body(request);
    api::keys(data, {"resources", "destinations", "action", "idempotency_key"});
    const auto &resources = data.at("resources");
    if (!resources.is_array() || resources.empty() || resources.size() > 200) throw std::invalid_argument("invalid resource list");
    model::ResourceTransferRequest value;
    for (const auto &item : resources) {
        api::keys(item, {"owner_type", "resource_id"});
        const auto owner = model::parseResourceOwner(item.at("owner_type").get<std::string>());
        if (!owner || (scope == "resources" && *owner != model::ResourceOwner::Song) || (scope == "covers" && *owner != model::ResourceOwner::Singer && *owner != model::ResourceOwner::Album) || (scope == "artifacts" && *owner != model::ResourceOwner::SongArtifact)) throw std::invalid_argument("invalid resource owner");
        value.resources.push_back({*owner, api::positive(item.at("resource_id"))});
    }
    if (data.contains("destinations")) value.destinations = api::destinations(data.at("destinations"));
    if (data.contains("action")) {
        if (data.at("action") == "produce_artifacts" && scope != "resources") throw std::invalid_argument("producer action requires MRC resource");
        const auto action = model::parseResourceTransferAction(data.at("action").get<std::string>());
        if (!action) throw std::invalid_argument("invalid action");
        value.action = *action;
    }
    if (data.contains("idempotency_key")) value.idempotency_key = data.at("idempotency_key").get<std::string>();
    return value;
}
// 路径内的操作 ID 只作为业务参数，禁止 URL 或任意路径继续传播。
std::string operationId(std::string value)
{
    if (value.empty() || value.size() > 64 || !std::all_of(value.begin(), value.end(), [](char ch) { return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_'; })) throw std::invalid_argument("invalid operation id");
    return value;
}
// 查询参数不允许伪造表名或绕过有界分页。
model::ResourceCloudReceiptQuery receiptQuery(const http::HttpRequest &request)
{
    api::keys(api::Json(request.query_params), {"owner_type", "resource_id", "provider", "task_state", "verification", "after_id", "limit"});
    const auto owner = request.query_params.find("owner_type");
    if (owner == request.query_params.end()) throw std::invalid_argument("owner required");
    const auto parsed = model::parseResourceOwner(owner->second);
    if (!parsed) throw std::invalid_argument("invalid owner");
    model::ResourceCloudReceiptQuery value;
    value.owner = *parsed;
    for (const auto &[key, text] : request.query_params) {
        if (key == "resource_id")
            value.resource_id = api::number(text);
        else if (key == "provider") {
            value.provider = model::parseCloudProvider(text);
            if (!value.provider) throw std::invalid_argument("invalid cloud provider");
        } else if (key == "task_state")
            value.task_state = text;
        else if (key == "verification")
            value.verification = text;
        else if (key == "after_id")
            value.after_id = api::number(text, true);
        else if (key == "limit") {
            const auto limit = api::number(text);
            if (limit > 200) throw std::invalid_argument("invalid page size");
            value.limit = static_cast<int>(limit);
        }
    }
    return value;
}
} // namespace

ResourceTransferRoutes::ResourceTransferRoutes(std::shared_ptr<application::use_cases::ResourceTransferService> service) : service_(std::move(service))
{}

void ResourceTransferRoutes::registerRoutes(RouteSink &sink) const
{
    registerProducerRoutes(sink);
    registerStatusRoutes(sink);
    const std::string prefix = "/api/v2/migu/";
    const auto add = [&](const std::string &method, const std::string &path, RequestHandler handler) {
        sink.route(method, prefix + path, guarded([service = service_, handler = std::move(handler), method](http::HttpRequest request) {
                       if (!service && method != "OPTIONS") return api::error(Error::ExternalApiError, "resource_schema_unavailable");
                       return handler(std::move(request));
                   }));
    };
    for (const std::string scope : {"resources", "covers", "artifacts"})
        add("POST", scope + "/process", [service = service_, scope](http::HttpRequest request) {
            const auto accepted = service->process(processRequest(request, scope));
            if (!accepted.isSuccess()) return api::error(accepted.error(), accepted.message());
            return api::response(accepted.data()->replayed ? 200 : 202, api::operation(*accepted.data()));
        });
    add("GET", "tasks/{1}", [service = service_, prefix](http::HttpRequest request) {
        const auto base = prefix + "tasks/";
        if (!request.path.starts_with(base)) throw std::invalid_argument("invalid task path");
        const auto id = request.path.substr(base.size());
        if (id.empty() || id.size() > 128 || std::any_of(id.begin(), id.end(), [](unsigned char ch) { return ch < 32 || ch == 127 || ch == '/'; })) throw std::invalid_argument("invalid task id");
        const bool real_id = id.size() <= 64 && std::all_of(id.begin(), id.end(), [](char ch) { return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_'; });
        if (real_id) {
            const auto found = service->find(id);
            if (found.isSuccess()) return api::response(200, api::operation(*found.data()));
            if (found.error() != Error::NotFound) return api::error(found.error(), found.message());
        }
        {
            const auto found = service->legacyBatch(id);
            if (!found.isSuccess()) return api::error(found.error(), found.message());
            const auto &batch = *found.data();
            return api::response(200, {{"batch_id", batch.batch_id}, {"legacy_view_state", batch.progress.state_str}, {"legacy_view_terminal", batch.terminal}, {"legacy_view_timed_out", batch.timed_out}, {"operation_ids", batch.operation_ids}});
        }
    });
    add("POST", "tasks/stop", [service = service_](http::HttpRequest request) {
        const auto data = api::body(request);
        api::keys(data, {"batch_id", "destinations"});
        const auto stopped = service->stop(operationId(data.at("batch_id").get<std::string>()), data.contains("destinations") ? api::destinations(data.at("destinations")) : std::vector<model::ResourceDestination> {});
        return stopped.isSuccess() ? api::response(200, api::operation(*stopped.data())) : api::error(stopped.error(), stopped.message());
    });
    add("GET", "cloud/receipts", [service = service_](http::HttpRequest request) {
        const auto found = service->receipts(receiptQuery(request));
        return found.isSuccess() ? api::response(200, api::receipts(*found.data())) : api::error(found.error(), found.message());
    });
    add("GET", "settings", [service = service_](http::HttpRequest) {
        const auto schema = service->schemaCompatible();
        if (!schema.isSuccess() || !*schema.data()) return api::error(Error::ExternalApiError, "resource_schema_unavailable");
        auto policies = api::Json::array(), targets = api::Json::array();
        for (const auto &profile : service->profiles()) {
            policies.push_back({{"destination", model::resourceDestinationName(profile.destination)}, {"policy", api::policyJson(profile.policy)}});
            targets.push_back({{"destination", model::resourceDestinationName(profile.destination)}, {"adapter", profile.adapter}, {"available", profile.available}, {"verification_policy", profile.policy.strict_sha256 ? "strict_sha256" : "hash_match"}});
        }
        return api::response(200, {{"mutable", {{"targets", policies}}}, {"readonly", {{"pipeline_mode", service->parallelEnabled() ? "parallel" : "legacy"}, {"targets", targets}, {"legacy_write_projection_ready", service->legacyProjectionReady()}}}});
    });
    add("PATCH", "settings", [service = service_](http::HttpRequest request) {
        const auto data = api::body(request);
        api::keys(data, {"destination", "policy"});
        const auto destination = model::parseResourceDestination(data.at("destination").get<std::string>());
        if (!destination) throw std::invalid_argument("invalid target");
        const auto policy = api::policy(data.at("policy"));
        const auto updated = service->updatePolicy(*destination, policy);
        return updated.isSuccess() ? api::response(200, {{"destination", model::resourceDestinationName(*destination)}, {"policy", api::policyJson(policy)}, {"applies_to", "new_operations"}}) : api::error(updated.error(), updated.message());
    });
    for (const std::string path : {"resources/process", "covers/process", "artifacts/process", "tasks/{1}", "tasks/stop", "cloud/receipts", "settings"})
        add("OPTIONS", path, [](http::HttpRequest) {
            return http::HttpResponse {204, {{"Access-Control-Allow-Origin", "*"}, {"Access-Control-Allow-Methods", "GET, POST, PATCH, OPTIONS"}, {"Access-Control-Allow-Headers", "Content-Type"}}, ""};
        });
}

} // namespace orchestrator::modules::crawler::api::routing
