#include "crawler/application/model/ProviderResponse.h"
#include "crawler/application/ports/persistence/AsyncSongResourceQueue.h"
#include "crawler/application/ports/provider/MiguPacmtokenRefresher.h"
#include "crawler/application/ports/provider/MiguPacmtokenStore.h"
#include "crawler/application/use_cases/AsyncSongResourceDownloadWorkflow.h"
#include "crawler/infrastructure/music/migu/parser/MiguSongResourceParser.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <deque>
#include <exception>
#include <iostream>
#include <memory>
#include <mutex>
#include <optional>
#include <span>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <utility>
#include <vector>

namespace crawler = orchestrator::modules::crawler;
namespace app_async = crawler::application::async;
namespace model = crawler::application::model;
namespace ports = crawler::application::ports;
namespace use_cases = crawler::application::use_cases;
namespace migu_parser = crawler::infrastructure::music::migu::parser;

namespace {

// 测试断言失败时抛出明确错误，避免依赖 assert 宏。
void require(bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// 将文本转换为字节。
std::vector<char> bytes(std::string text)
{
    return std::vector<char>(text.begin(), text.end());
}

// 轻量测试 runtime；阻塞任务放到独立线程，避免同步恢复掩盖协程调度问题。
class ThreadedAsyncRuntime : public ports::AsyncRuntime {
public:
    ~ThreadedAsyncRuntime() override
    {
        for (auto &thread : threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }

    bool spawn(app_async::Task<void> task) override
    {
        task.startDetached();
        return true;
    }

protected:
    void postBlockingCallback(std::function<void()> task) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        threads_.emplace_back(std::move(task));
    }

private:
    std::mutex mutex_;                 // 保护线程记录
    std::vector<std::thread> threads_; // 已提交阻塞任务线程
};

// 异步资源队列测试替身，记录认领和回写。
class FakeAsyncSongResourceQueue : public ports::AsyncSongResourceQueue {
public:
    std::vector<model::SongResourceQueueTarget> pending;         // 主资源待认领队列
    std::vector<model::SongResourceQueueTarget> related_targets; // 关联资源待认领队列
    std::vector<model::SongResourceSuccessUpdate> succeeded;     // 成功回写记录
    std::vector<model::SongResourceFailureUpdate> failed;        // 失败回写记录
    std::vector<model::SongResourceSkipUpdate> skipped;          // 跳过回写记录
    int related_claim_count = 0;                                 // 关联资源认领次数

    app_async::Task<std::vector<model::SongResourceQueueTarget>> claimPending(model::SongResourceClaimQuery query) override
    {
        std::vector<model::SongResourceQueueTarget> claimed;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            const auto count = (std::min)(pending.size(), query.limit);
            claimed.insert(claimed.end(), pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(count));
            pending.erase(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(count));
        }
        co_return claimed;
    }

    app_async::Task<std::optional<model::SongResourceQueueTarget>> claimRelated(model::SongResourceRelatedClaimRequest request) override
    {
        std::optional<model::SongResourceQueueTarget> claimed;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            ++related_claim_count;
            for (auto iter = related_targets.begin(); iter != related_targets.end(); ++iter) {
                if (iter->song_source_ref_id == request.song_source_ref_id && iter->resource_kind == request.resource_kind) {
                    auto target = *iter;
                    related_targets.erase(iter);
                    target.remote_url = request.remote_url;
                    claimed = std::move(target);
                    break;
                }
            }
        }
        co_return claimed;
    }

    app_async::Task<bool> markSucceeded(model::SongResourceSuccessUpdate update) override
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            succeeded.push_back(std::move(update));
        }
        co_return true;
    }

    app_async::Task<bool> markFailed(model::SongResourceFailureUpdate update) override
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            failed.push_back(std::move(update));
        }
        co_return true;
    }

    app_async::Task<bool> markSkipped(model::SongResourceSkipUpdate update) override
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            skipped.push_back(std::move(update));
        }
        co_return true;
    }

private:
    std::mutex mutex_; // 保护队列和回写记录
};

// 顺序 listen provider，用于验证重试会重新获取临时 URL。
class SequenceAsyncSongResourceProvider : public ports::AsyncSongResourceProvider {
public:
    std::vector<std::optional<Response>> responses;        // 顺序响应脚本
    std::vector<model::SongResourceFetchRequest> requests; // 请求记录

