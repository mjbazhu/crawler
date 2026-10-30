#include "crawler/api/routing/ResourceTransferRoutes.h"
#include "ResourceTransferApiSupport.h"

#include <stdexcept>
#include <string>
#include <utility>

namespace orchestrator::modules::crawler::api::routing {
namespace api = transfer_api;
void ResourceTransferRoutes::registerStatusRoutes(RouteSink &sink) const
{
    sink.route("POST", "/api/v2/migu/resources/status", [service = service_](http::HttpRequest request) {
        using Error = application::result::ServiceErrorType;
        if (!service) return api::error(Error::ExternalApiError, "resource_schema_unavailable");
        try {
            const auto data = api::body(request);
            api::keys(data, {"resources", "destinations"});
            const auto &resources = data.at("resources");
            if (!resources.is_array() || resources.empty() || resources.size() > 200) throw std::invalid_argument("invalid resource status batch");
            application::model::ResourceStatusQuery query;
            for (const auto &item : resources) {
                api::keys(item, {"owner_type", "resource_id"});
                const auto owner = application::model::parseResourceOwner(item.at("owner_type").get<std::string>());
                if (!owner) throw std::invalid_argument("invalid resource owner");
                query.resources.push_back({*owner, api::positive(item.at("resource_id"))});
            }
            if (data.contains("destinations")) query.destinations = api::destinations(data.at("destinations"));
            const auto found = service->status(std::move(query));
            if (!found.isSuccess()) return api::error(found.error(), found.message());
            auto items = api::Json::array();
            for (const auto &item : found.data()->items) {
                const auto &local = item.local;
                api::Json clouds = {{"baidu", nullptr}, {"gdrive", nullptr}};
                const auto cloud_page = api::receipts({item.cloud_receipts, {}});
                for (const auto &cloud : cloud_page.at("items")) clouds[cloud.at("provider").get<std::string>()] = cloud;
                items.push_back({{"owner_type", application::model::resourceOwnerName(item.resource.owner)}, {"resource_id", item.resource.resource_id}, {"found", item.found}, {"local", {{"status", local.status}, {"content_generation", local.confirmed.content_generation}, {"target_revision", local.confirmed.target_revision}, {"current_content_generation", local.current_version.content_generation}, {"current_target_revision", local.current_version.target_revision}, {"size_bytes", local.size_bytes}, {"checksum", local.checksum}, {"path_resolved", local.path_resolved}, {"current", local.current}, {"transfer_skipped", local.transfer_skipped}}}, {"cloud_receipts", clouds}, {"all_requested_targets_current", item.all_requested_targets_current}});
            }
            auto targets = api::Json::array();
            for (auto destination : found.data()->requested_targets) targets.push_back(application::model::resourceDestinationName(destination));
            return api::response(200, {{"requested_targets", targets}, {"items", items}});
        } catch (const nlohmann::json::exception &) {
            return api::error(Error::InvalidParam);
        } catch (const std::invalid_argument &) {
            return api::error(Error::InvalidParam);
        } catch (const std::exception &) {
            return api::error(Error::InternalError);
        }
    });
    sink.route("OPTIONS", "/api/v2/migu/resources/status", [](http::HttpRequest) { return http::HttpResponse {204, {{"Access-Control-Allow-Origin", "*"}, {"Access-Control-Allow-Methods", "POST, OPTIONS"}, {"Access-Control-Allow-Headers", "Content-Type"}}, ""}; });
}
} // namespace orchestrator::modules::crawler::api::routing
