#include "crawler/api/routing/ResourceWriteRouteSink.h"

#include <exception>
#include <stdexcept>
#include <utility>

namespace orchestrator::modules::crawler::api::routing {
// 仅明确的只读 POST 和协作停止绕过闸门；未来新写路由默认受保护。
bool requiresResourceWriteAdmission(std::string_view method, std::string_view path)
{
    if (method == "GET" || method == "HEAD" || method == "OPTIONS") return false;
    if (method != "POST") return true;
    return path != "/api/v2/migu/resources/status" && path != "/api/migu/tasks/stop" && path != "/api/migu/resources/preview" && path != "/api/migu/singer-songs/crawl-from-db/preview" && path != "/api/migu/singer-albums/crawl-from-db/preview" && path != "/api/migu/backfill/{1}/preview" && path != "/api/migu/backfill/{type}/preview";
}

namespace {
// 错误不回显数据库异常，CORS 与普通路由保持一致。
http::HttpResponse unavailable()
{
    auto response = http::HttpResponse::json(503, R"({"error_code":"resource_maintenance","message":"resource writes are paused or baseline is not ready","error":"resource writes are paused or baseline is not ready"})");
    response.headers["Access-Control-Allow-Origin"] = "*";
    response.headers["Access-Control-Expose-Headers"] = "ETag, Retry-After";
    response.headers["Retry-After"] = "30";
    return response;
}
} // namespace

ResourceWriteRouteSink::ResourceWriteRouteSink(RouteSink &sink, std::shared_ptr<application::ports::ResourceTransferAccess> access) : sink_(sink), access_(std::move(access))
{
    if (!access_) throw std::invalid_argument("resource write routes require admission");
}

void ResourceWriteRouteSink::route(const std::string &method, const std::string &path, RequestHandler handler)
{
    if (!requiresResourceWriteAdmission(method, path)) {
        sink_.route(method, path, std::move(handler));
        return;
    }
    const bool requires_local = path == "/api/migu/resources/process" || path == "/api/migu/covers/process" || path == "/api/migu/resources/recover-stale" || path == "/api/migu/covers/recover-stale" || path == "/api/migu/maintenance/recover-stale";
    sink_.route(method, path, [access = access_, handler = std::move(handler), requires_local](http::HttpRequest request) {
        try {
            const auto allowed = access->writesAllowed();
            if (!allowed.isSuccess() || !*allowed.data()) return unavailable();
            if (requires_local) {
                const auto local = access->destinationReady(application::model::ResourceDestination::Local);
                if (!local.isSuccess() || !*local.data()) return unavailable();
            }
        } catch (const std::exception &) {
            return unavailable();
        }
        return handler(std::move(request));
    });
}
} // namespace orchestrator::modules::crawler::api::routing