    app_async::Task<std::optional<Response>> fetchSongResource(model::SongResourceFetchRequest request) override
    {
        std::optional<Response> response;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            requests.push_back(std::move(request));
            const auto index = requests.size() - 1;
            if (index < responses.size()) {
                response = responses[index];
            }
        }
        co_return response;
    }

private:
    std::mutex mutex_; // 保护顺序游标
};

// 流式下载脚本响应。
struct StreamScript {
    bool ok = true;                             // 是否下载成功
    std::string content_type = "audio/flac";    // 响应 Content-Type
    std::vector<std::string> chunks {"ok"};     // 成功时交付的分片
    std::string error_category = "network";     // 失败分类
    std::string result_code = "network_failed"; // 失败编码
    std::string message = "download failed";    // 失败说明
};

// 可延迟的异步流式 fetcher，用于验证并发和重试。
class ScriptedAsyncContentFetcher : public ports::AsyncSongResourceContentFetcher {
public:
    std::chrono::milliseconds delay {0};                               // 每次下载延迟
    std::unordered_map<std::string, std::deque<StreamScript>> scripts; // URL 到顺序脚本
    std::vector<std::string> requested_urls;                           // 下载 URL 记录
    std::atomic<int> active {0};                                       // 当前活跃下载数
    std::atomic<int> max_active {0};                                   // 最大观察并发数

    app_async::Task<model::SongResourceContentStreamResult> streamSongResourceContent(
        model::SongResourceContentFetchRequest request,
        ports::AsyncSongResourceContentStreamCallbacks callbacks
    ) override
    {
        const int current = active.fetch_add(1) + 1;
        int observed = max_active.load();
        while (current > observed && !max_active.compare_exchange_weak(observed, current)) {
        }
        if (delay.count() > 0) {
            app_async::TaskCompletionSource<void> sleeper;
            std::thread([sleeper, wait = delay]() mutable {
                std::this_thread::sleep_for(wait);
                sleeper.set_value();
            }).detach();
            co_await sleeper.task();
        }

        StreamScript script;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            requested_urls.push_back(request.url);
            auto iter = scripts.find(request.url);
            if (iter != scripts.end() && !iter->second.empty()) {
                script = iter->second.front();
                iter->second.pop_front();
            }
        }

        model::SongResourceContentStreamResult result;
        result.status_code = script.ok ? 200 : 0;
        result.content_type = script.content_type;
        if (!script.ok) {
            result.error_category = script.error_category;
            result.result_code = script.result_code;
            result.message = script.message;
            active.fetch_sub(1);
            co_return result;
        }

        model::SongResourceContentStreamInfo info;
        info.status_code = 200;
        info.content_type = script.content_type;
        if (callbacks.on_response && !(co_await callbacks.on_response(info))) {
            result.error_category = "persistence";
            result.result_code = "stream_consumer_failed";
            result.message = "response rejected";
            active.fetch_sub(1);
            co_return result;
        }
        for (const auto &chunk : script.chunks) {
            if (callbacks.on_chunk && !(co_await callbacks.on_chunk(std::span<const char>(chunk.data(), chunk.size())))) {
                result.error_category = "persistence";
                result.result_code = "stream_consumer_failed";
                result.message = "chunk rejected";
                active.fetch_sub(1);
                co_return result;
            }
            result.bytes_received += static_cast<std::uint64_t>(chunk.size());
        }
        result.ok = true;
        result.result_code = "ok";
        active.fetch_sub(1);
        co_return result;
    }

private:
    std::mutex mutex_; // 保护脚本和请求记录
};

// 内存异步 blob store 会话。
class MemoryAsyncBlobSession : public ports::AsyncBlobWriteSession {
public:
    explicit MemoryAsyncBlobSession(std::string path, bool fail_commit = false) : path_(std::move(path)),
                                                                                  fail_commit_(fail_commit)
    {}

    app_async::Task<ports::AsyncBlobWriteResult> write(std::span<const char> chunk) override
    {
        data_.insert(data_.end(), chunk.begin(), chunk.end());
        ports::AsyncBlobWriteResult result;
        result.ok = true;
        co_return result;
    }

