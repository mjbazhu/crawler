#include "crawler/api/controllers/ResourceLegacyMigrationOpenApi.h"
#include "crawler/api/routing/ResourceWriteRouteSink.h"
#include "crawler/api/routing/ResourceParallelLegacyRouteSink.h"
#include <string>
#include <utility>
namespace orchestrator::modules::crawler::api::controllers {
void appendResourceLegacyMigrationOpenApi(nlohmann::json &document)
{
    using Json = nlohmann::json;
    document["components"]["schemas"]["LegacyWriterMigrationGuidance"] = Json::parse(R"guide({"type":"object","additionalProperties":false,"required":["supported","equivalent","original_semantics","reason","alternative","required"],"properties":{"supported":{"type":"boolean","enum":[false]},"equivalent":{"type":"boolean","enum":[false]},"original_semantics":{"type":"string"},"reason":{"type":"string"},"alternative":{"type":"object","nullable":true,"additionalProperties":false,"required":["type","starts_transfer"],"properties":{"type":{"type":"string","enum":["http","maintenance_cli"]},"method":{"type":"string"},"path":{"type":"string"},"action":{"type":"string"},"command":{"type":"string"},"starts_transfer":{"type":"boolean"},"preserves_history_skip":{"type":"boolean"},"may_schedule_retry":{"type":"boolean"},"preserves_uncertain_intents":{"type":"boolean"}}},"required":{"type":"array","items":{"type":"string"}}}})guide");
    document["components"]["schemas"]["ErrorResponse"]["properties"]["migration"] = {{"$ref", "#/components/schemas/LegacyWriterMigrationGuidance"}};
    for (auto &[path, methods] : document["paths"].items()) {
        if (!path.starts_with("/api/migu/") || path.starts_with("/api/migu/pacmtoken")) continue;
        for (const auto &[lower, upper] : {std::pair {"post", "POST"}, std::pair {"put", "PUT"}, std::pair {"patch", "PATCH"}, std::pair {"delete", "DELETE"}}) {
            if (!methods.contains(lower) || !routing::requiresResourceWriteAdmission(upper, path)) continue;
            auto route_path = path;
            for (const auto parameter : {"{batch_id}", "{type}"}) {
                const auto offset = route_path.find(parameter);
                if (offset != std::string::npos) route_path.replace(offset, std::string(parameter).size(), "{1}");
            }
            if (routing::supportsParallelLegacyGovernance(upper, route_path)) {
                auto &operation = methods[lower];
                operation["description"] = operation.value("description", std::string {}) + " Parallel mode retains this legacy operation and response under durable maintenance admission. Resource retry/reset remain native-state updates without transfer admission; current history skips, baseline exclusions, occupied unified slots and uncertain cloud intents are protected. Stale recovery handles only free legacy native claims; unified tasks retain their destination-scoped maintenance recovery. Metadata writes register resources and advance source versions without starting resource transfers.";
                continue;
            }
            auto &response = methods[lower]["responses"]["503"];
            if (!response.contains("$ref")) response["description"] = response.value("description", std::string {}) + " Unsupported or unconfigured legacy routes in parallel mode return legacy_parallel_unsupported with optional migration guidance. Configured metadata and governance retain their original semantics under durable admission; unknown writers are not executed. A v2 retry explicitly starts transfer and is not a native state reset.";
        }
    }
}
} // namespace orchestrator::modules::crawler::api::controllers
