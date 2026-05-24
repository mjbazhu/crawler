#include "crawler/application/ports/execution/TaskExecutor.h"
#include "crawler/application/ports/parsing/SongResourceParser.h"
#include "crawler/application/ports/persistence/BatchTaskRepository.h"
#include "crawler/application/ports/persistence/SongResourceQueue.h"
#include "crawler/application/ports/provider/SongResourceContentFetcher.h"
#include "crawler/application/ports/provider/SongResourceProvider.h"
#include "crawler/application/ports/storage/SongResourceBlobStore.h"
#include "crawler/application/use_cases/CrawlerService.h"
#include "crawler/application/use_cases/CrawlerServiceDependencies.h"
#include "crawler/infrastructure/observability/logging/ConsoleDiagnosticsSink.h"
#include "crawler/infrastructure/observability/logging/Logger.h"

#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <iterator>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace crawler = orchestrator::modules::crawler;
namespace model = crawler::application::model;
namespace ports = crawler::application::ports;
namespace use_cases = crawler::application::use_cases;
namespace logging = crawler::infrastructure::observability::logging;

namespace {

// 测试断言失败时抛出明确错误，便于定位服务层生命周期缺口。
void require(bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// 读取完整文件内容用于断言运行日志落盘结果。
std::string readFile(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::in | std::ios::binary);
    require(static_cast<bool>(input), "failed to open file: " + path.string());
    return std::string(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
}

// 记录批次仓储调用，提供 listEvents 查询以覆盖服务层查询出口。
class RecordingBatchTaskRepository : public ports::BatchTaskRepository {
public:
    std::vector<model::CrawlBatchRecord> created_batches;    // createBatch 调用记录
    std::vector<model::CrawlBatchRecord> progress_updates;   // updateBatchProgress 调用记录
    std::vector<model::CrawlBatchItemRecord> upserted_items; // upsertItem/updateItem 调用记录
    std::vector<model::CrawlBatchEventRecord> events;        // appendEvent 调用记录

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

    model::CrawlBatchPage listBatches(const model::CrawlBatchQuery &) override
    {
        model::CrawlBatchPage page;
        page.items = created_batches;
        page.total = created_batches.size();
        return page;
    }

    std::optional<model::CrawlBatchRecord> findBatch(std::string batch_id) override
    {
        for (auto it = progress_updates.rbegin(); it != progress_updates.rend(); ++it) {
            if (it->batch_id == batch_id) {
                return *it;
            }
        }
        for (const auto &batch : created_batches) {
            if (batch.batch_id == batch_id) {
                return batch;
            }
        }
        return std::nullopt;
    }

    model::CrawlBatchItemPage listItems(std::string batch_id, const model::CrawlBatchItemQuery &query) override
    {
        model::CrawlBatchItemPage page;
        page.page = query.page;
        page.page_size = query.page_size;
        for (const auto &item : upserted_items) {
            if (item.batch_id == batch_id) {
                page.items.push_back(item);
            }
        }
        page.total = page.items.size();
        return page;
    }

    model::CrawlBatchEventPage listEvents(std::string batch_id, const model::CrawlBatchEventQuery &query) override
    {
        model::CrawlBatchEventPage page;
        page.page = query.page;
        page.page_size = query.page_size;
        for (const auto &event : events) {
            if (event.batch_id == batch_id) {
                page.items.push_back(event);
            }
        }
        page.total = page.items.size();
        return page;
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

    bool hasEvent(const std::string &event_type) const
    {
        return std::any_of(events.begin(), events.end(), [&](const model::CrawlBatchEventRecord &event) {
            return event.event_type == event_type;
        });
    }

    int eventIndex(const std::string &event_type) const
    {
        for (std::size_t index = 0; index < events.size(); ++index) {
            if (events[index].event_type == event_type) {
                return static_cast<int>(index);
            }
        }
        return -1;
    }
};

// 可控资源队列，服务层测试通过它驱动认领、成功回写和异常路径。
class ConfigurableSongResourceQueue : public ports::SongResourceQueue {
public:
    std::vector<model::SongResourceQueueTarget> next_targets; // 下次认领返回的目标
    bool throw_on_claim = false;                              // 是否模拟认领异常
    std::vector<model::SongResourceSuccessUpdate> succeeded;  // 成功回写记录
    std::vector<model::SongResourceFailureUpdate> failed;     // 失败回写记录
    std::vector<model::SongResourceSkipUpdate> skipped;       // 跳过回写记录

    std::vector<model::SongResourceQueueTarget> claimPending(model::SongResourceClaimQuery) override
    {
        if (throw_on_claim) {
            throw std::runtime_error("claim failed for service test");
        }
        auto targets = std::move(next_targets);
        next_targets.clear();
        return targets;
    }

    std::optional<model::SongResourceQueueTarget> claimRelated(model::SongResourceRelatedClaimRequest) override
    {
        return std::nullopt;
    }

    bool markSucceeded(model::SongResourceSuccessUpdate update) override
    {
        succeeded.push_back(std::move(update));
        return true;
    }

    bool markFailed(model::SongResourceFailureUpdate update) override
    {
        failed.push_back(std::move(update));
        return true;
    }

    bool markSkipped(model::SongResourceSkipUpdate update) override
    {
        skipped.push_back(std::move(update));
        return true;
    }
};

// 直链 lyric 测试不会调用 provider，但 CrawlerService 需要非空 port 以装配 workflow。
class StubSongResourceProvider : public ports::SongResourceProvider {
public:
    std::optional<Response> fetchSongResource(model::SongResourceFetchRequest) override
    {
        return std::nullopt;
    }
};

// 直链 lyric 测试不会调用 parser，但 CrawlerService 需要非空 port 以装配 workflow。
class StubSongResourceParser : public ports::SongResourceParser {
public:
    model::SongResourceFetchResult parseSongResource(const std::vector<char> &) const override
    {
        return {};
    }
};

// 固定返回少量文本内容，用于覆盖下载和 blob commit 后的成功回写。
class SuccessfulContentFetcher : public ports::SongResourceContentFetcher {
public:
    model::SongResourceContentFetchResult fetchSongResourceContent(model::SongResourceContentFetchRequest) override
    {
        model::SongResourceContentFetchResult result;
        result.ok = true;
        result.status_code = 200;
        result.content_type = "text/plain";
        result.body = {'o', 'k'};
        result.result_code = "ok";
        return result;
    }
};

// 内存 blob store，返回 workflow 规划出的相对路径并保持大小一致。
class SuccessfulBlobStore : public ports::SongResourceBlobStore {
public:
    model::SongResourceBlobSaveResult saveSongResourceBlob(model::SongResourceBlobSaveRequest request) override
    {
        model::SongResourceBlobSaveResult result;
        result.ok = true;
        result.local_path = std::move(request.relative_path_hint);
        result.size_bytes = request.data.size();
        result.checksum = "service-test-checksum";
        return result;
    }
};

// 提交时抛异常，覆盖 async 启动失败时的 batchFailed 收尾。
class ThrowingTaskExecutor : public ports::TaskExecutor {
public:
    void submit(std::function<void()>) override
    {
        throw std::runtime_error("executor rejected task");
    }
};

// 记录服务层镜像到运行日志的结构化事件。
class RecordingDiagnosticsSink : public ports::DiagnosticsSink {
public:
    std::vector<std::string> events; // 已收到的结构化事件名

    void log(ports::DiagnosticsLevel, std::string_view, std::string_view) override {}

    void event(ports::DiagnosticsLevel, std::string_view, std::string_view event_type, std::string_view, std::string_view) override
    {
        events.emplace_back(event_type);
    }

    bool hasEvent(const std::string &event_type) const
    {
        return std::find(events.begin(), events.end(), event_type) != events.end();
    }
};

// 创建只装配资源处理相关 port 的 CrawlerService。
std::unique_ptr<use_cases::CrawlerService> makeService(
    std::shared_ptr<RecordingBatchTaskRepository> batch_repository,
    std::shared_ptr<ConfigurableSongResourceQueue> resource_queue,
    std::shared_ptr<ports::TaskExecutor> batch_executor = nullptr,
    std::shared_ptr<ports::DiagnosticsSink> diagnostics = nullptr
)
{
    use_cases::CrawlerServiceDependencies dependencies;
    dependencies.persistence.batch_task_repository = std::move(batch_repository);
    dependencies.persistence.song_resource_queue = std::move(resource_queue);
    dependencies.providers.song_resource_provider = std::make_shared<StubSongResourceProvider>();
    dependencies.parsers.song_resource_parser = std::make_shared<StubSongResourceParser>();
    dependencies.providers.song_resource_content_fetcher = std::make_shared<SuccessfulContentFetcher>();
    dependencies.persistence.song_resource_blob_store = std::make_shared<SuccessfulBlobStore>();
    dependencies.executors.batch_executor = std::move(batch_executor);
    dependencies.diagnostics = std::move(diagnostics);
    dependencies.options.runtime_settings.auto_refresh_pacmtoken = false;
    return std::make_unique<use_cases::CrawlerService>(std::move(dependencies));
}

// 构造最小资源处理查询，默认关闭 token 刷新以避免测试依赖外部状态。
model::SongResourceClaimQuery makeQuery(std::string batch_id)
{
    model::SongResourceClaimQuery query;
    query.batch_id = std::move(batch_id);
    query.source = "migu";
    query.resource_kind = "lyric";
    query.limit = 1;
    query.option_overrides.auto_refresh_pacmtoken = false;
    return query;
}

// 构造一条可直链下载的 lyric 资源目标。
model::SongResourceQueueTarget lyricTarget()
{
    model::SongResourceQueueTarget target;
    target.resource_id = 9001;
    target.song_source_ref_id = 91;
    target.source = "migu";
    target.source_song_id = "song-9001";
    target.source_content_id = "content-9001";
    target.title = "Service Test Lyric";
    target.resource_kind = "lyric";
    target.remote_url = "https://example.test/lyric.lrc";
    target.status = 1;
    return target;
}

void assertEventOrder(const RecordingBatchTaskRepository &repository, const std::string &before, const std::string &after)
{
    const int before_index = repository.eventIndex(before);
    const int after_index = repository.eventIndex(after);
    require(before_index >= 0, "missing event " + before);
    require(after_index >= 0, "missing event " + after);
    require(before_index < after_index, before + " should be emitted before " + after);
}

// 空认领也必须写清楚“没有目标”并正常结束批次。
void testSyncProcessFinishesEmptyClaim()
{
    auto repository = std::make_shared<RecordingBatchTaskRepository>();
    auto queue = std::make_shared<ConfigurableSongResourceQueue>();
    auto service = makeService(repository, queue);

    const auto result = service->processMiguResources(makeQuery("service-empty-claim"));

    require(result.batch_id == "service-empty-claim", "empty result should keep batch id");
    require(result.claimed_count == 0, "empty claim should not claim resources");
    require(repository->hasEvent("batchStarted"), "empty claim should start batch");
    require(repository->hasEvent("resourceClaimStarted"), "empty claim should emit claim start");
    require(repository->hasEvent("resourceClaimCompleted"), "empty claim should emit claim complete");
    require(repository->hasEvent("resourceClaimEmpty"), "empty claim should emit claim empty");
    require(repository->hasEvent("batchFinished"), "empty claim should finish batch");
    assertEventOrder(*repository, "resourceClaimEmpty", "batchFinished");
    require(!repository->progress_updates.empty(), "empty claim should update final batch progress");
    require(repository->progress_updates.back().state == "Completed", "empty claim final batch state should be Completed");
    require(repository->progress_updates.back().target_count == 0, "empty claim target_count should be 0");

    const auto event_page = service->listBatchEvents("service-empty-claim", model::CrawlBatchEventQuery {});
    require(event_page.total == repository->events.size(), "service event query should return recorded empty-claim events");
}

// 成功处理一条资源时，服务层必须把 item 事件、DB 回写事件和 batchFinished 串起来。
void testSyncProcessFinishesSuccessfulResource()
{
    auto repository = std::make_shared<RecordingBatchTaskRepository>();
    auto queue = std::make_shared<ConfigurableSongResourceQueue>();
    queue->next_targets.push_back(lyricTarget());
    auto diagnostics = std::make_shared<RecordingDiagnosticsSink>();
    auto service = makeService(repository, queue, nullptr, diagnostics);

    const auto result = service->processMiguResources(makeQuery("service-success"));

    require(result.succeeded_count == 1, "successful resource should increment succeeded_count");
    require(queue->succeeded.size() == 1, "successful resource should be written back once");
    require(repository->hasEvent("itemStarted"), "successful resource should emit itemStarted");
    require(repository->hasEvent("resourceBlobCommitStarted"), "successful resource should emit blob commit start");
    require(repository->hasEvent("resourceDbWritebackCompleted"), "successful resource should emit db writeback completed");
    require(repository->hasEvent("itemCompleted"), "successful resource should emit itemCompleted");
    require(repository->hasEvent("batchFinished"), "successful resource should finish batch");
    require(diagnostics->hasEvent("resourceDbWritebackCompleted"), "successful resource should mirror writeback event to runtime log sink");
    require(diagnostics->hasEvent("batchFinished"), "successful resource should mirror batch finish to runtime log sink");
    assertEventOrder(*repository, "itemCompleted", "batchFinished");
    const auto completed_event = std::find_if(repository->events.begin(), repository->events.end(), [](const model::CrawlBatchEventRecord &event) {
        return event.event_type == "itemCompleted";
    });
    require(completed_event != repository->events.end(), "successful resource should record itemCompleted payload");
    require(completed_event->payload.find(R"("trace":"batch:service-success/item:resource:9001/index:0")") != std::string::npos, "itemCompleted payload should contain unified item trace");
    require(completed_event->payload.find(R"("batch_id":"service-success")") != std::string::npos, "itemCompleted payload should contain batch_id trace field");
    require(completed_event->payload.find(R"("item_index":0)") != std::string::npos, "itemCompleted payload should contain item_index trace field");
    require(completed_event->payload.find(R"("duration_ms":)") != std::string::npos, "itemCompleted payload should contain item duration");
    const auto batch_finished_event = std::find_if(repository->events.begin(), repository->events.end(), [](const model::CrawlBatchEventRecord &event) {
        return event.event_type == "batchFinished";
    });
    require(batch_finished_event != repository->events.end(), "successful resource should record batchFinished payload");
    require(batch_finished_event->payload.find(R"("trace":"batch:service-success")") != std::string::npos, "batchFinished payload should contain unified batch trace");
    require(batch_finished_event->payload.find(R"("duration_ms":)") != std::string::npos, "batchFinished payload should contain batch duration");
    require(!repository->progress_updates.empty(), "successful resource should update batch progress");
    require(repository->progress_updates.back().state == "Completed", "successful batch should be Completed");
    require(repository->progress_updates.back().success_count == 1, "successful batch should record one success");
    require(std::any_of(repository->upserted_items.begin(), repository->upserted_items.end(), [](const model::CrawlBatchItemRecord &item) {
                return item.item_key == "resource:9001" && item.state == "Completed" && item.written_count == 1;
            }),
            "successful resource should upsert completed item");
}

// 运行日志不能依赖批次仓储；数据库只负责结果展示，console/file sink 才是真实日志出口。
void testRuntimeLogDoesNotDependOnBatchRepository()
{
    const std::filesystem::path log_path = "build/service-resource-runtime.log";
    std::filesystem::remove(log_path);

    logging::Logger::Options options;
    options.level = logging::LogLevel::L_INFO;
    options.console_enabled = false;
    options.text_file_enabled = true;
    options.text_file_path = log_path.string();
    logging::Logger::instance().configure(std::move(options));

    auto queue = std::make_shared<ConfigurableSongResourceQueue>();
    queue->next_targets.push_back(lyricTarget());
    auto service = makeService(nullptr, queue, nullptr, std::make_shared<crawler::infrastructure::observability::logging::ConsoleDiagnosticsSink>());

    const auto result = service->processMiguResources(makeQuery("service-runtime-log"));

    require(result.succeeded_count == 1, "runtime log test should process one resource");
    const std::string content = readFile(log_path);
    require(content.find("[ResourceProcess]") != std::string::npos, "runtime log should contain ResourceProcess component");
    require(content.find("event=batchStarted") != std::string::npos, "runtime log should contain batchStarted");
    require(content.find("event=resourceDbWritebackCompleted") != std::string::npos, "runtime log should contain db writeback event");
    require(content.find("event=itemCompleted") != std::string::npos, "runtime log should contain itemCompleted summary");
    require(content.find("event=batchFinished") != std::string::npos, "runtime log should contain batchFinished");
    require(content.find("trace=batch:service-runtime-log") != std::string::npos, "runtime text log should contain unified trace");
    require(content.find("batch_id=service-runtime-log") != std::string::npos, "runtime text log should contain batch_id trace field");
    require(content.find("duration_ms=") != std::string::npos, "runtime text log should contain duration");
    require(content.find("stage=resource_claim_started") != std::string::npos, "runtime text log should render payload fields");
    require(content.find("payload={") == std::string::npos, "runtime text log should not contain raw JSON payload");

    logging::Logger::instance().configure(logging::Logger::Options {});
}

// JSON 格式下资源事件必须保留结构化字段，便于后续被日志采集系统解析。
void testRuntimeJsonLogContainsResourceEvents()
{
    const std::filesystem::path log_path = "build/service-resource-runtime-json.log";
    std::filesystem::remove(log_path);

    logging::Logger::Options options;
    options.level = logging::LogLevel::L_INFO;
    options.console_enabled = false;
    options.json_file_enabled = true;
    options.json_file_path = log_path.string();
    logging::Logger::instance().configure(std::move(options));

    auto queue = std::make_shared<ConfigurableSongResourceQueue>();
    queue->next_targets.push_back(lyricTarget());
    auto service = makeService(nullptr, queue, nullptr, std::make_shared<crawler::infrastructure::observability::logging::ConsoleDiagnosticsSink>());

    const auto result = service->processMiguResources(makeQuery("service-runtime-json-log"));

    require(result.succeeded_count == 1, "runtime json log test should process one resource");
    const std::string content = readFile(log_path);
    require(content.find(R"("component":"ResourceProcess")") != std::string::npos, "json runtime log should contain ResourceProcess component");
    require(content.find(R"("event_type":"batchStarted")") != std::string::npos, "json runtime log should contain batchStarted");
    require(content.find(R"("event_type":"resourceDbWritebackCompleted")") != std::string::npos, "json runtime log should contain db writeback event");
    require(content.find(R"("event_type":"batchFinished")") != std::string::npos, "json runtime log should contain batchFinished");
    require(content.find(R"("payload":{)") != std::string::npos, "json runtime log should preserve object payload");
    require(content.find(R"("trace":"batch:service-runtime-json-log")") != std::string::npos, "json runtime log should contain unified trace");
    require(content.find(R"("batch_id":"service-runtime-json-log")") != std::string::npos, "json runtime log should contain batch_id trace field");
    require(content.find(R"("duration_ms":)") != std::string::npos, "json runtime log should contain duration field");

    logging::Logger::instance().configure(logging::Logger::Options {});
}

// 同步 workflow 抛异常时也要写 batchFailed，避免批次停留在 Running。
void testSyncProcessFailureMarksBatchFailed()
{
    auto repository = std::make_shared<RecordingBatchTaskRepository>();
    auto queue = std::make_shared<ConfigurableSongResourceQueue>();
    queue->throw_on_claim = true;
    auto service = makeService(repository, queue);

    bool threw = false;
    try {
        (void)service->processMiguResources(makeQuery("service-claim-failed"));
    } catch (const std::runtime_error &) {
        threw = true;
    }

    require(threw, "claim failure should still propagate to caller");
    require(repository->hasEvent("batchStarted"), "failed sync process should start batch");
    require(repository->hasEvent("resourceClaimFailed"), "failed sync process should emit claim failed");
    require(repository->hasEvent("batchFailed"), "failed sync process should mark batch failed");
    assertEventOrder(*repository, "resourceClaimFailed", "batchFailed");
    require(!repository->progress_updates.empty(), "failed sync process should update batch progress");
    require(repository->progress_updates.back().state == "Failed", "failed sync process final batch state should be Failed");
}

// 异步任务提交失败时，startMiguResourcesProcessAsync 应立即写 batchFailed 并返回空批次 ID。
void testAsyncSubmitFailureMarksBatchFailed()
{
    auto repository = std::make_shared<RecordingBatchTaskRepository>();
    auto queue = std::make_shared<ConfigurableSongResourceQueue>();
    auto service = makeService(repository, queue, std::make_shared<ThrowingTaskExecutor>());

    const std::string batch_id = service->startMiguResourcesProcessAsync(makeQuery("service-submit-failed"));

    require(batch_id.empty(), "submit failure should return empty batch id");
    require(repository->hasEvent("batchStarted"), "submit failure should create batch before submit");
    require(repository->hasEvent("batchFailed"), "submit failure should append batchFailed");
    assertEventOrder(*repository, "batchStarted", "batchFailed");
    require(!repository->progress_updates.empty(), "submit failure should update batch progress");
    require(repository->progress_updates.back().state == "Failed", "submit failure final batch state should be Failed");
}

} // namespace

int main()
{
    try {
        testSyncProcessFinishesEmptyClaim();
        testSyncProcessFinishesSuccessfulResource();
        testRuntimeLogDoesNotDependOnBatchRepository();
        testRuntimeJsonLogContainsResourceEvents();
        testSyncProcessFailureMarksBatchFailed();
        testAsyncSubmitFailureMarksBatchFailed();
        std::cout << "CrawlerServiceResourceBatchEventTest passed." << std::endl;
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "CrawlerServiceResourceBatchEventTest failed: " << e.what() << std::endl;
        return 1;
    }
}
