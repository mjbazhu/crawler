#include "crawler/api/routing/ResourceParallelLegacyRouteSink.h"
#include "crawler/api/routing/ResourceWriteRouteSink.h"
#include "migu/support/ResourceTransferAccessTestSupport.h"
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <nlohmann/json.hpp>
namespace c = orchestrator::modules::crawler;
namespace {
// 固定断言，不输出真实请求内容。
void require(bool value, const char *message)
{
    if (!value) throw std::runtime_error(message);
}
// 只注册内存处理器，不启动HTTP或数据库。
struct Routes final : c::api::routing::RouteSink {
    std::map<std::string, c::api::routing::RequestHandler> handlers; // 注册表
    // 保留完整方法/路径，不调用业务。
    void route(const std::string &method, const std::string &path, c::api::routing::RequestHandler handler) override
    {
        handlers[method + " " + path] = std::move(handler);
    }
    // 构造最小请求。
    c::api::http::HttpResponse call(const std::string &method, const std::string &path)
    {
        return handlers.at(method + " " + path)({method, path, {}, {}, "{}"});
    }
};
} // namespace
int main()
{
    try {
        Routes routes;
        int old_calls = 0;
        const auto handler = [&](auto) {++old_calls;return c::api::http::HttpResponse::json(200,"{\"legacy\":true}"); };
        c::api::routing::ResourceParallelLegacyRouteSink parallel(routes, true);
        for (const auto path : {"/api/migu/resources/retry", "/api/migu/resources/reset", "/api/migu/resources/recover-stale", "/api/migu/covers/recover-stale", "/api/migu/maintenance/recover-stale", "/api/migu/backfill/{1}/reset", "/api/migu/search"}) parallel.route("POST", path, handler);
        for (const auto path : {"/api/migu/resources/retry", "/api/migu/resources/reset"}) {
            const auto response = routes.call("POST", path);
            const auto body = nlohmann::json::parse(response.body);
            require(response.status_code == 503 && body.at("error_code") == "legacy_parallel_unsupported" && body.contains("message") && body.contains("error"), "standard rejection changed");
            const auto guide = body.at("migration");
            require(guide.at("supported") == false && guide.at("equivalent") == false && guide.at("alternative").at("path") == "/api/v2/migu/resources/process" && guide.at("alternative").at("starts_transfer") == true && guide.at("alternative").at("preserves_history_skip") == true, "status reset silently became transfer admission");
            require(response.headers.at("Access-Control-Allow-Origin") == "*", "upgrade guidance lost CORS");
        }
        for (const auto path : {"/api/migu/resources/recover-stale", "/api/migu/covers/recover-stale"}) {
            const auto guide = nlohmann::json::parse(routes.call("POST", path).body).at("migration");
            require(guide.at("alternative").at("type") == "maintenance_cli" && guide.at("alternative").at("preserves_uncertain_intents") == true && guide.at("required").size() >= 4, "unsafe global recovery offered");
        }
        for (const auto path : {"/api/migu/maintenance/recover-stale", "/api/migu/backfill/{1}/reset", "/api/migu/search"}) require(nlohmann::json::parse(routes.call("POST", path).body).at("migration").at("alternative").is_null(), "metadata offered nonexistent v2 equivalent");
        require(old_calls == 0, "rejected parallel writer called old handler");
        for (const auto path : {"/api/migu/resources/process", "/api/migu/covers/process", "/api/migu/resources/preview", "/api/migu/tasks/stop", "/api/migu/pacmtoken/refresh"}) {
            parallel.route("POST", path, handler);
            require(routes.call("POST", path).status_code == 200, "existing compatible/readonly/stop path changed");
        }
        parallel.route("GET", "/api/migu/resources", handler);
        require(routes.call("GET", "/api/migu/resources").status_code == 200, "readonly route changed");
        Routes unconfigured;
        c::api::routing::ResourceParallelLegacyRouteSink disabled(unconfigured, false);
        disabled.route("POST", "/api/migu/resources/process", handler);
        require(nlohmann::json::parse(unconfigured.call("POST", "/api/migu/resources/process").body).at("migration").at("reason") == "legacy_projection_not_configured", "unconfigured process lacks migration requirement");
        auto access = std::make_shared<c::tests::TransferAccess>();
        Routes legacy;
        c::api::routing::ResourceWriteRouteSink normal(legacy, access);
        normal.route("POST", "/api/migu/resources/reset", handler);
        require(legacy.call("POST", "/api/migu/resources/reset").body == "{\"legacy\":true}", "legacy success structure changed");
        access->allowed = false;
        const auto before = old_calls;
        require(legacy.call("POST", "/api/migu/resources/reset").status_code == 503 && old_calls == before, "maintenance bypassed legacy admission");
        Routes compatible;
        access->allowed = true;
        c::api::routing::ResourceParallelLegacyRouteSink configured(compatible, true, access);
        for (const auto path : {"/api/migu/resources/retry", "/api/migu/resources/reset", "/api/migu/resources/recover-stale", "/api/migu/covers/recover-stale", "/api/migu/maintenance/recover-stale", "/api/migu/singers/crawl", "/api/migu/singers/profile/refresh-from-db", "/api/migu/singer-songs/crawl-from-db", "/api/migu/singer-albums/crawl-from-db", "/api/migu/tasks/{1}/rerun-failed", "/api/migu/tasks/{1}/clone", "/api/migu/tasks/{1}/backfill-retry", "/api/migu/tasks/{1}/archive", "/api/migu/backfill/{1}/retry", "/api/migu/backfill/{1}/reset"}) {
            configured.route("POST", path, handler);
            require(compatible.call("POST", path).body == "{\"legacy\":true}", "configured governance lost original handler or response");
        }
        for (const auto method : {"PUT", "PATCH"}) {
            configured.route(method, "/api/migu/settings", handler);
            require(compatible.call(method, "/api/migu/settings").status_code == 200, "settings governance remained unavailable");
        }
        configured.route("POST", "/api/migu/new-unknown-writer", handler);
        const auto accepted_calls = old_calls;
        require(compatible.call("POST", "/api/migu/new-unknown-writer").status_code == 503 && old_calls == accepted_calls, "unknown writer bypassed whitelist");
        access->allowed = false;
        for (const auto path : {"/api/migu/resources/reset", "/api/migu/backfill/{1}/retry", "/api/migu/singers/crawl"}) {
            const auto rejected = compatible.call("POST", path);
            require(rejected.status_code == 503 && nlohmann::json::parse(rejected.body).at("error_code") == "resource_maintenance", "parallel governance bypassed durable maintenance");
        }
        require(old_calls == accepted_calls, "maintenance rejection executed original governance");
        access->allowed = true;
        access->ready = false;
        require(compatible.call("POST", "/api/migu/resources/recover-stale").status_code == 503 && old_calls == accepted_calls, "local pause did not protect native recovery");
        require(compatible.call("POST", "/api/migu/backfill/{1}/reset").status_code == 200, "local pause wrongly blocked metadata-only reset");
        access->throws = true;
        require(compatible.call("POST", "/api/migu/resources/reset").status_code == 503, "admission error did not fail closed");
        std::cout << "PASS: configured legacy governance, original handlers, unknown rejection, maintenance/pause and dependency protection\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
