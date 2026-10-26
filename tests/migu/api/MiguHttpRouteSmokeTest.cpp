#include "crawler/api/controllers/CrawlerController.h"
#include "crawler/api/routing/CrawlerRoutes.h"
#include "crawler/application/use_cases/CrawlerServiceDependencies.h"
#include "crawler/infrastructure/http/client/AsioHttpClient.h"
#include "crawler/infrastructure/http/server/DrogonHttpServer.h"

#include <chrono>
#include <exception>
#include <functional>
#include <future>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace crawler = orchestrator::modules::crawler;
namespace controllers = crawler::api::controllers;
namespace model = crawler::application::model;
namespace ports = crawler::application::ports;
namespace routing = crawler::api::routing;
namespace use_cases = crawler::application::use_cases;
namespace http_client = crawler::infrastructure::http::client;
namespace http_server = crawler::infrastructure::http::server;

namespace {

// 测试断言失败时抛出明确错误，避免 assert 宏影响 clangd tweak 检查。
void require(bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// 将响应体转换为字符串。
std::string bodyString(const http_client::HttpResponse &response)
{
    return std::string(response.body.begin(), response.body.end());
}

// 本地 smoke 失败时保留协议诊断，区分路由返回错误与客户端丢失响应体。
std::string responseDescription(const http_client::HttpResponse &response)
{
    std::string text = "bytes=" + std::to_string(response.body.size());
    for (const auto &header : response.headers) text += "; " + header.name + "=" + header.value;
    return text + "; body=" + bodyString(response).substr(0, 200);
}

// 资源队列测试替身，返回一条无需访问外部平台的 lyric 资源。
class SmokeSongResourceQueue : public ports::SongResourceQueue {
public:
    bool claimed = false;                     // 是否已被认领
    int claim_count = 0;                      // 认领调用次数
    model::SongResourceClaimQuery last_query; // 最近一次认领查询

    std::vector<model::SongResourceQueueTarget> claimPending(model::SongResourceClaimQuery query) override
    {
        ++claim_count;
        last_query = query;
        if (claimed) {
            return {};
        }
        claimed = true;
        model::SongResourceQueueTarget target;
        target.resource_id = 9002;
        target.song_source_ref_id = 92;
        target.source = query.source;
        target.source_song_id = "2643";
        target.source_content_id = "600902000006889266";
        target.title = "夜曲";
        target.resource_kind = "lyric";
        target.remote_url = "https://example.test/lyric";
        return {target};
    }

    std::optional<model::SongResourceQueueTarget> claimRelated(model::SongResourceRelatedClaimRequest) override
    {
        return std::nullopt;
    }

    bool markSucceeded(model::SongResourceSuccessUpdate) override
    {
        return true;
    }

    bool markFailed(model::SongResourceFailureUpdate) override
    {
        return true;
    }

    bool markSkipped(model::SongResourceSkipUpdate) override
    {
        return true;
    }
};

class SmokeSongResourceProvider : public ports::SongResourceProvider {
public:
    std::optional<Response> fetchSongResource(model::SongResourceFetchRequest) override
    {
        return std::nullopt;
    }
};

class SmokeSongResourceParser : public ports::SongResourceParser {
public:
    model::SongResourceFetchResult parseSongResource(const std::vector<char> &) const override
    {
        return {};
    }
};

class SmokeSongResourceContentFetcher : public ports::SongResourceContentFetcher {
public:
    model::SongResourceContentFetchResult fetchSongResourceContent(model::SongResourceContentFetchRequest) override
    {
        model::SongResourceContentFetchResult result;
        result.ok = true;
        result.status_code = 200;
        result.content_type = "text/plain";
        result.body = {'o', 'k'};
        return result;
    }
};

class SmokeSongResourceBlobStore : public ports::SongResourceBlobStore {
public:
    model::SongResourceBlobSaveResult saveSongResourceBlob(model::SongResourceBlobSaveRequest request) override
    {
        model::SongResourceBlobSaveResult result;
        result.ok = true;
        result.local_path = request.relative_path_hint;
        result.size_bytes = request.data.size();
        result.checksum = "smoke";
        return result;
    }
};

class SmokeBatchTaskRepository : public ports::BatchTaskRepository {
public:
    bool createBatch(const model::CrawlBatchRecord &, const std::vector<model::CrawlBatchItemRecord> &) override
    {
        return true;
    }
    bool updateBatchProgress(const model::CrawlBatchRecord &) override
    {
        return true;
    }
    bool updateItem(const model::CrawlBatchItemRecord &) override
    {
        return true;
    }
    bool upsertItem(const model::CrawlBatchItemRecord &) override
    {
        return true;
    }
    bool appendEvent(const model::CrawlBatchEventRecord &) override
    {
        return true;
    }
    model::CrawlBatchPage listBatches(const model::CrawlBatchQuery &) override
    {
        return {};
    }
    std::optional<model::CrawlBatchRecord> findBatch(std::string) override
    {
        return std::nullopt;
    }
    model::CrawlBatchItemPage listItems(std::string, const model::CrawlBatchItemQuery &) override
    {
        return {};
    }
    model::CrawlBatchEventPage listEvents(std::string, const model::CrawlBatchEventQuery &) override
    {
        return {};
    }
    model::CrawlBatchSummary summary(std::string, int) override
    {
        return {};
    }
    bool archiveBatch(std::string, std::string) override
    {
        return false;
    }
    int recoverStaleRunningBatches(std::string, int, std::string) override
    {
        return 0;
    }
};

class ImmediateTaskExecutor : public ports::TaskExecutor {
public:
    void submit(std::function<void()> task) override
    {
        task();
    }
};

// 歌手来源同步队列测试替身，返回固定预览结果，不访问真实数据库。
class SmokeSingerSourceSyncQueue : public ports::SingerSourceSyncQueue {
public:
    std::vector<model::SingerSourceSyncTarget> claimPendingSongs(model::SingerSourceSyncQuery) override
    {
        return {};
    }

    std::vector<model::SingerSourceSyncTarget> claimPendingAlbums(model::SingerSourceSyncQuery) override
    {
        return {};
    }

    model::SingerSourceSyncPreviewResult previewPendingSongs(model::SingerSourceSyncQuery query) override
    {
        return preview(std::move(query), 3);
    }

    model::SingerSourceSyncPreviewResult previewPendingAlbums(model::SingerSourceSyncQuery query) override
    {
        return preview(std::move(query), 4);
    }

    bool updateSongsSyncStatus(std::string, std::string, int, int, std::string) override
    {
        return true;
    }

    bool updateAlbumsSyncStatus(std::string, std::string, int, int, std::string) override
    {
        return true;
    }

private:
    static model::SingerSourceSyncPreviewResult preview(model::SingerSourceSyncQuery query, int fallback_status)
    {
        model::SingerSourceSyncTarget target;
        target.source = query.source;
        target.source_singer_id = "112";
        target.name = "Codex Singer";
        target.source_country = query.country.empty() ? "huayu" : query.country;
        target.gender = query.gender.value_or(1);
        target.sync_status = query.sync_statuses.empty() ? fallback_status : query.sync_statuses.front();

        model::SingerSourceSyncPreviewResult result;
        result.matched_count = 2;
        result.eligible_count = 1;
        result.items.push_back(std::move(target));
        return result;
    }
};

// 创建不连接外部系统的控制器，用于真实 HTTP 路由 smoke。
std::shared_ptr<controllers::CrawlerController> makeController(std::shared_ptr<SmokeSongResourceQueue> resource_queue = {})
{
    if (!resource_queue) {
        resource_queue = std::make_shared<SmokeSongResourceQueue>();
    }
    use_cases::CrawlerServiceDependencies dependencies;
    dependencies.persistence.singer_source_sync_queue = std::make_shared<SmokeSingerSourceSyncQueue>();
    dependencies.persistence.song_resource_queue = std::move(resource_queue);
    dependencies.providers.song_resource_provider = std::make_shared<SmokeSongResourceProvider>();
    dependencies.parsers.song_resource_parser = std::make_shared<SmokeSongResourceParser>();
    dependencies.providers.song_resource_content_fetcher = std::make_shared<SmokeSongResourceContentFetcher>();
    dependencies.persistence.song_resource_blob_store = std::make_shared<SmokeSongResourceBlobStore>();
    dependencies.persistence.batch_task_repository = std::make_shared<SmokeBatchTaskRepository>();
    dependencies.executors.batch_executor = std::make_shared<ImmediateTaskExecutor>();
    auto service = std::make_shared<use_cases::CrawlerService>(std::move(dependencies));
    return std::make_shared<controllers::CrawlerController>(std::move(service));
}

// 捕获项目内路由注册结果，用于验证 CORS OPTIONS handler。
class CapturingRouteSink : public routing::RouteSink {
public:
    // 保存路由处理函数。
    void route(const std::string &method, const std::string &path, routing::RequestHandler handler) override
    {
        handlers_[method + " " + path] = std::move(handler);
    }

    // 调用已注册的路由处理函数。
    crawler::api::http::HttpResponse invoke(const std::string &method, const std::string &path)
    {
        auto it = handlers_.find(method + " " + path);
        if (it == handlers_.end()) {
            throw std::runtime_error("route not registered: " + method + " " + path);
        }
        crawler::api::http::HttpRequest request;
        request.method = method;
        request.path = path;
        return it->second(std::move(request));
    }

private:
    std::unordered_map<std::string, routing::RequestHandler> handlers_; // 已注册路由
};

// 验证 CORS OPTIONS 路由在 routing 层已注册并返回跨域头。
void assertCorsOptions()
{
    CapturingRouteSink sink;
    routing::CrawlerRoutes routes(makeController());
    routes.registerRoutes(sink);
    const auto response = sink.invoke("OPTIONS", "/api/migu/backfill/{1}/queue");
    require(response.status_code == 204, "OPTIONS route should return 204");
    const auto it = response.headers.find("Access-Control-Allow-Origin");
    require(it != response.headers.end() && it->second == "*", "OPTIONS route should include CORS origin");

    const auto maintenance = sink.invoke("OPTIONS", "/api/migu/maintenance/recover-stale");
    require(maintenance.status_code == 204, "maintenance OPTIONS route should return 204");

    const auto schema_status = sink.invoke("OPTIONS", "/api/migu/schema/status");
    require(schema_status.status_code == 204, "schema status OPTIONS route should return 204");

    const auto preview = sink.invoke("OPTIONS", "/api/migu/backfill/{1}/preview");
    require(preview.status_code == 204, "preview OPTIONS route should return 204");

    const auto singer_songs_from_db_preview = sink.invoke("OPTIONS", "/api/migu/singer-songs/crawl-from-db/preview");
    require(singer_songs_from_db_preview.status_code == 204, "singer songs from db preview OPTIONS route should return 204");

    const auto singer_albums_from_db_preview = sink.invoke("OPTIONS", "/api/migu/singer-albums/crawl-from-db/preview");
    require(singer_albums_from_db_preview.status_code == 204, "singer albums from db preview OPTIONS route should return 204");

    const auto resource_retry = sink.invoke("OPTIONS", "/api/migu/resources/retry");
    require(resource_retry.status_code == 204, "resource retry OPTIONS route should return 204");

    const auto resource_preview = sink.invoke("OPTIONS", "/api/migu/resources/preview");
    require(resource_preview.status_code == 204, "resource preview OPTIONS route should return 204");

    const auto resource_process = sink.invoke("OPTIONS", "/api/migu/resources/process");
    require(resource_process.status_code == 204, "resource process OPTIONS route should return 204");

    const auto settings = sink.invoke("OPTIONS", "/api/migu/settings");
    require(settings.status_code == 204, "settings OPTIONS route should return 204");

    const auto task_summary = sink.invoke("OPTIONS", "/api/migu/tasks/summary");
    require(task_summary.status_code == 204, "task summary OPTIONS route should return 204");

    const auto governance_events = sink.invoke("OPTIONS", "/api/migu/governance/events");
    require(governance_events.status_code == 204, "governance events OPTIONS route should return 204");

    const auto pacmtoken_refresh = sink.invoke("OPTIONS", "/api/migu/pacmtoken/refresh");
    require(pacmtoken_refresh.status_code == 204, "pacmtoken refresh OPTIONS route should return 204");

    const auto pacmtoken_state = sink.invoke("OPTIONS", "/api/migu/pacmtoken/state");
    require(pacmtoken_state.status_code == 204, "pacmtoken state OPTIONS route should return 204");

    const auto pacmtoken_current = sink.invoke("OPTIONS", "/api/migu/pacmtoken/current");
    require(pacmtoken_current.status_code == 204, "pacmtoken current OPTIONS route should return 204");
}

// 验证 HTTP 响应状态。
void requireStatus(const std::optional<http_client::HttpResponse> &response, int status, const std::string &message)
{
    require(response.has_value(), message + ": no response");
    require(response->status_code == status, message + ": unexpected status " + std::to_string(response->status_code));
}

} // namespace

int main()
{
    constexpr int kPort = 19080;
    http_server::DrogonHttpServer server(1, nullptr);
    std::thread server_thread;

    try {
        assertCorsOptions();

        server.bind("127.0.0.1", kPort);
        auto resource_queue = std::make_shared<SmokeSongResourceQueue>();
        routing::CrawlerRoutes routes(makeController(resource_queue));
        routes.registerRoutes(server);

        std::promise<void> start_signal;
        server_thread = std::thread([&server, &start_signal]() {
            start_signal.set_value();
            server.run();
        });
        start_signal.get_future().wait();
        std::this_thread::sleep_for(std::chrono::milliseconds(300));

        http_client::AsioHttpClient client;
        const std::string base_url = "http://127.0.0.1:" + std::to_string(kPort);

        auto singer_songs = client.get(base_url + "/api/migu/singers/112/songs?page=1&page_size=20");
        requireStatus(singer_songs, 200, "GET singer songs route");
        require(bodyString(*singer_songs).find("\"items\"") != std::string::npos, "singer songs response should be a page: " + responseDescription(*singer_songs));

        auto album_songs = client.get(base_url + "/api/migu/albums/600927015009001083/songs");
        requireStatus(album_songs, 200, "GET album songs route");
        require(bodyString(*album_songs).find("\"page_size\"") != std::string::npos, "album songs response should be a page");

        auto backfill_queue = client.get(base_url + "/api/migu/backfill/album-details/queue?status=0,3,4");
        requireStatus(backfill_queue, 200, "GET backfill queue route");
        require(bodyString(*backfill_queue).find("\"items\"") != std::string::npos, "backfill queue response should be a page");

        auto dictionaries = client.get(base_url + "/api/migu/dictionaries");
        requireStatus(dictionaries, 200, "GET dictionaries route");
        require(bodyString(*dictionaries).find("\"sync_status\"") != std::string::npos, "dictionaries response should include sync_status");

        auto schema_status = client.get(base_url + "/api/migu/schema/status");
        requireStatus(schema_status, 200, "GET schema status route");
        require(bodyString(*schema_status).find("\"schema_version\"") != std::string::npos, "schema status response should include schema_version");

        auto invalid_page = client.get(base_url + "/api/migu/singers/112/songs?page=0");
        requireStatus(invalid_page, 400, "GET invalid page route");

        auto unknown = client.get(base_url + "/api/migu/backfill/unknown/queue");
        requireStatus(unknown, 404, "GET unknown backfill route");

        auto options = client.get(base_url + "/api/health");
        requireStatus(options, 200, "GET health route");

        auto retry_bad_source = client.post(base_url + "/api/migu/backfill/singer-songs/retry", R"({"source":"qq"})", {"Content-Type: application/json"});
        requireStatus(retry_bad_source, 400, "POST bad source retry route");

        auto singer_songs_from_db_preview = client.post(base_url + "/api/migu/singer-songs/crawl-from-db/preview", R"({"source":"migu","max_targets":5,"filter":{"status":[3]}})", {"Content-Type: application/json"});
        requireStatus(singer_songs_from_db_preview, 200, "POST singer songs from db preview route");
        require(bodyString(*singer_songs_from_db_preview).find("\"singer_id\"") != std::string::npos, "singer songs from db preview response should include singer_id");
        require(bodyString(*singer_songs_from_db_preview).find("\"eligible_count\"") != std::string::npos, "singer songs from db preview response should include eligible_count");

        auto singer_albums_from_db_preview = client.post(base_url + "/api/migu/singer-albums/crawl-from-db/preview", R"({"source":"migu","filter":{"status":[4]}})", {"Content-Type: application/json"});
        requireStatus(singer_albums_from_db_preview, 200, "POST singer albums from db preview route");
        require(bodyString(*singer_albums_from_db_preview).find("\"sync_status\":4") != std::string::npos, "singer albums from db preview response should include sync_status");

        auto preview_ok = client.post(base_url + "/api/migu/backfill/singer-songs/preview", R"({"source":"migu","source_ids":["112"]})", {"Content-Type: application/json"});
        requireStatus(preview_ok, 200, "POST backfill preview route");
        require(bodyString(*preview_ok).find("\"eligible_count\"") != std::string::npos, "preview response should include eligible_count");

        auto preview_unknown = client.post(base_url + "/api/migu/backfill/unknown/preview", R"({"source":"migu"})", {"Content-Type: application/json"});
        requireStatus(preview_unknown, 404, "POST unknown preview route");

        auto resource_retry = client.post(base_url + "/api/migu/resources/retry", R"({"source":"migu","resource_ids":[1],"filter":{"status":[3]}})", {"Content-Type: application/json"});
        requireStatus(resource_retry, 200, "POST resource retry route");
        require(bodyString(*resource_retry).find("\"updated_count\"") != std::string::npos, "resource retry response should include updated_count");

        auto resource_reset_empty = client.post(base_url + "/api/migu/resources/reset", R"({"source":"migu"})", {"Content-Type: application/json"});
        requireStatus(resource_reset_empty, 400, "POST empty resource reset route");

        auto resource_recover = client.post(base_url + "/api/migu/resources/recover-stale", R"({"source":"migu","stale_after_seconds":60,"filter":{"kind":"audio"}})", {"Content-Type: application/json"});
        requireStatus(resource_recover, 200, "POST resource recover route");

        auto resource_preview = client.post(base_url + "/api/migu/resources/preview", R"({"source":"migu","action":"retry","resource_ids":[1],"filter":{"status":[3]}})", {"Content-Type: application/json"});
        requireStatus(resource_preview, 200, "POST resource preview route");
        require(bodyString(*resource_preview).find("\"eligible_count\"") != std::string::npos, "resource preview response should include eligible_count");

        auto resource_preview_bad = client.post(base_url + "/api/migu/resources/preview", R"({"source":"migu"})", {"Content-Type: application/json"});
        requireStatus(resource_preview_bad, 400, "POST bad resource preview route");

        auto resource_process = client.post(base_url + "/api/migu/resources/process", R"({"source":"migu","limit":1,"filter":{"kind":"lyric"}})", {"Content-Type: application/json"});
        requireStatus(resource_process, 200, "POST resource process route");
        require(bodyString(*resource_process).find("\"error_category\"") != std::string::npos, "resource process response should include error_category");
        require(bodyString(*resource_process).find("\"related\"") != std::string::npos, "resource process response should include related flag");
        require(bodyString(*resource_process).find("\"synthetic_related_skipped_count\"") != std::string::npos, "resource process response should include synthetic related skip count");

        auto resource_process_async = client.post(base_url + "/api/migu/resources/process", R"({"source":"migu","limit":1,"async":true,"filter":{"kind":"lyric"}})", {"Content-Type: application/json"});
        requireStatus(resource_process_async, 202, "POST async resource process route");
        require(bodyString(*resource_process_async).find("\"accepted\":true") != std::string::npos, "async resource process response should be accepted");

        resource_queue->claimed = false;
        auto resource_process_selected = client.post(base_url + "/api/migu/resources/process", R"({"source":"migu","limit":1,"offset":99,"resource_ids":[9002,9003],"filter":{"status":[2],"kind":"cover"}})", {"Content-Type: application/json"});
        requireStatus(resource_process_selected, 200, "POST selected resource process route");
        require(resource_queue->last_query.resource_ids.size() == 2, "selected resource process should pass explicit ids");
        require(resource_queue->last_query.resource_ids[0] == 9002 && resource_queue->last_query.resource_ids[1] == 9003, "selected resource ids should preserve request order");
        require(resource_queue->last_query.offset == 0, "selected resource process should ignore offset");
        require(resource_queue->last_query.resource_kind.empty(), "selected resource process should ignore kind filter");
        require(resource_queue->last_query.statuses.size() == 2 && resource_queue->last_query.statuses[0] == 0 && resource_queue->last_query.statuses[1] == 3, "selected resource process should only claim pending and failed statuses");

        auto resource_process_bad = client.post(base_url + "/api/migu/resources/process", R"({"source":"migu","limit":1001})", {"Content-Type: application/json"});
        requireStatus(resource_process_bad, 400, "POST bad resource process route");

        auto resource_process_bad_options = client.post(base_url + "/api/migu/resources/process", R"({"source":"migu","limit":1,"options":{"resource_download_timeout_ms":9999}})", {"Content-Type: application/json"});
        requireStatus(resource_process_bad_options, 400, "POST bad resource process options route");

        auto settings = client.get(base_url + "/api/migu/settings");
        requireStatus(settings, 200, "GET settings route");
        require(bodyString(*settings).find("\"resource_download_concurrency\"") != std::string::npos, "settings response should include resource download options");

        auto recover = client.post(base_url + "/api/migu/maintenance/recover-stale", R"({"source":"migu","stale_after_seconds":60,"types":["album-details"]})", {"Content-Type: application/json"});
        requireStatus(recover, 200, "POST recover stale route");
        require(bodyString(*recover).find("\"album_details_recovered\"") != std::string::npos, "recover stale response should include album_details_recovered");

        auto recover_invalid = client.post(base_url + "/api/migu/maintenance/recover-stale", R"({"source":"migu","types":["bad"]})", {"Content-Type: application/json"});
        requireStatus(recover_invalid, 400, "POST invalid recover stale route");

        auto task_summary = client.get(base_url + "/api/migu/tasks/summary");
        requireStatus(task_summary, 200, "GET task summary route");
        require(bodyString(*task_summary).find("\"running_count\"") != std::string::npos, "task summary response should include running_count");

        auto governance_events = client.get(base_url + "/api/migu/governance/events?page=1&page_size=20");
        requireStatus(governance_events, 200, "GET governance events route");
        require(bodyString(*governance_events).find("\"items\"") != std::string::npos, "governance events response should be a page");

        auto clone_bad = client.post(base_url + "/api/migu/tasks/missing/clone", R"({"mode":"bad"})", {"Content-Type: application/json"});
        requireStatus(clone_bad, 400, "POST bad task clone route");

        auto archive_missing = client.post(base_url + "/api/migu/tasks/missing/archive", R"({"reason":"test"})", {"Content-Type: application/json"});
        requireStatus(archive_missing, 404, "POST missing task archive route");

        server.stop();
        if (server_thread.joinable()) {
            server_thread.join();
        }

        std::cout << "MiguHttpRouteSmokeTest passed" << std::endl;
        return 0;
    } catch (const std::exception &e) {
        server.stop();
        if (server_thread.joinable()) {
            server_thread.join();
        }
        std::cerr << "MiguHttpRouteSmokeTest failed: " << e.what() << std::endl;
        return 1;
    }
}
