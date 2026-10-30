#include "crawler/api/routing/ResourceParallelLegacyRouteSink.h"

#include "crawler/api/routing/ResourceWriteRouteSink.h"
#include "crawler/api/http/ResourceLegacyMigrationGuide.h"

#include <utility>
#include <array>

namespace orchestrator::modules::crawler::api::routing {

bool supportsParallelLegacyGovernance(std::string_view method, std::string_view path)
{
    if ((method == "PUT" || method == "PATCH") && path == "/api/migu/settings") return true;
    if (method != "POST") return false;
    constexpr std::array paths {
        "/api/migu/singers/crawl",
        "/api/migu/singers/profile/refresh-from-db",
        "/api/migu/singer-songs/crawl-from-db",
        "/api/migu/singer-albums/crawl-from-db",
        "/api/migu/tasks/{1}/rerun-failed",
        "/api/migu/tasks/{1}/clone",
        "/api/migu/tasks/{1}/backfill-retry",
        "/api/migu/tasks/{1}/archive",
        "/api/migu/maintenance/recover-stale",
        "/api/migu/resources/retry",
        "/api/migu/resources/reset",
        "/api/migu/resources/recover-stale",
        "/api/migu/covers/recover-stale",
        "/api/migu/backfill/{1}/preview",
        "/api/migu/backfill/{1}/retry",
        "/api/migu/backfill/{1}/reset"
    };
    for (const auto candidate : paths)
        if (path == candidate) return true;
    return false;
}

ResourceParallelLegacyRouteSink::ResourceParallelLegacyRouteSink(RouteSink &sink, bool compatibility, std::shared_ptr<application::ports::ResourceTransferAccess> access) : sink_(sink), compatibility_(compatibility), access_(std::move(access))
{}

void ResourceParallelLegacyRouteSink::route(const std::string &method, const std::string &path, RequestHandler handler)
{
    if (!requiresResourceWriteAdmission(method, path) || path.starts_with("/api/migu/pacmtoken")) {
        sink_.route(method, path, std::move(handler));
        return;
    }
    const bool process = compatibility_ && method == "POST" && (path == "/api/migu/resources/process" || path == "/api/migu/covers/process");
    if (process || (access_ && supportsParallelLegacyGovernance(method, path))) {
        if (access_)
            ResourceWriteRouteSink(sink_, access_).route(method, path, std::move(handler));
        else
            sink_.route(method, path, std::move(handler));
        return;
    }
    sink_.route(method, path, [path](http::HttpRequest) { return http::resourceLegacyMigrationResponse(path); });
}

} // namespace orchestrator::modules::crawler::api::routing
