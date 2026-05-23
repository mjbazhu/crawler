#include "crawler/api/controllers/CrawlerController.h"
#include "crawler/application/ports/provider/MiguPacmtokenRefresher.h"
#include "crawler/application/use_cases/CrawlerServiceDependencies.h"
#include "crawler/application/use_cases/MiguPacmtokenService.h"
#include "crawler/infrastructure/music/migu/provider/MiguRuntimePacmtokenStore.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <functional>
#include <iostream>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

namespace crawler = orchestrator::modules::crawler;
namespace api = crawler::api;
namespace controllers = crawler::api::controllers;
namespace http = crawler::api::http;
namespace model = crawler::application::model;
namespace ports = crawler::application::ports;
namespace use_cases = crawler::application::use_cases;
namespace migu_provider = crawler::infrastructure::music::migu::provider;

namespace {

// 测试断言失败时抛出明确错误，避免 assert 宏影响 clangd tweak 检查。
void require(bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// 歌手来源同步队列测试替身，返回固定预览结果，不触发真实数据库。
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

// 创建不连接外部系统的控制器，用于验证 DTO 与 HTTP 状态映射。
std::shared_ptr<controllers::CrawlerController> makeController()
{
    use_cases::CrawlerServiceDependencies dependencies;
    dependencies.persistence.singer_source_sync_queue = std::make_shared<SmokeSingerSourceSyncQueue>();
    auto service = std::make_shared<use_cases::CrawlerService>(std::move(dependencies));
    return std::make_shared<controllers::CrawlerController>(std::move(service));
}

// 资源队列测试替身，返回一条无需访问外部平台的 lyric 资源。
class SmokeSongResourceQueue : public ports::SongResourceQueue {
public:
    bool claimed = false; // 是否已被认领

    std::vector<model::SongResourceQueueTarget> claimPending(model::SongResourceClaimQuery query) override
    {
        if (claimed) {
            return {};
        }
        claimed = true;
        model::SongResourceQueueTarget target;
        target.resource_id = 9001;
        target.song_source_ref_id = 91;
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

// 空 provider；lyric 直链处理不会调用该对象。
class SmokeSongResourceProvider : public ports::SongResourceProvider {
public:
    std::optional<Response> fetchSongResource(model::SongResourceFetchRequest) override
    {
        return std::nullopt;
    }
};

// 空 parser；lyric 直链处理不会调用该对象。
class SmokeSongResourceParser : public ports::SongResourceParser {
public:
    model::SongResourceFetchResult parseSongResource(const std::vector<char> &) const override
    {
        return {};
    }
};

// 内容下载测试替身，固定返回少量文本字节。
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

// 二进制保存测试替身，只回传路径和大小。
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

// 批次仓储测试替身，记录 resources/process 写入的批次生命周期。
class SmokeBatchTaskRepository : public ports::BatchTaskRepository {
public:
    std::vector<model::CrawlBatchRecord> created_batches;    // createBatch 调用记录
    std::vector<model::CrawlBatchRecord> progress_updates;   // updateBatchProgress 调用记录
    std::vector<model::CrawlBatchItemRecord> upserted_items; // upsertItem 调用记录
    std::vector<model::CrawlBatchEventRecord> events;        // appendEvent 调用记录
    std::optional<model::CrawlBatchQuery> last_batch_query;  // 最近一次批次查询参数
    model::CrawlBatchPage batch_page;                        // 批次查询响应

    bool createBatch(const model::CrawlBatchRecord &batch, const std::vector<model::CrawlBatchItemRecord> &) override
    {
        created_batches.push_back(batch);
        return true;
    }

    bool updateBatchProgress(const model::CrawlBatchRecord &batch) override
    {
        progress_updates.push_back(batch);
        return true;
    }

    bool updateItem(const model::CrawlBatchItemRecord &item) override
    {
        upserted_items.push_back(item);
        return true;
    }

    bool upsertItem(const model::CrawlBatchItemRecord &item) override
    {
        upserted_items.push_back(item);
        return true;
    }

    bool appendEvent(const model::CrawlBatchEventRecord &event) override
    {
        events.push_back(event);
        return true;
    }

    model::CrawlBatchPage listBatches(const model::CrawlBatchQuery &query) override
    {
        last_batch_query = query;
        return batch_page;
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
        return true;
    }

    int recoverStaleRunningBatches(std::string, int, std::string) override
    {
        return 0;
    }
};

// 可控异步执行器，测试中先捕获任务，再显式执行。
class CapturingTaskExecutor : public ports::TaskExecutor {
public:
    std::vector<std::function<void()>> tasks; // 待执行任务

    void submit(std::function<void()> task) override
    {
        tasks.push_back(std::move(task));
    }

    void runAll()
    {
        auto pending = std::move(tasks);
        tasks.clear();
        for (auto &task : pending) {
            task();
        }
    }
};

// pacmtoken 刷新测试替身，避免 smoke 测试访问 Migu 上游。
class FakePacmtokenRefresher : public ports::MiguPacmtokenRefresher {
public:
    std::string last_token;               // 最近一次传给上游的 token
    std::string next_token = "new-token"; // 模拟上游返回的新 token

    model::MiguPacmtokenRefreshResult refreshPacmtoken(std::string pacmtoken) override
    {
        last_token = std::move(pacmtoken);
        model::MiguPacmtokenRefreshResult result;
        result.success = true;
        result.refreshed = true;
        result.pacmtoken = next_token;
        result.pacmtoken_length = next_token.size();
        result.upstream_status = 200;
        result.message = "ok";
        result.updated_at = 123;
        return result;
    }
};

struct PacmtokenControllerFixture {
    std::shared_ptr<controllers::CrawlerController> controller;      // 被测 controller
    std::shared_ptr<FakePacmtokenRefresher> refresher;               // 可观察刷新调用
    std::shared_ptr<migu_provider::MiguRuntimePacmtokenStore> store; // 运行期 token 存储
};

// 创建配置 pacmtoken service 的控制器。
PacmtokenControllerFixture makePacmtokenController()
{
    auto service = std::make_shared<use_cases::CrawlerService>(use_cases::CrawlerServiceDependencies {});
    auto refresher = std::make_shared<FakePacmtokenRefresher>();
    auto store = std::make_shared<migu_provider::MiguRuntimePacmtokenStore>();
    auto pacmtoken_service = std::make_shared<use_cases::MiguPacmtokenService>(refresher, store);
    return {std::make_shared<controllers::CrawlerController>(std::move(service), api::dto::CrawlerSettingsReadonlySnapshot {}, pacmtoken_service), refresher, store};
}

// 创建已配置资源 workflow 的控制器，用于 resources/process 200 smoke。
std::shared_ptr<controllers::CrawlerController> makeResourceController(std::shared_ptr<SmokeBatchTaskRepository> batch_repository = nullptr, std::shared_ptr<CapturingTaskExecutor> batch_executor = nullptr)
{
    use_cases::CrawlerServiceDependencies dependencies;
    dependencies.persistence.song_resource_queue = std::make_shared<SmokeSongResourceQueue>();
    dependencies.providers.song_resource_provider = std::make_shared<SmokeSongResourceProvider>();
    dependencies.parsers.song_resource_parser = std::make_shared<SmokeSongResourceParser>();
    dependencies.providers.song_resource_content_fetcher = std::make_shared<SmokeSongResourceContentFetcher>();
    dependencies.persistence.song_resource_blob_store = std::make_shared<SmokeSongResourceBlobStore>();
    dependencies.persistence.batch_task_repository = std::move(batch_repository);
    dependencies.executors.batch_executor = std::move(batch_executor);
    auto service = std::make_shared<use_cases::CrawlerService>(std::move(dependencies));
    return std::make_shared<controllers::CrawlerController>(std::move(service));
}

// 构造最小 HTTP 请求。
http::HttpRequest request(
    std::string method,
    std::string path,
    std::unordered_map<std::string, std::string> query = {},
    std::string body = {}
)
{
    http::HttpRequest req;
    req.method = std::move(method);
    req.path = std::move(path);
    req.query_params = std::move(query);
    req.body = std::move(body);
    return req;
}

// 解析 JSON 响应体。
nlohmann::json parseBody(const http::HttpResponse &response)
{
    return nlohmann::json::parse(response.body.empty() ? "{}" : response.body);
}

// 验证列表响应的统一分页结构。
void assertPageResponse(const http::HttpResponse &response)
{
    require(response.status_code == 200, "expected page response status 200");
    const auto body = parseBody(response);
    require(body.contains("items"), "page response missing items");
    require(body["items"].is_array(), "page response items must be array");
    require(body["page"].is_number_integer(), "page response page must be integer");
    require(body["page_size"].is_number_integer(), "page response page_size must be integer");
    require(body["total"].is_number_unsigned() || body["total"].is_number_integer(), "page response total must be integer");
}

// 覆盖批次列表的目标级筛选参数和聚合展示字段。
void testTaskListSearchFields()
{
    auto batch_repository = std::make_shared<SmokeBatchTaskRepository>();
    model::CrawlBatchRecord batch;
    batch.batch_id = "batch_search_1";
    batch.source = "migu";
    batch.task_type = "singerSongsFromDb";
    batch.state = "Failed";
    batch.last_error = "network timeout";
    batch.display_name = "周杰伦";
    batch.target_summary = "周杰伦 112 error:network timeout";
    batch.target_keywords = "singer:112 singerSongs 周杰伦 network network_failed";
    batch.error_category = "network";
    batch.result_code = "network_failed";
    batch.created_at = 100;
    batch.updated_at = 120;
    batch_repository->batch_page = model::CrawlBatchPage {{batch}, 2, 10, 1};

    auto controller = makeResourceController(batch_repository);
    const auto response = controller->handleListMiguTasks(request(
        "GET",
        "/api/migu/tasks",
        {
            {"page", "2"},
            {"page_size", "10"},
            {"q", "周杰伦"},
            {"display_name", "周杰伦"},
            {"target_summary", "timeout"},
            {"target_keywords", "singer:112"},
            {"last_error", "network"},
            {"error_category", "network"},
            {"result_code", "network_failed"},
            {"include_archived", "true"},
        }
    ));

    require(response.status_code == 200, "task list search should return 200");
    require(batch_repository->last_batch_query.has_value(), "task list should call batch repository");
    const auto &query = *batch_repository->last_batch_query;
    require(query.page == 2 && query.page_size == 10, "task list should pass pagination");
    require(query.keyword == "周杰伦", "task list should pass q");
    require(query.display_name == "周杰伦", "task list should pass display_name");
    require(query.target_summary == "timeout", "task list should pass target_summary");
    require(query.target_keywords == "singer:112", "task list should pass target_keywords");
    require(query.last_error == "network", "task list should pass last_error");
    require(query.error_category == "network", "task list should pass error_category");
    require(query.result_code == "network_failed", "task list should pass result_code");
    require(query.include_archived, "task list should pass include_archived");

    const auto body = parseBody(response);
    require(body["items"].size() == 1, "task list should return seeded batch");
    const auto &item = body["items"].front();
    require(item["display_name"].get<std::string>() == "周杰伦", "task list item missing display_name");
    require(item["target_summary"].get<std::string>().find("timeout") != std::string::npos, "task list item missing target_summary");
    require(item["target_keywords"].get<std::string>().find("singer:112") != std::string::npos, "task list item missing target_keywords");
    require(item["error_category"].get<std::string>() == "network", "task list item missing error_category");
    require(item["result_code"].get<std::string>() == "network_failed", "task list item missing result_code");
}

// 覆盖内容查询列表与详情的基础 HTTP 状态。
void testCatalogRoutes()
{
    auto controller = makeController();

    assertPageResponse(controller->handleListMiguSingers(request("GET", "/api/migu/singers", {{"page", "1"}, {"page_size", "20"}})));
    assertPageResponse(controller->handleListMiguAlbums(request("GET", "/api/migu/albums")));
    assertPageResponse(controller->handleListMiguSongs(request("GET", "/api/migu/songs", {{"status", "0,3"}, {"format", "HQ"}})));
    assertPageResponse(controller->handleListMiguResources(request("GET", "/api/migu/resources/queue", {{"kind", "audio"}})));
    const auto resource_preview = controller->handlePreviewMiguResources(request("POST", "/api/migu/resources/preview", {}, R"({"source":"migu","action":"retry","resource_ids":[1],"filter":{"status":[3]}})"));
    require(resource_preview.status_code == 200, "resource preview should return 200");
    const auto resource_preview_body = parseBody(resource_preview);
    require(resource_preview_body.contains("matched_count"), "resource preview missing matched_count");
    require(resource_preview_body.contains("eligible_count"), "resource preview missing eligible_count");
    require(controller->handlePreviewMiguResources(request("POST", "/api/migu/resources/preview", {}, R"({"source":"migu"})")).status_code == 400, "resource preview without action should return 400");
    require(controller->handleRetryMiguResources(request("POST", "/api/migu/resources/retry", {}, R"({"source":"migu"})")).status_code == 400, "empty resource retry should return 400");
    const auto resource_retry = controller->handleRetryMiguResources(request("POST", "/api/migu/resources/retry", {}, R"({"source":"migu","resource_ids":[1],"filter":{"status":[3]}})"));
    require(resource_retry.status_code == 200, "resource retry should return 200");
    require(parseBody(resource_retry).contains("items"), "resource retry missing items");
    const auto resource_reset = controller->handleResetMiguResources(request("POST", "/api/migu/resources/reset", {}, R"({"source":"migu","filter":{"status":[3],"kind":"audio"}})"));
    require(resource_reset.status_code == 200, "resource reset should return 200");
    const auto resource_recover = controller->handleRecoverStaleMiguResources(request("POST", "/api/migu/resources/recover-stale", {}, R"({"source":"migu","stale_after_seconds":60,"filter":{"kind":"audio"}})"));
    require(resource_recover.status_code == 200, "resource stale recovery should return 200");
    require(controller->handleProcessMiguResources(request("POST", "/api/migu/resources/process", {}, R"({"source":"qq","limit":1})")).status_code == 400, "non-migu resource process should return 400");
    require(controller->handleProcessMiguResources(request("POST", "/api/migu/resources/process", {}, R"({"source":"migu","limit":1001})")).status_code == 400, "invalid resource process limit should return 400");
    require(controller->handleProcessMiguResources(request("POST", "/api/migu/resources/process", {}, R"({"source":"migu","limit":1,"options":{"resource_download_concurrency":33}})")).status_code == 400, "invalid resource process options should return 400");
    require(controller->handleProcessMiguResources(request("POST", "/api/migu/resources/process", {}, R"({"source":"migu","limit":1})")).status_code == 503, "unconfigured resource process should return 503");
    auto resource_batch_repository = std::make_shared<SmokeBatchTaskRepository>();
    const auto resource_process = makeResourceController(resource_batch_repository)->handleProcessMiguResources(request("POST", "/api/migu/resources/process", {}, R"({"source":"migu","limit":1,"filter":{"kind":"lyric"},"options":{"resource_download_concurrency":2,"resource_download_retries":2,"resource_download_timeout_ms":30000,"pacmtoken_refresh_interval_seconds":60,"auto_refresh_pacmtoken":false}})"));
    require(resource_process.status_code == 200, "configured resource process should return 200");
    const auto resource_process_body = parseBody(resource_process);
    require(resource_process_body.contains("batch_id"), "resource process missing batch_id");
    require(resource_process_body.contains("synthetic_related_skipped_count"), "resource process missing synthetic related skip count");
    require(resource_process_body.contains("items"), "resource process missing items");
    require(!resource_process_body["items"].empty(), "resource process should include one item");
    require(resource_process_body["items"].front().contains("error_category"), "resource process item missing error_category");
    require(resource_process_body["items"].front().contains("related"), "resource process item missing related flag");
    require(resource_process_body["items"].front().contains("synthetic"), "resource process item missing synthetic flag");
    require(resource_process_body["items"].front().contains("source_content_id"), "resource process item missing source content id");
    require(resource_batch_repository->created_batches.size() == 1, "resource process should create one crawl batch");
    require(resource_batch_repository->created_batches.front().task_type == "resourceProcess", "resource process batch task_type mismatch");
    require(resource_batch_repository->created_batches.front().request_params.find("\"resource_download_concurrency\":2") != std::string::npos, "resource process batch request should include effective options");
    require(!resource_batch_repository->progress_updates.empty() && resource_batch_repository->progress_updates.back().state == "Completed", "resource process should complete crawl batch");
    require(std::any_of(resource_batch_repository->upserted_items.begin(), resource_batch_repository->upserted_items.end(), [](const model::CrawlBatchItemRecord &item) {
                return item.item_key == "resource:9001" && item.item_type == "songResource" && item.state == "Completed";
            }),
            "resource process should upsert completed batch item");
    require(std::any_of(resource_batch_repository->events.begin(), resource_batch_repository->events.end(), [](const model::CrawlBatchEventRecord &event) {
                return event.event_type == "batchStarted";
            }),
            "resource process should append batchStarted event");
    require(std::any_of(resource_batch_repository->events.begin(), resource_batch_repository->events.end(), [](const model::CrawlBatchEventRecord &event) {
                return event.event_type == "itemCompleted";
            }),
            "resource process should append itemCompleted event");

    auto async_resource_batch_repository = std::make_shared<SmokeBatchTaskRepository>();
    auto async_executor = std::make_shared<CapturingTaskExecutor>();
    const auto async_resource_process = makeResourceController(async_resource_batch_repository, async_executor)->handleProcessMiguResources(request("POST", "/api/migu/resources/process", {}, R"({"source":"migu","limit":1,"async":true,"filter":{"kind":"lyric"}})"));
    require(async_resource_process.status_code == 202, "async resource process should return 202");
    const auto async_body = parseBody(async_resource_process);
    require(async_body["accepted"].get<bool>(), "async resource process should be accepted");
    require(async_body["async"].get<bool>(), "async resource process should mark async true");
    require(!async_body["batch_id"].get<std::string>().empty(), "async resource process missing batch_id");
    require(async_resource_batch_repository->created_batches.size() == 1, "async resource process should create batch before returning");
    require(async_resource_batch_repository->created_batches.front().state == "Running", "async resource process batch should start running");
    require(async_executor->tasks.size() == 1, "async resource process should submit one task");
    async_executor->runAll();
    require(!async_resource_batch_repository->progress_updates.empty() && async_resource_batch_repository->progress_updates.back().state == "Completed", "async resource process should complete after task runs");
    require(std::any_of(async_resource_batch_repository->events.begin(), async_resource_batch_repository->events.end(), [](const model::CrawlBatchEventRecord &event) {
                return event.event_type == "itemStarted";
            }),
            "async resource process should append real-time itemStarted event");

    require(controller->handleListMiguSingers(request("GET", "/api/migu/singers", {{"page", "0"}})).status_code == 400, "invalid singer page should return 400");
    require(controller->handleListMiguSongs(request("GET", "/api/migu/songs", {{"page_size", "201"}})).status_code == 400, "invalid song page_size should return 400");
    require(controller->handleGetMiguSinger(request("GET", "/api/migu/singers/unknown")).status_code == 404, "unknown singer should return 404");
    require(controller->handleGetMiguAlbum(request("GET", "/api/migu/albums/unknown")).status_code == 404, "unknown album should return 404");
    require(controller->handleGetMiguSong(request("GET", "/api/migu/songs/unknown")).status_code == 404, "unknown song should return 404");
}

// 覆盖补池队列、retry、reset 和 integrity 的基础 HTTP 状态。
void testBackfillAndIntegrityRoutes()
{
    auto controller = makeController();

    const auto songs_from_db_preview = controller->handlePreviewMiguSingerSongsCrawlFromDb(request("POST", "/api/migu/singer-songs/crawl-from-db/preview", {}, R"({"source":"migu","max_targets":5,"filter":{"country":"huayu","gender":1,"status":[3]}})"));
    require(songs_from_db_preview.status_code == 200, "singer songs from db preview should return 200");
    const auto songs_from_db_preview_body = parseBody(songs_from_db_preview);
    require(songs_from_db_preview_body["matched_count"].get<int>() == 2, "singer songs from db preview missing matched count");
    require(songs_from_db_preview_body["eligible_count"].get<int>() == 1, "singer songs from db preview missing eligible count");
    require(!songs_from_db_preview_body["items"].empty(), "singer songs from db preview missing items");
    require(songs_from_db_preview_body["items"].front().contains("singer_id"), "singer songs from db preview item missing singer_id");
    require(songs_from_db_preview_body["items"].front().contains("source_name"), "singer songs from db preview item missing source_name");
    require(controller->handlePreviewMiguSingerSongsCrawlFromDb(request("POST", "/api/migu/singer-songs/crawl-from-db/preview", {}, R"({"source":"qq"})")).status_code == 400, "non-migu singer songs preview should return 400");

    const auto albums_from_db_preview = controller->handlePreviewMiguSingerAlbumsCrawlFromDb(request("POST", "/api/migu/singer-albums/crawl-from-db/preview", {}, R"({"source":"migu","filter":{"status":[4]}})"));
    require(albums_from_db_preview.status_code == 200, "singer albums from db preview should return 200");
    const auto albums_from_db_preview_body = parseBody(albums_from_db_preview);
    require(albums_from_db_preview_body["items"].front()["sync_status"].get<int>() == 4, "singer albums from db preview should expose album sync status");

    assertPageResponse(controller->handleListMiguBackfillQueue(request("GET", "/api/migu/backfill/singer-songs/queue")));
    const auto queue = controller->handleListMiguBackfillQueue(request("GET", "/api/migu/backfill/singer-songs/queue"));
    const auto queue_body = parseBody(queue);
    require(queue_body.contains("items"), "backfill queue missing items");
    assertPageResponse(controller->handleListMiguBackfillQueue(request("GET", "/api/migu/backfill/album-details/queue", {{"status", "0,3,4"}})));
    require(controller->handleListMiguBackfillQueue(request("GET", "/api/migu/backfill/unknown/queue")).status_code == 404, "unknown backfill queue should return 404");
    require(controller->handleListMiguBackfillQueue(request("GET", "/api/migu/backfill/singer-songs/queue", {{"page", "abc"}})).status_code == 400, "invalid backfill page should return 400");

    const auto preview = controller->handlePreviewMiguBackfill(request("POST", "/api/migu/backfill/singer-songs/preview", {}, R"({"source":"migu","source_ids":["112"],"filter":{"status":[3]}})"));
    require(preview.status_code == 200, "backfill preview should return 200");
    const auto preview_body = parseBody(preview);
    require(preview_body.contains("matched_count"), "preview missing matched_count");
    require(preview_body.contains("eligible_count"), "preview missing eligible_count");
    require(preview_body.contains("items"), "preview missing items");
    require(controller->handlePreviewMiguBackfill(request("POST", "/api/migu/backfill/unknown/preview", {}, R"({"source":"migu"})")).status_code == 404, "unknown preview should return 404");
    require(controller->handlePreviewMiguBackfill(request("POST", "/api/migu/backfill/singer-songs/preview", {}, R"({"source":"qq"})")).status_code == 400, "non-migu preview should return 400");

    require(controller->handleRetryMiguBackfill(request("POST", "/api/migu/backfill/singer-songs/retry", {}, R"({"source":"qq"})")).status_code == 400, "non-migu retry should return 400");
    require(controller->handleRetryMiguBackfill(request("POST", "/api/migu/backfill/unknown/retry", {}, R"({"source":"migu","source_ids":["112"]})")).status_code == 404, "unknown retry should return 404");
    require(controller->handleResetMiguBackfill(request("POST", "/api/migu/backfill/singer-songs/reset", {}, R"({"source":"migu"})")).status_code == 400, "empty reset should return 400");
    const auto reset_missing = controller->handleResetMiguBackfill(request("POST", "/api/migu/backfill/singer-songs/reset", {}, R"({"source":"migu","source_ids":["missing"]})"));
    require(reset_missing.status_code == 200, "explicit reset should return 200");
    const auto reset_body = parseBody(reset_missing);
    require(reset_body.contains("success_count"), "reset missing success_count");
    require(reset_body.contains("failed_count"), "reset missing failed_count");
    require(reset_body.contains("skipped_count"), "reset missing skipped_count");
    require(reset_body.contains("items"), "reset missing items");

    const auto integrity = controller->handleGetMiguIntegrity(request("GET", "/api/migu/integrity"));
    require(integrity.status_code == 200, "integrity should return 200");
    const auto body = parseBody(integrity);
    require(body.contains("singers_without_songs"), "integrity missing singers_without_songs");
    require(body.contains("singers_songs_running_stale"), "integrity missing singers_songs_running_stale");
    require(body.contains("running_batches_stale"), "integrity missing running_batches_stale");
    require(body.contains("generated_at"), "integrity missing generated_at");

    const auto schema_status = controller->handleGetMiguSchemaStatus(request("GET", "/api/migu/schema/status"));
    require(schema_status.status_code == 200, "schema status should return 200");
    const auto schema_body = parseBody(schema_status);
    require(schema_body.contains("schema_version"), "schema status missing schema_version");
    require(schema_body.contains("latest_available_version"), "schema status missing latest_available_version");
    require(schema_body.contains("pending_migrations"), "schema status missing pending_migrations");
    require(schema_body.contains("missing_tables"), "schema status missing missing_tables");

    const auto task_summary = controller->handleGetMiguTaskSummary(request("GET", "/api/migu/tasks/summary"));
    require(task_summary.status_code == 200, "task summary should return 200");
    const auto task_summary_body = parseBody(task_summary);
    require(task_summary_body.contains("state_distribution"), "task summary missing state_distribution");
    require(task_summary_body.contains("running_count"), "task summary missing running_count");
    require(controller->handleGetMiguTaskSummary(request("GET", "/api/migu/tasks/summary", {{"stale_after_seconds", "0"}})).status_code == 400, "invalid task summary stale threshold should return 400");

    require(controller->handleRerunFailedMiguTask(request("POST", "/api/migu/tasks/missing/rerun-failed", {}, R"({"limit":10})")).status_code == 404, "missing rerun should return 404");
    require(controller->handleCloneMiguTask(request("POST", "/api/migu/tasks/missing/clone", {}, R"({"mode":"bad"})")).status_code == 400, "bad clone mode should return 400");
    require(controller->handleBackfillRetryMiguTask(request("POST", "/api/migu/tasks/missing/backfill-retry", {}, R"({"limit":10})")).status_code == 404, "missing batch backfill retry should return 404");
    require(controller->handleArchiveMiguTask(request("POST", "/api/migu/tasks/missing/archive", {}, R"({"reason":"test"})")).status_code == 404, "missing archive should return 404");

    assertPageResponse(controller->handleListMiguGovernanceEvents(request("GET", "/api/migu/governance/events", {{"target_type", "resource"}})));

    const auto recovery = controller->handleRecoverMiguStale(request("POST", "/api/migu/maintenance/recover-stale", {}, R"({"source":"migu","stale_after_seconds":60,"types":["singer-songs","resources","batches"]})"));
    require(recovery.status_code == 200, "recover stale should return 200");
    const auto recovery_body = parseBody(recovery);
    require(recovery_body.contains("singer_songs_recovered"), "recovery missing singer_songs_recovered");
    require(recovery_body.contains("resources_recovered"), "recovery missing resources_recovered");
    require(controller->handleRecoverMiguStale(request("POST", "/api/migu/maintenance/recover-stale", {}, R"({"source":"qq"})")).status_code == 400, "non-migu recovery should return 400");
    require(controller->handleRecoverMiguStale(request("POST", "/api/migu/maintenance/recover-stale", {}, R"({"source":"migu","types":["unknown"]})")).status_code == 400, "unknown recovery type should return 400");

    const auto dictionaries = controller->handleGetMiguDictionaries(request("GET", "/api/migu/dictionaries"));
    require(dictionaries.status_code == 200, "dictionaries should return 200");
    const auto dict_body = parseBody(dictionaries);
    require(dict_body.contains("sync_status"), "dictionaries missing sync_status");
    require(dict_body.contains("result_code"), "dictionaries missing result_code");

    const auto bad_page = controller->handleListMiguSingers(request("GET", "/api/migu/singers", {{"page", "0"}}));
    const auto bad_page_body = parseBody(bad_page);
    require(bad_page_body.contains("error_code"), "error response missing error_code");
    require(bad_page_body.contains("message"), "error response missing message");
}

// 覆盖 settings API 的资源下载运行设置读写和范围校验。
void testSettingsRoute()
{
    auto controller = makeController();
    const auto current = controller->handleGetMiguSettings(request("GET", "/api/migu/settings"));
    require(current.status_code == 200, "settings GET should return 200");
    const auto current_body = parseBody(current);
    require(current_body["mutable"].contains("resource_download_concurrency"), "settings should include resource_download_concurrency");
    require(current_body["mutable"].contains("resource_download_retries"), "settings should include resource_download_retries");
    require(current_body["mutable"].contains("resource_download_timeout_ms"), "settings should include resource_download_timeout_ms");
    require(current_body["mutable"].contains("pacmtoken_refresh_interval_seconds"), "settings should include pacmtoken_refresh_interval_seconds");
    require(current_body["mutable"].contains("auto_refresh_pacmtoken"), "settings should include auto_refresh_pacmtoken");
    require(current_body["mutable"].contains("refresh_pacmtoken_on_440013"), "settings should include refresh_pacmtoken_on_440013");
    require(current_body["readonly"].contains("resource_worker_threads"), "settings should include resource_worker_threads readonly snapshot");

    const auto updated = controller->handleUpdateMiguSettings(request("PATCH", "/api/migu/settings", {}, R"({"resource_download_concurrency":5,"resource_download_retries":2,"resource_download_timeout_ms":30000,"pacmtoken_refresh_interval_seconds":120,"auto_refresh_pacmtoken":false,"refresh_pacmtoken_on_440013":false})"));
    require(updated.status_code == 200, "settings PATCH should return 200");
    const auto updated_body = parseBody(updated);
    require(updated_body["mutable"]["resource_download_concurrency"].get<int>() == 5, "settings should update resource_download_concurrency");
    require(updated_body["mutable"]["resource_download_retries"].get<int>() == 2, "settings should update resource_download_retries");
    require(updated_body["mutable"]["resource_download_timeout_ms"].get<int>() == 30000, "settings should update resource_download_timeout_ms");
    require(!updated_body["mutable"]["auto_refresh_pacmtoken"].get<bool>(), "settings should update auto_refresh_pacmtoken");
    require(!updated_body["mutable"]["refresh_pacmtoken_on_440013"].get<bool>(), "settings should update refresh_pacmtoken_on_440013");

    const auto invalid = controller->handleUpdateMiguSettings(request("PATCH", "/api/migu/settings", {}, R"({"resource_download_concurrency":0})"));
    require(invalid.status_code == 400, "invalid resource settings should return 400");
}

// 覆盖 pacmtoken 刷新接口的输入解析、运行期存储和未配置错误。
void testPacmtokenRoute()
{
    auto fixture = makePacmtokenController();
    const auto refreshed = fixture.controller->handleRefreshMiguPacmtoken(request("POST", "/api/migu/pacmtoken/refresh", {}, R"({"pacmtoken":"old-token"})"));
    require(refreshed.status_code == 200, "pacmtoken refresh should return 200");
    auto refreshed_body = parseBody(refreshed);
    require(refreshed_body["pacmtoken"].get<std::string>() == "new-token", "pacmtoken response should return new token");
    require(fixture.refresher->last_token == "old-token", "pacmtoken refresher should receive request token");
    require(fixture.store->currentPacmtoken().value_or("") == "new-token", "pacmtoken refresh should update runtime store");

    fixture.refresher->next_token = "cookie-token-new";
    const auto cookie_refresh = fixture.controller->handleRefreshMiguPacmtoken(request("POST", "/api/migu/pacmtoken/refresh", {}, R"({"cookie":"foo=1; pacmtoken=cookie-token; bar=2"})"));
    require(cookie_refresh.status_code == 200, "pacmtoken cookie refresh should return 200");
    require(fixture.refresher->last_token == "cookie-token", "pacmtoken service should extract token from cookie");
    require(parseBody(cookie_refresh)["pacmtoken"].get<std::string>() == "cookie-token-new", "cookie refresh should return updated token");

    fixture.refresher->next_token = "stored-token-new";
    const auto stored_refresh = fixture.controller->handleRefreshMiguPacmtoken(request("POST", "/api/migu/pacmtoken/refresh"));
    require(stored_refresh.status_code == 200, "pacmtoken refresh should use stored token when body is empty");
    require(fixture.refresher->last_token == "cookie-token-new", "empty refresh should reuse current runtime token");
    require(parseBody(stored_refresh)["pacmtoken"].get<std::string>() == "stored-token-new", "stored refresh should return updated token");

    const auto state = fixture.controller->handleGetMiguPacmtokenState(request("GET", "/api/migu/pacmtoken/state"));
    require(state.status_code == 200, "pacmtoken state should return 200");
    const auto state_body = parseBody(state);
    require(state_body["configured"].get<bool>(), "pacmtoken state should be configured");
    require(state_body["has_pacmtoken"].get<bool>(), "pacmtoken state should have token");
    require(state_body["pacmtoken_length"].get<int>() == static_cast<int>(std::string("stored-token-new").size()), "pacmtoken state should expose token length");
    require(!state_body.contains("pacmtoken"), "pacmtoken state should not expose token");

    const auto current = fixture.controller->handleGetCurrentMiguPacmtoken(request("POST", "/api/migu/pacmtoken/current"));
    require(current.status_code == 200, "pacmtoken current should return 200");
    const auto current_body = parseBody(current);
    require(current_body["pacmtoken"].get<std::string>() == "stored-token-new", "pacmtoken current should expose runtime token for frontend sync");

    const auto missing_service = makeController()->handleRefreshMiguPacmtoken(request("POST", "/api/migu/pacmtoken/refresh", {}, R"({"pacmtoken":"old-token"})"));
    require(missing_service.status_code == 503, "unconfigured pacmtoken refresh should return 503");
    require(makeController()->handleGetMiguPacmtokenState(request("GET", "/api/migu/pacmtoken/state")).status_code == 503, "unconfigured pacmtoken state should return 503");
    require(makeController()->handleGetCurrentMiguPacmtoken(request("POST", "/api/migu/pacmtoken/current")).status_code == 503, "unconfigured pacmtoken current should return 503");
}

} // namespace

int main()
{
    try {
        testTaskListSearchFields();
        testCatalogRoutes();
        testBackfillAndIntegrityRoutes();
        testSettingsRoute();
        testPacmtokenRoute();
        std::cout << "MiguCatalogApiSmokeTest passed" << std::endl;
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "MiguCatalogApiSmokeTest failed: " << e.what() << std::endl;
        return 1;
    }
}
