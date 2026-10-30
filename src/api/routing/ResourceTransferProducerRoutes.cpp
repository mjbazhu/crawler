#include "crawler/api/routing/ResourceTransferRoutes.h"
#include "ResourceTransferApiSupport.h"

#include <algorithm>

namespace orchestrator::modules::crawler::api::routing {
namespace api = transfer_api;
using Error = application::result::ServiceErrorType;
void ResourceTransferRoutes::registerProducerRoutes(RouteSink &sink) const
{
    const auto add = [&](const std::string &method, const std::string &path, RequestHandler handler) {
        sink.route(method, path, [service = service_, handler = std::move(handler)](http::HttpRequest request) {
            if (!service) return api::error(Error::ExternalApiError, "resource_schema_unavailable");
            try {
                return handler(std::move(request));
            } catch (const nlohmann::json::exception &) {
                return api::error(Error::InvalidParam);
            } catch (const std::invalid_argument &) {
                return api::error(Error::InvalidParam);
            } catch (const std::exception &) {
                return api::error(Error::InternalError);
            }
        });
    };
    add("GET", "/api/v2/migu/tasks/{1}/producers", [service = service_](http::HttpRequest request) {
        constexpr std::string_view prefix = "/api/v2/migu/tasks/", suffix = "/producers";
        const std::string_view path = request.path;
        if (!path.starts_with(prefix) || !path.ends_with(suffix) || path.size() <= prefix.size() + suffix.size() || !request.query_params.empty()) throw std::invalid_argument("invalid producer parent");
        const auto id = std::string(path.substr(prefix.size(), path.size() - prefix.size() - suffix.size()));
        if (id.size() > 64 || !std::all_of(id.begin(), id.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= '0' && c <= '9') || c == '_'; })) throw std::invalid_argument("invalid producer parent");
        const auto found = service->producers(id);
        if (!found.isSuccess()) return api::error(found.error(), found.message());
        auto items = api::Json::array();
        for (const auto &t : *found.data()) items.push_back({{"producer_id", t.producer_id}, {"parent_batch_id", t.source_job.operation_id}, {"parent_task_id", t.source_job.task_id}, {"destination", application::model::resourceDestinationName(t.source_job.destination)}, {"state", t.state}, {"result_code", t.result_code}, {"attempt_count", t.attempt_count}, {"cancel_requested", t.cancel_requested}, {"child_batch_id", t.child_operation_id.empty() ? api::Json(nullptr) : api::Json(t.child_operation_id)}, {"policy", api::policyJson(t.policy)}});
        return api::response(200, {{"batch_id", id}, {"items", items}});
    });
    add("POST", "/api/v2/migu/producers/retry", [service = service_](http::HttpRequest request) {
        const auto body = api::body(request);
        api::keys(body, {"producer_id"});
        const auto id = api::positive(body.at("producer_id"));
        const auto retried = service->retryProducer(id);
        if (!retried.isSuccess()) return api::error(retried.error(), retried.message());
        if (!*retried.data()) return api::error(Error::Conflict, "producer_retry_rejected");
        return api::response(202, {{"producer_id", id}, {"accepted", true}});
    });
    for (const std::string path : {"/api/v2/migu/tasks/{1}/producers", "/api/v2/migu/producers/retry"}) sink.route("OPTIONS", path, [](http::HttpRequest) { return http::HttpResponse {204, {{"Access-Control-Allow-Origin", "*"}, {"Access-Control-Allow-Methods", "GET, POST, OPTIONS"}, {"Access-Control-Allow-Headers", "Content-Type"}}, ""}; });
}
} // namespace orchestrator::modules::crawler::api::routing