    app_async::Task<model::SongResourceBlobSaveResult> commit() override
    {
        model::SongResourceBlobSaveResult result;
        if (fail_commit_) {
            result.error = "commit_failed";
            co_return result;
        }
        result.ok = true;
        result.local_path = path_;
        result.size_bytes = data_.size();
        result.checksum = "memory";
        co_return result;
    }

    app_async::Task<void> abort() override
    {
        data_.clear();
        co_return;
    }

private:
    std::string path_;       // 保存路径
    std::vector<char> data_; // 已写入字节
    bool fail_commit_;       // 是否模拟提交失败
};

// 内存异步 blob store。
class MemoryAsyncBlobStore : public ports::AsyncSongResourceBlobStore {
public:
    bool fail_commit = false;                                     // 是否让 commit 失败
    std::vector<model::SongResourceBlobStreamSaveRequest> opened; // 打开记录

    app_async::Task<std::unique_ptr<ports::AsyncBlobWriteSession>> open(model::SongResourceBlobStreamSaveRequest request) override
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            opened.push_back(request);
        }
        co_return std::make_unique<MemoryAsyncBlobSession>(request.relative_path_hint, fail_commit);
    }

private:
    std::mutex mutex_; // 保护并发打开记录
};

// pacmtoken 刷新测试替身。
class FakePacmtokenRefresher : public ports::MiguPacmtokenRefresher {
public:
    std::atomic<int> calls {0}; // 刷新次数

    model::MiguPacmtokenRefreshResult refreshPacmtoken(std::string) override
    {
        ++calls;
        model::MiguPacmtokenRefreshResult result;
        result.success = true;
        result.refreshed = true;
        result.pacmtoken = "new-token";
        result.pacmtoken_length = result.pacmtoken.size();
        result.updated_at = 123;
        result.upstream_status = 200;
        return result;
    }
};

// pacmtoken 内存存储测试替身。
class FakePacmtokenStore : public ports::MiguPacmtokenStore {
public:
    std::optional<std::string> currentPacmtoken() const override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return token_;
    }

    void updatePacmtoken(std::string pacmtoken, std::uint64_t updated_at) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        token_ = std::move(pacmtoken);
        updated_at_ = updated_at;
    }

    std::uint64_t updatedAt() const override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return updated_at_;
    }

private:
    mutable std::mutex mutex_;         // 保护 token 状态
    std::optional<std::string> token_; // 当前 token
    std::uint64_t updated_at_ = 0;     // 最近更新时间
};

model::SongResourceQueueTarget audioTarget(std::uint64_t id = 101, std::string format = "ZQ24")
{
    model::SongResourceQueueTarget target;
    target.resource_id = id;
    target.song_source_ref_id = 11;
    target.source = "migu";
    target.source_song_id = "2643";
    target.source_content_id = "600902000006889266";
    target.source_resource_type = "2";
    target.audio_resource_type = "2";
    target.copyright_id = "60054701947";
    target.title = "夜曲";
    target.resource_kind = "audio";
    target.format_type = std::move(format);
    return target;
}

model::SongResourceQueueTarget directTarget(std::uint64_t id, std::string url)
{
    auto target = audioTarget(id, "");
    target.resource_kind = "lyric";
    target.remote_url = std::move(url);
    return target;
}

model::SongResourceQueueTarget relatedTarget(std::uint64_t id, std::string kind)
{
    auto target = audioTarget(id, "");
    target.resource_kind = std::move(kind);
    return target;
}

