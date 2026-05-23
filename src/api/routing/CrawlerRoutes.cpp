#include "crawler/api/routing/CrawlerRoutes.h"

#include <utility>

namespace orchestrator::modules::crawler::api::routing {

namespace {

api::http::HttpResponse withCors(api::http::HttpResponse response)
{
    response.headers["Access-Control-Allow-Origin"] = "*";
    return response;
}

api::http::HttpResponse handleCorsOptions(api::http::HttpRequest)
{
    return api::http::HttpResponse {
        204,
        {{"Access-Control-Allow-Origin", "*"},
         {"Access-Control-Allow-Methods", "GET, POST, PUT, PATCH, OPTIONS"},
         {"Access-Control-Allow-Headers", "Content-Type"}},
        ""
    };
}

api::http::HttpResponse handleHealth(api::http::HttpRequest)
{
    return api::http::HttpResponse {
        200,
        {{"Content-Type", "application/json"}, {"Access-Control-Allow-Origin", "*"}},
        R"({"status":"ok"})"
    };
}

} // namespace

CrawlerRoutes::CrawlerRoutes(std::shared_ptr<api::controllers::CrawlerController> controller) : controller_(std::move(controller))
{}

void CrawlerRoutes::registerRoutes(RouteSink &routes) const
{
    routes.route("OPTIONS", "/api/migu/singers/crawl", handleCorsOptions);
    routes.route("POST", "/api/migu/singers/crawl", [controller = controller_](api::http::HttpRequest req) {
        return withCors(controller->handleStartMiguSingerIndexCrawl(std::move(req)));
    });

    routes.route("OPTIONS", "/api/migu/singer-songs/crawl", handleCorsOptions);
    routes.route("POST", "/api/migu/singer-songs/crawl", [controller = controller_](api::http::HttpRequest req) {
        return withCors(controller->handleStartMiguSingerSongsCrawl(std::move(req)));
    });

    routes.route("OPTIONS", "/api/migu/singer-songs/crawl-from-db", handleCorsOptions);
    routes.route("POST", "/api/migu/singer-songs/crawl-from-db", [controller = controller_](api::http::HttpRequest req) {
        return withCors(controller->handleStartMiguSingerSongsCrawlFromDb(std::move(req)));
    });
    routes.route("OPTIONS", "/api/migu/singer-songs/crawl-from-db/preview", handleCorsOptions);
    routes.route("POST", "/api/migu/singer-songs/crawl-from-db/preview", [controller = controller_](api::http::HttpRequest req) {
        return withCors(controller->handlePreviewMiguSingerSongsCrawlFromDb(std::move(req)));
    });

    routes.route("OPTIONS", "/api/migu/singer-albums/crawl", handleCorsOptions);
    routes.route("POST", "/api/migu/singer-albums/crawl", [controller = controller_](api::http::HttpRequest req) {
        return withCors(controller->handleStartMiguSingerAlbumsCrawl(std::move(req)));
    });

    routes.route("OPTIONS", "/api/migu/singer-albums/crawl-from-db", handleCorsOptions);
    routes.route("POST", "/api/migu/singer-albums/crawl-from-db", [controller = controller_](api::http::HttpRequest req) {
        return withCors(controller->handleStartMiguSingerAlbumsCrawlFromDb(std::move(req)));
    });
    routes.route("OPTIONS", "/api/migu/singer-albums/crawl-from-db/preview", handleCorsOptions);
    routes.route("POST", "/api/migu/singer-albums/crawl-from-db/preview", [controller = controller_](api::http::HttpRequest req) {
        return withCors(controller->handlePreviewMiguSingerAlbumsCrawlFromDb(std::move(req)));
    });

    routes.route("OPTIONS", "/api/migu/album-details/crawl", handleCorsOptions);
    routes.route("POST", "/api/migu/album-details/crawl", [controller = controller_](api::http::HttpRequest req) {
        return withCors(controller->handleStartMiguAlbumDetailsBackfill(std::move(req)));
    });

    routes.route("OPTIONS", "/api/migu/album-songs/crawl", handleCorsOptions);
    routes.route("POST", "/api/migu/album-songs/crawl", [controller = controller_](api::http::HttpRequest req) {
        return withCors(controller->handleStartMiguAlbumSongsBackfill(std::move(req)));
    });

    routes.route("OPTIONS", "/api/migu/tasks/stop", handleCorsOptions);
    routes.route("POST", "/api/migu/tasks/stop", [controller = controller_](api::http::HttpRequest req) {
        return withCors(controller->handleStopMiguBatch(std::move(req)));
    });

    routes.route("OPTIONS", "/api/migu/tasks/progress", handleCorsOptions);
    routes.route("GET", "/api/migu/tasks/progress", [controller = controller_](api::http::HttpRequest req) {
        return withCors(controller->handleGetMiguBatchProgress(std::move(req)));
    });

    routes.route("OPTIONS", "/api/migu/tasks", handleCorsOptions);
    routes.route("GET", "/api/migu/tasks", [controller = controller_](api::http::HttpRequest req) {
        return withCors(controller->handleListMiguTasks(std::move(req)));
    });

    routes.route("OPTIONS", "/api/migu/tasks/summary", handleCorsOptions);
    routes.route("GET", "/api/migu/tasks/summary", [controller = controller_](api::http::HttpRequest req) {
        return withCors(controller->handleGetMiguTaskSummary(std::move(req)));
    });

    routes.route("OPTIONS", "/api/migu/tasks/{1}/rerun-failed", handleCorsOptions);
    routes.route("POST", "/api/migu/tasks/{1}/rerun-failed", [controller = controller_](api::http::HttpRequest req) {
        return withCors(controller->handleRerunFailedMiguTask(std::move(req)));
    });

    routes.route("OPTIONS", "/api/migu/tasks/{1}/clone", handleCorsOptions);
    routes.route("POST", "/api/migu/tasks/{1}/clone", [controller = controller_](api::http::HttpRequest req) {
        return withCors(controller->handleCloneMiguTask(std::move(req)));
    });

    routes.route("OPTIONS", "/api/migu/tasks/{1}/backfill-retry", handleCorsOptions);
    routes.route("POST", "/api/migu/tasks/{1}/backfill-retry", [controller = controller_](api::http::HttpRequest req) {
        return withCors(controller->handleBackfillRetryMiguTask(std::move(req)));
    });

    routes.route("OPTIONS", "/api/migu/tasks/{1}/archive", handleCorsOptions);
    routes.route("POST", "/api/migu/tasks/{1}/archive", [controller = controller_](api::http::HttpRequest req) {
        return withCors(controller->handleArchiveMiguTask(std::move(req)));
    });

    routes.route("OPTIONS", "/api/migu/tasks/{1}", handleCorsOptions);
    routes.route("GET", "/api/migu/tasks/{1}", [controller = controller_](api::http::HttpRequest req) {
        return withCors(controller->handleGetMiguTask(std::move(req)));
    });

    routes.route("OPTIONS", "/api/migu/tasks/{1}/items", handleCorsOptions);
    routes.route("GET", "/api/migu/tasks/{1}/items", [controller = controller_](api::http::HttpRequest req) {
        return withCors(controller->handleListMiguTaskItems(std::move(req)));
    });

    routes.route("OPTIONS", "/api/migu/tasks/{1}/events", handleCorsOptions);
    routes.route("GET", "/api/migu/tasks/{1}/events", [controller = controller_](api::http::HttpRequest req) {
        return withCors(controller->handleListMiguTaskEvents(std::move(req)));
    });

    routes.route("OPTIONS", "/api/migu/overview", handleCorsOptions);
    routes.route("GET", "/api/migu/overview", [controller = controller_](api::http::HttpRequest req) {
        return withCors(controller->handleGetMiguOverview(std::move(req)));
    });

    routes.route("OPTIONS", "/api/migu/integrity", handleCorsOptions);
    routes.route("GET", "/api/migu/integrity", [controller = controller_](api::http::HttpRequest req) {
        return withCors(controller->handleGetMiguIntegrity(std::move(req)));
    });

    routes.route("OPTIONS", "/api/migu/schema/status", handleCorsOptions);
    routes.route("GET", "/api/migu/schema/status", [controller = controller_](api::http::HttpRequest req) {
        return withCors(controller->handleGetMiguSchemaStatus(std::move(req)));
    });

    routes.route("OPTIONS", "/api/migu/maintenance/recover-stale", handleCorsOptions);
    routes.route("POST", "/api/migu/maintenance/recover-stale", [controller = controller_](api::http::HttpRequest req) {
        return withCors(controller->handleRecoverMiguStale(std::move(req)));
    });

    routes.route("OPTIONS", "/api/migu/singers", handleCorsOptions);
    routes.route("GET", "/api/migu/singers", [controller = controller_](api::http::HttpRequest req) {
        return withCors(controller->handleListMiguSingers(std::move(req)));
    });
    routes.route("OPTIONS", "/api/migu/singers/{1}/songs", handleCorsOptions);
    routes.route("GET", "/api/migu/singers/{1}/songs", [controller = controller_](api::http::HttpRequest req) {
        return withCors(controller->handleListMiguSingerSongs(std::move(req)));
    });
    routes.route("OPTIONS", "/api/migu/singers/{1}/albums", handleCorsOptions);
    routes.route("GET", "/api/migu/singers/{1}/albums", [controller = controller_](api::http::HttpRequest req) {
        return withCors(controller->handleListMiguSingerAlbums(std::move(req)));
    });
    routes.route("OPTIONS", "/api/migu/singers/{1}", handleCorsOptions);
    routes.route("GET", "/api/migu/singers/{1}", [controller = controller_](api::http::HttpRequest req) {
        return withCors(controller->handleGetMiguSinger(std::move(req)));
    });

    routes.route("OPTIONS", "/api/migu/albums", handleCorsOptions);
    routes.route("GET", "/api/migu/albums", [controller = controller_](api::http::HttpRequest req) {
        return withCors(controller->handleListMiguAlbums(std::move(req)));
    });
    routes.route("OPTIONS", "/api/migu/albums/{1}/songs", handleCorsOptions);
    routes.route("GET", "/api/migu/albums/{1}/songs", [controller = controller_](api::http::HttpRequest req) {
        return withCors(controller->handleListMiguAlbumSongs(std::move(req)));
    });
    routes.route("OPTIONS", "/api/migu/albums/{1}", handleCorsOptions);
    routes.route("GET", "/api/migu/albums/{1}", [controller = controller_](api::http::HttpRequest req) {
        return withCors(controller->handleGetMiguAlbum(std::move(req)));
    });

    routes.route("OPTIONS", "/api/migu/songs", handleCorsOptions);
    routes.route("GET", "/api/migu/songs", [controller = controller_](api::http::HttpRequest req) {
        return withCors(controller->handleListMiguSongs(std::move(req)));
    });
    routes.route("OPTIONS", "/api/migu/songs/{1}", handleCorsOptions);
    routes.route("GET", "/api/migu/songs/{1}", [controller = controller_](api::http::HttpRequest req) {
        return withCors(controller->handleGetMiguSong(std::move(req)));
    });

    routes.route("OPTIONS", "/api/migu/resources/queue", handleCorsOptions);
    routes.route("GET", "/api/migu/resources/queue", [controller = controller_](api::http::HttpRequest req) {
        return withCors(controller->handleListMiguResources(std::move(req)));
    });
    routes.route("OPTIONS", "/api/migu/resources/preview", handleCorsOptions);
    routes.route("POST", "/api/migu/resources/preview", [controller = controller_](api::http::HttpRequest req) {
        return withCors(controller->handlePreviewMiguResources(std::move(req)));
    });
    routes.route("OPTIONS", "/api/migu/resources/retry", handleCorsOptions);
    routes.route("POST", "/api/migu/resources/retry", [controller = controller_](api::http::HttpRequest req) {
        return withCors(controller->handleRetryMiguResources(std::move(req)));
    });
    routes.route("OPTIONS", "/api/migu/resources/reset", handleCorsOptions);
    routes.route("POST", "/api/migu/resources/reset", [controller = controller_](api::http::HttpRequest req) {
        return withCors(controller->handleResetMiguResources(std::move(req)));
    });
    routes.route("OPTIONS", "/api/migu/resources/recover-stale", handleCorsOptions);
    routes.route("POST", "/api/migu/resources/recover-stale", [controller = controller_](api::http::HttpRequest req) {
        return withCors(controller->handleRecoverStaleMiguResources(std::move(req)));
    });
    routes.route("OPTIONS", "/api/migu/resources/process", handleCorsOptions);
    routes.route("POST", "/api/migu/resources/process", [controller = controller_](api::http::HttpRequest req) {
        return withCors(controller->handleProcessMiguResources(std::move(req)));
    });

    routes.route("OPTIONS", "/api/migu/dictionaries", handleCorsOptions);
    routes.route("GET", "/api/migu/dictionaries", [controller = controller_](api::http::HttpRequest req) {
        return withCors(controller->handleGetMiguDictionaries(std::move(req)));
    });

    routes.route("OPTIONS", "/api/migu/governance/events", handleCorsOptions);
    routes.route("GET", "/api/migu/governance/events", [controller = controller_](api::http::HttpRequest req) {
        return withCors(controller->handleListMiguGovernanceEvents(std::move(req)));
    });

    routes.route("OPTIONS", "/api/migu/backfill/{1}/queue", handleCorsOptions);
    routes.route("GET", "/api/migu/backfill/{1}/queue", [controller = controller_](api::http::HttpRequest req) {
        return withCors(controller->handleListMiguBackfillQueue(std::move(req)));
    });
    routes.route("OPTIONS", "/api/migu/backfill/{1}/preview", handleCorsOptions);
    routes.route("POST", "/api/migu/backfill/{1}/preview", [controller = controller_](api::http::HttpRequest req) {
        return withCors(controller->handlePreviewMiguBackfill(std::move(req)));
    });
    routes.route("OPTIONS", "/api/migu/backfill/{1}/retry", handleCorsOptions);
    routes.route("POST", "/api/migu/backfill/{1}/retry", [controller = controller_](api::http::HttpRequest req) {
        return withCors(controller->handleRetryMiguBackfill(std::move(req)));
    });
    routes.route("OPTIONS", "/api/migu/backfill/{1}/reset", handleCorsOptions);
    routes.route("POST", "/api/migu/backfill/{1}/reset", [controller = controller_](api::http::HttpRequest req) {
        return withCors(controller->handleResetMiguBackfill(std::move(req)));
    });

    routes.route("OPTIONS", "/api/migu/settings", handleCorsOptions);
    routes.route("GET", "/api/migu/settings", [controller = controller_](api::http::HttpRequest req) {
        return withCors(controller->handleGetMiguSettings(std::move(req)));
    });
    routes.route("PUT", "/api/migu/settings", [controller = controller_](api::http::HttpRequest req) {
        return withCors(controller->handleUpdateMiguSettings(std::move(req)));
    });
    routes.route("PATCH", "/api/migu/settings", [controller = controller_](api::http::HttpRequest req) {
        return withCors(controller->handleUpdateMiguSettings(std::move(req)));
    });

    routes.route("OPTIONS", "/api/migu/pacmtoken/refresh", handleCorsOptions);
    routes.route("POST", "/api/migu/pacmtoken/refresh", [controller = controller_](api::http::HttpRequest req) {
        return withCors(controller->handleRefreshMiguPacmtoken(std::move(req)));
    });
    routes.route("OPTIONS", "/api/migu/pacmtoken/state", handleCorsOptions);
    routes.route("GET", "/api/migu/pacmtoken/state", [controller = controller_](api::http::HttpRequest req) {
        return withCors(controller->handleGetMiguPacmtokenState(std::move(req)));
    });
    routes.route("OPTIONS", "/api/migu/pacmtoken/current", handleCorsOptions);
    routes.route("POST", "/api/migu/pacmtoken/current", [controller = controller_](api::http::HttpRequest req) {
        return withCors(controller->handleGetCurrentMiguPacmtoken(std::move(req)));
    });

    routes.route("GET", "/api/openapi.json", [controller = controller_](api::http::HttpRequest req) {
        return withCors(controller->handleOpenApi(std::move(req)));
    });

    routes.route("GET", "/api/health", handleHealth);
}

} // namespace orchestrator::modules::crawler::api::routing