ports::AsyncSongResourceProvider::Response listenResponse(std::string url, std::string extra = {})
{
    ports::AsyncSongResourceProvider::Response response;
    response.status_code = 200;
    response.body = bytes(std::string(R"({"code":"000000","info":"操作成功","data":{"version":"v1","url":")") + url + R"(","audioFormatType":"ZQ24")" + extra + "}}");
    return response;
}

ports::AsyncSongResourceProvider::Response restrictedResponse()
{
    ports::AsyncSongResourceProvider::Response response;
    response.status_code = 200;
    response.body = bytes(R"({"code":"000000","info":"操作成功","data":{"dialogInfo":{"text":"需要刷新 pacmtoken"},"cannotCode":"440013"}})");
    return response;
}

ports::AsyncSongResourceProvider::Response listenRestrictedResponse(std::string code, std::string message, std::string extra = {})
{
    ports::AsyncSongResourceProvider::Response response;
    response.status_code = 200;
    response.body = bytes(std::string(R"({"code":"000000","info":"操作成功","data":{"dialogInfo":{"text":")") + message + R"("},"cannotCode":")" + code + R"(")" + extra + "}}");
    return response;
}

ports::AsyncSongResourceProvider::Response digitalAlbumRestrictedResponse()
{
    return listenRestrictedResponse("440016", "数字专辑需要购买后下载");
}

model::SongResourceClaimQuery baseQuery(std::string batch_id)
{
    model::SongResourceClaimQuery query;
    query.source = "migu";
    query.limit = 10;
    query.batch_id = std::move(batch_id);
    query.options.auto_refresh_pacmtoken = false;
    return query;
}

// 记录异步 workflow 阶段事件，用于验证批次事件覆盖关键步骤。
class RecordingResourceListener : public use_cases::SongResourceProcessListener {
public:
    void onBatchEvent(std::string event_type, std::string) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        events.push_back(std::move(event_type));
    }

    void onItemStarted(const model::SongResourceProcessItem &) override
    {}

    void onItemFinished(const model::SongResourceProcessItem &) override
    {}

    void onItemEvent(const model::SongResourceProcessItem &, std::string event_type, std::string) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        events.push_back(std::move(event_type));
    }

    bool hasEvent(const std::string &event_type) const
    {
        std::lock_guard<std::mutex> lock(mutex_);
        return std::find(events.begin(), events.end(), event_type) != events.end();
    }

    std::vector<std::string> events; // 已收到的事件名

private:
    mutable std::mutex mutex_; // 保护事件记录
};

model::SongResourceProcessResult runWorkflow(
    use_cases::AsyncSongResourceDownloadWorkflow &workflow,
    std::shared_ptr<ThreadedAsyncRuntime> runtime,
    model::SongResourceClaimQuery query,
    use_cases::SongResourceProcessListener *listener = nullptr
)
{
    return runtime->runForTest(workflow.processPending(std::move(query), listener));
}

void testAsyncWorkflowHonorsConcurrency()
{
    auto runtime = std::make_shared<ThreadedAsyncRuntime>();
    auto parser = std::make_shared<migu_parser::MiguSongResourceParser>();

    auto sequential_queue = std::make_shared<FakeAsyncSongResourceQueue>();
    sequential_queue->pending = {directTarget(1, "https://example.test/1"), directTarget(2, "https://example.test/2"), directTarget(3, "https://example.test/3")};
    auto sequential_fetcher = std::make_shared<ScriptedAsyncContentFetcher>();
    sequential_fetcher->delay = std::chrono::milliseconds(50);
    use_cases::AsyncSongResourceDownloadWorkflow sequential_workflow(runtime, sequential_queue, nullptr, parser, sequential_fetcher, std::make_shared<MemoryAsyncBlobStore>());
    auto sequential_query = baseQuery("async-concurrency-1");
    sequential_query.resource_kind = "lyric";
    sequential_query.options.resource_download_concurrency = 1;
    const auto sequential_result = runWorkflow(sequential_workflow, runtime, sequential_query);
    require(sequential_result.succeeded_count == 3, "concurrency=1 direct resources should succeed");
    require(sequential_fetcher->max_active.load() == 1, "concurrency=1 should not overlap downloads");

    auto concurrent_queue = std::make_shared<FakeAsyncSongResourceQueue>();
    concurrent_queue->pending = {directTarget(11, "https://example.test/11"), directTarget(12, "https://example.test/12"), directTarget(13, "https://example.test/13")};
    auto concurrent_fetcher = std::make_shared<ScriptedAsyncContentFetcher>();
    concurrent_fetcher->delay = std::chrono::milliseconds(50);
    use_cases::AsyncSongResourceDownloadWorkflow concurrent_workflow(runtime, concurrent_queue, nullptr, parser, concurrent_fetcher, std::make_shared<MemoryAsyncBlobStore>());
    auto concurrent_query = baseQuery("async-concurrency-3");
    concurrent_query.resource_kind = "lyric";
    concurrent_query.options.resource_download_concurrency = 3;
    const auto concurrent_result = runWorkflow(concurrent_workflow, runtime, concurrent_query);
    require(concurrent_result.succeeded_count == 3, "concurrency=3 direct resources should succeed");
    require(concurrent_fetcher->max_active.load() >= 2, "concurrency=3 should allow overlapping downloads");
}

void testAsyncWorkflowDeduplicatesRelatedResourcesUnderConcurrency()
{
    auto runtime = std::make_shared<ThreadedAsyncRuntime>();
    auto queue = std::make_shared<FakeAsyncSongResourceQueue>();
    queue->pending = {audioTarget(201, "PQ"), audioTarget(202, "HQ")};
    queue->related_targets = {relatedTarget(301, "lyric"), relatedTarget(302, "mrc"), relatedTarget(303, "trc")};

    auto provider = std::make_shared<SequenceAsyncSongResourceProvider>();
    provider->responses = {
        listenResponse("https://example.test/pq.flac", R"(,"lrcUrl":"https://example.test/lyric","mrcUrl":"https://example.test/mrc","trcUrl":"https://example.test/trc")"),
        listenResponse("https://example.test/hq.flac", R"(,"lrcUrl":"https://example.test/lyric","mrcUrl":"https://example.test/mrc","trcUrl":"https://example.test/trc")"),
    };
    auto fetcher = std::make_shared<ScriptedAsyncContentFetcher>();
    fetcher->delay = std::chrono::milliseconds(20);
    auto parser = std::make_shared<migu_parser::MiguSongResourceParser>();
    use_cases::AsyncSongResourceDownloadWorkflow workflow(runtime, queue, provider, parser, fetcher, std::make_shared<MemoryAsyncBlobStore>());

    auto query = baseQuery("async-related-dedup");
    query.resource_kind = "audio";
    query.limit = 2;
    query.options.resource_download_concurrency = 2;
    RecordingResourceListener listener;
    const auto result = runWorkflow(workflow, runtime, query, &listener);

    require(result.succeeded_count == 2, "both audio resources should succeed");
    require(result.related_succeeded_count == 3, "related lyric/mrc/trc should be saved once");
    require(queue->related_claim_count == 3, "related resources should only be claimed once per kind");
    require(queue->succeeded.size() == 5, "two audio and three related resources should be written back");
    require(listener.hasEvent("relatedResourceClaimStarted"), "async workflow should emit related claim started event");
    require(listener.hasEvent("relatedResourceClaimed"), "async workflow should emit related claimed event");
    require(listener.hasEvent("relatedResourceDeduplicated"), "async workflow should emit related deduplicated event");
}

void testAsyncWorkflowRetriesAudioWithFreshListenUrl()
{
    auto runtime = std::make_shared<ThreadedAsyncRuntime>();
    auto queue = std::make_shared<FakeAsyncSongResourceQueue>();
    queue->pending = {audioTarget()};
    auto provider = std::make_shared<SequenceAsyncSongResourceProvider>();
    provider->responses = {
        listenResponse("https://example.test/expired.flac"),
        listenResponse("https://example.test/fresh.flac"),
    };
    auto fetcher = std::make_shared<ScriptedAsyncContentFetcher>();
    StreamScript failed;
    failed.ok = false;
    failed.error_category = "network";
    failed.result_code = "network_failed";
    fetcher->scripts["https://example.test/expired.flac"].push_back(failed);
    fetcher->scripts["https://example.test/fresh.flac"].push_back(StreamScript {});

    auto parser = std::make_shared<migu_parser::MiguSongResourceParser>();
    use_cases::AsyncSongResourceDownloadWorkflow workflow(runtime, queue, provider, parser, fetcher, std::make_shared<MemoryAsyncBlobStore>());
    auto query = baseQuery("async-retry-fresh-url");
    query.resource_kind = "audio";
    query.limit = 1;
    query.options.resource_download_retries = 1;
    RecordingResourceListener listener;
    const auto result = runWorkflow(workflow, runtime, query, &listener);

    require(result.succeeded_count == 1, "network retry should succeed with fresh listen URL");
    require(provider->requests.size() == 2, "audio retry should call listen provider twice");
    require(fetcher->requested_urls.size() == 2, "audio retry should download twice");
    require(fetcher->requested_urls[0] == "https://example.test/expired.flac", "first attempt should use expired URL");
    require(fetcher->requested_urls[1] == "https://example.test/fresh.flac", "retry should use fresh URL");
    require(queue->failed.empty(), "intermediate retry failure should not be written back");
    require(queue->succeeded.size() == 1, "final success should be written back once");
    require(listener.hasEvent("resourceClaimStarted"), "async workflow should emit claim started event");
    require(listener.hasEvent("resourceClaimCompleted"), "async workflow should emit claim completed event");
    require(listener.hasEvent("resourceAttemptStarted"), "async workflow should emit attempt started event");
    require(listener.hasEvent("resourceRetryScheduled"), "async workflow should emit retry scheduled event");
    require(listener.hasEvent("resourceBlobCommitStarted"), "async workflow should emit blob commit started event");
    require(listener.hasEvent("resourceDbWritebackCompleted"), "async workflow should emit success writeback event");
}

void testAsyncWorkflowRefreshesPacmtokenOn440013()
{
    auto runtime = std::make_shared<ThreadedAsyncRuntime>();
    auto queue = std::make_shared<FakeAsyncSongResourceQueue>();
    queue->pending = {audioTarget()};
    auto provider = std::make_shared<SequenceAsyncSongResourceProvider>();
    provider->responses = {
        restrictedResponse(),
        listenResponse("https://example.test/fresh-after-token.flac"),
    };
    auto fetcher = std::make_shared<ScriptedAsyncContentFetcher>();
    fetcher->scripts["https://example.test/fresh-after-token.flac"].push_back(StreamScript {});
    auto parser = std::make_shared<migu_parser::MiguSongResourceParser>();
    auto refresher = std::make_shared<FakePacmtokenRefresher>();
    auto store = std::make_shared<FakePacmtokenStore>();
    store->updatePacmtoken("old-token", 1);

    use_cases::AsyncSongResourceDownloadWorkflow workflow(runtime, queue, provider, parser, fetcher, std::make_shared<MemoryAsyncBlobStore>(), {}, refresher, store);
    auto query = baseQuery("async-refresh-440013");
    query.resource_kind = "audio";
    query.options.auto_refresh_pacmtoken = true;
    query.options.refresh_pacmtoken_on_440013 = true;
    query.options.pacmtoken_refresh_interval_seconds = 0;
    query.options.resource_download_retries = 0;
    const auto result = runWorkflow(workflow, runtime, query);

    require(result.succeeded_count == 1, "440013 should recover after pacmtoken refresh");
    require(provider->requests.size() == 2, "440013 retry should request listen URL again");
    require(refresher->calls.load() >= 1, "440013 should trigger pacmtoken refresh");
    require(store->currentPacmtoken().value_or("") == "new-token", "runtime pacmtoken should be updated");
    require(queue->failed.empty(), "recovered 440013 should not mark resource failed");
}

void testAsyncWorkflowSkipsKnownDigitalAlbumBeforeFetch()
{
    auto runtime = std::make_shared<ThreadedAsyncRuntime>();
    auto queue = std::make_shared<FakeAsyncSongResourceQueue>();
    auto target = audioTarget();
    target.source_album_kind = 2;
    queue->pending = {target};

    auto provider = std::make_shared<SequenceAsyncSongResourceProvider>();
    auto parser = std::make_shared<migu_parser::MiguSongResourceParser>();
    use_cases::AsyncSongResourceDownloadWorkflow workflow(runtime, queue, provider, parser, std::make_shared<ScriptedAsyncContentFetcher>(), std::make_shared<MemoryAsyncBlobStore>());
    auto query = baseQuery("async-digital-skip");
    query.resource_kind = "audio";
    RecordingResourceListener listener;
    const auto result = runWorkflow(workflow, runtime, query, &listener);

    require(result.skipped_count == 1, "known digital album should be skipped");
    require(result.failed_count == 0, "known digital album should not fail");
    require(provider->requests.empty(), "known digital album should not call listen provider");
    require(queue->skipped.size() == 1, "known digital album should use skip writeback");
    require(queue->skipped.front().resource_kind == "audio", "known digital album should only skip audio resources");
    require(queue->skipped.front().result_code == "digital_album", "known digital album should use stable skip code");
    require(listener.hasEvent("resourceSkipWritebackCompleted"), "known digital album should emit skip writeback event");
}

void testAsyncWorkflowSkipsAudioOn440016()
{
    auto runtime = std::make_shared<ThreadedAsyncRuntime>();
    auto queue = std::make_shared<FakeAsyncSongResourceQueue>();
    queue->pending = {audioTarget()};

    auto provider = std::make_shared<SequenceAsyncSongResourceProvider>();
    provider->responses = {digitalAlbumRestrictedResponse()};
    auto parser = std::make_shared<migu_parser::MiguSongResourceParser>();
    use_cases::AsyncSongResourceDownloadWorkflow workflow(runtime, queue, provider, parser, std::make_shared<ScriptedAsyncContentFetcher>(), std::make_shared<MemoryAsyncBlobStore>());
    auto query = baseQuery("async-440016-skip");
    query.resource_kind = "audio";
    const auto result = runWorkflow(workflow, runtime, query);

    require(result.skipped_count == 1, "440016 should be counted as skipped");
    require(result.failed_count == 0, "440016 should not be failed");
    require(queue->failed.empty(), "440016 should not use failure writeback");
    require(queue->skipped.size() == 1, "440016 should use skip writeback");
    require(queue->skipped.front().song_source_ref_id == 11, "440016 skip should target song source ref");
    require(queue->skipped.front().resource_kind == "audio", "440016 skip should only cover audio");
    require(queue->skipped.front().format_type.empty(), "440016 should mark all audio formats skipped for the song");
    require(queue->skipped.front().result_code == "440016", "440016 skip should preserve platform code");
}

void testAsyncWorkflowDownloadsRelatedTextWhenAudioRestricted()
{
    const struct {
        const char *code;
        const char *message;
    } cases[] = {
        {"440015", "应版权方要求，该数字专辑购买后即可无限畅享"},
        {"440016", "数字专辑需要购买后下载"},
    };

    for (const auto &test_case : cases) {
        auto runtime = std::make_shared<ThreadedAsyncRuntime>();
        auto queue = std::make_shared<FakeAsyncSongResourceQueue>();
        queue->pending = {audioTarget()};
        queue->related_targets = {relatedTarget(301, "lyric"), relatedTarget(302, "mrc"), relatedTarget(303, "trc")};

        auto provider = std::make_shared<SequenceAsyncSongResourceProvider>();
        provider->responses = {
            listenRestrictedResponse(
                test_case.code,
                test_case.message,
                R"(,"lrcUrl":"https://example.test/lyric","mrcUrl":"https://example.test/mrc","trcUrl":"https://example.test/trc")"
            ),
        };
        auto parser = std::make_shared<migu_parser::MiguSongResourceParser>();
        use_cases::AsyncSongResourceDownloadWorkflow workflow(runtime, queue, provider, parser, std::make_shared<ScriptedAsyncContentFetcher>(), std::make_shared<MemoryAsyncBlobStore>());
        auto query = baseQuery(std::string("async-restricted-related-") + test_case.code);
        query.resource_kind = "audio";
        const auto result = runWorkflow(workflow, runtime, query);

        require(result.skipped_count == 1, std::string(test_case.code) + " should skip the audio task");
        require(result.failed_count == 0, std::string(test_case.code) + " should not fail the audio task");
        require(result.related_succeeded_count == 3, std::string(test_case.code) + " should still save lyric/mrc/trc");
        require(result.related_failed_count == 0, std::string(test_case.code) + " should not fail related text resources");
        require(result.related_skipped_count == 0, std::string(test_case.code) + " should not skip related text resources");
        require(queue->skipped.size() == 1, std::string(test_case.code) + " should write one audio skip");
        require(queue->skipped.front().resource_kind == "audio", std::string(test_case.code) + " skip should only target audio");
        require(queue->succeeded.size() == 3, std::string(test_case.code) + " should write lyric/mrc/trc success");
        require(std::count_if(result.items.begin(), result.items.end(), [](const model::SongResourceProcessItem &item) {
                    return item.related && item.ok;
                }) == 3,
                std::string(test_case.code) + " should report three related text successes");
    }
}

void testAsyncWorkflowSkipsPermanentListenRestrictions()
{
    const struct {
        const char *code;
        const char *message;
        const char *category;
    } cases[] = {
        {"440000", "该歌曲暂无版权", "copyright"},
        {"440015", "应版权方要求，该数字专辑购买后即可无限畅享", "resource"},
    };

    for (const auto &test_case : cases) {
        auto runtime = std::make_shared<ThreadedAsyncRuntime>();
        auto queue = std::make_shared<FakeAsyncSongResourceQueue>();
        queue->pending = {audioTarget()};

        auto provider = std::make_shared<SequenceAsyncSongResourceProvider>();
        provider->responses = {listenRestrictedResponse(test_case.code, test_case.message)};
        auto parser = std::make_shared<migu_parser::MiguSongResourceParser>();
        use_cases::AsyncSongResourceDownloadWorkflow workflow(runtime, queue, provider, parser, std::make_shared<ScriptedAsyncContentFetcher>(), std::make_shared<MemoryAsyncBlobStore>());
        auto query = baseQuery(std::string("async-permanent-skip-") + test_case.code);
        query.resource_kind = "audio";
        const auto result = runWorkflow(workflow, runtime, query);

        require(result.skipped_count == 1, std::string(test_case.code) + " should be counted as skipped");
        require(result.failed_count == 0, std::string(test_case.code) + " should not be failed");
        require(result.items.size() == 1 && result.items.front().skipped, std::string(test_case.code) + " item should be skipped");
        require(result.items.front().error_category == test_case.category, std::string(test_case.code) + " should expose stable category");
        require(result.items.front().result_code == test_case.code, std::string(test_case.code) + " should preserve platform code");
        require(queue->failed.empty(), std::string(test_case.code) + " should not use failure writeback");
        require(queue->skipped.size() == 1, std::string(test_case.code) + " should use skip writeback");
        require(queue->skipped.front().song_source_ref_id == 11, std::string(test_case.code) + " skip should target song source ref");
        require(queue->skipped.front().resource_kind == "audio", std::string(test_case.code) + " skip should only cover audio");
        require(queue->skipped.front().format_type.empty(), std::string(test_case.code) + " skip should cover all audio formats");
    }
}

void testAsyncWorkflowDoesNotWriteCompletedWhenCommitFails()
{
    auto runtime = std::make_shared<ThreadedAsyncRuntime>();
    auto queue = std::make_shared<FakeAsyncSongResourceQueue>();
    queue->pending = {audioTarget()};
    auto provider = std::make_shared<SequenceAsyncSongResourceProvider>();
    provider->responses = {listenResponse("https://example.test/audio.flac")};
    auto fetcher = std::make_shared<ScriptedAsyncContentFetcher>();
    fetcher->scripts["https://example.test/audio.flac"].push_back(StreamScript {});
    auto blob_store = std::make_shared<MemoryAsyncBlobStore>();
    blob_store->fail_commit = true;
    auto parser = std::make_shared<migu_parser::MiguSongResourceParser>();
    use_cases::AsyncSongResourceDownloadWorkflow workflow(runtime, queue, provider, parser, fetcher, blob_store);
    auto query = baseQuery("async-commit-failure");
    query.resource_kind = "audio";
    query.options.resource_download_retries = 0;
    const auto result = runWorkflow(workflow, runtime, query);

    require(result.failed_count == 1, "commit failure should fail resource");
    require(result.items.size() == 1, "commit failure should return one item");
    require(result.items.front().error_category == "persistence", "commit failure should expose persistence category");
    require(result.items.front().result_code == "persist_failed", "commit failure should use stable result code");
    require(queue->succeeded.empty(), "commit failure must not mark DB completed");
    require(queue->failed.size() == 1, "commit failure should mark resource failed");
}

} // namespace

int main()
{
    try {
        testAsyncWorkflowHonorsConcurrency();
        testAsyncWorkflowDeduplicatesRelatedResourcesUnderConcurrency();
        testAsyncWorkflowRetriesAudioWithFreshListenUrl();
        testAsyncWorkflowRefreshesPacmtokenOn440013();
        testAsyncWorkflowSkipsKnownDigitalAlbumBeforeFetch();
        testAsyncWorkflowSkipsAudioOn440016();
        testAsyncWorkflowDownloadsRelatedTextWhenAudioRestricted();
        testAsyncWorkflowSkipsPermanentListenRestrictions();
        testAsyncWorkflowDoesNotWriteCompletedWhenCommitFails();
        std::cout << "AsyncSongResourceDownloadWorkflowTest passed." << std::endl;
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "AsyncSongResourceDownloadWorkflowTest failed: " << e.what() << std::endl;
        return 1;
    }
}
