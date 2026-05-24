#include "crawler/application/ports/persistence/SongResourceQueue.h"
#include "crawler/application/ports/provider/MiguPacmtokenRefresher.h"
#include "crawler/application/use_cases/SongResourceDownloadWorkflow.h"
#include "crawler/infrastructure/http/client/HttpSongResourceContentFetcher.h"
#include "crawler/infrastructure/music/migu/parser/MiguSongResourceParser.h"
#include "crawler/infrastructure/music/migu/provider/MiguRuntimePacmtokenStore.h"
#include "crawler/infrastructure/storage/filesystem/FileStorage.h"
#include "mocks/MockHttpClient.h"
#include "mocks/MockMusicProvider.h"

#include <atomic>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

namespace crawler = orchestrator::modules::crawler;
namespace model = crawler::application::model;
namespace ports = crawler::application::ports;
namespace use_cases = crawler::application::use_cases;
namespace http_client = crawler::infrastructure::http::client;
namespace migu_parser = crawler::infrastructure::music::migu::parser;
namespace migu_provider = crawler::infrastructure::music::migu::provider;
namespace filesystem_storage = crawler::infrastructure::storage::filesystem;

namespace {

// 测试断言失败时抛出异常，避免依赖 assert 宏影响 clangd 检查。
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

// 资源队列测试替身，记录 worker 对 claim 和回写的调用。
class FakeSongResourceQueue : public ports::SongResourceQueue {
public:
    std::vector<model::SongResourceQueueTarget> pending;         // 主资源待认领队列
    std::vector<model::SongResourceQueueTarget> related_targets; // 关联文本资源队列
    std::vector<model::SongResourceSuccessUpdate> succeeded;     // 成功回写记录
    std::vector<model::SongResourceFailureUpdate> failed;        // 失败回写记录
    std::vector<model::SongResourceSkipUpdate> skipped;          // 跳过回写记录

    std::vector<model::SongResourceQueueTarget> claimPending(model::SongResourceClaimQuery query) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        std::vector<model::SongResourceQueueTarget> claimed;
        const auto count = (std::min)(pending.size(), query.limit);
        claimed.insert(claimed.end(), pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(count));
        pending.erase(pending.begin(), pending.begin() + static_cast<std::ptrdiff_t>(count));
        for (auto &target : claimed) {
            target.status = 1;
        }
        return claimed;
    }

    std::optional<model::SongResourceQueueTarget> claimRelated(model::SongResourceRelatedClaimRequest request) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        for (auto iter = related_targets.begin(); iter != related_targets.end(); ++iter) {
            if (iter->song_source_ref_id == request.song_source_ref_id && iter->resource_kind == request.resource_kind) {
                auto target = *iter;
                related_targets.erase(iter);
                target.remote_url = request.remote_url;
                target.status = 1;
                return target;
            }
        }
        return std::nullopt;
    }

    bool markSucceeded(model::SongResourceSuccessUpdate update) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        succeeded.push_back(std::move(update));
        return true;
    }

    bool markFailed(model::SongResourceFailureUpdate update) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        failed.push_back(std::move(update));
        return true;
    }

    bool markSkipped(model::SongResourceSkipUpdate update) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        skipped.push_back(std::move(update));
        return true;
    }

private:
    std::mutex mutex_; // 保护并发 workflow 测试中的队列和回写记录
};

// 简单线程执行器，用真实线程验证 workflow 内部并发上限。
class ThreadedTaskExecutor : public ports::TaskExecutor {
public:
    ~ThreadedTaskExecutor() override
    {
        for (auto &thread : threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }

    void submit(std::function<void()> task) override
    {
        threads_.emplace_back(std::move(task));
    }

private:
    std::vector<std::thread> threads_; // 已提交任务线程
};

// 内存 blob store，只验证 workflow 保存语义，不落真实文件。
class MemoryBlobStore : public ports::SongResourceBlobStore {
public:
    std::vector<model::SongResourceBlobSaveRequest> saved; // 保存请求记录

    model::SongResourceBlobSaveResult saveSongResourceBlob(model::SongResourceBlobSaveRequest request) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        model::SongResourceBlobSaveResult result;
        result.ok = true;
        result.local_path = request.relative_path_hint;
        result.size_bytes = request.data.size();
        result.checksum = "memory";
        saved.push_back(std::move(request));
        return result;
    }

private:
    std::mutex mutex_; // 保护并发保存记录
};

// 返回不一致保存结果的 blob store，用于验证 workflow 不会误回写完成。
class InvalidBlobStore : public ports::SongResourceBlobStore {
public:
    model::SongResourceBlobSaveResult saveSongResourceBlob(model::SongResourceBlobSaveRequest request) override
    {
        model::SongResourceBlobSaveResult result;
        result.ok = true;
        result.local_path = request.relative_path_hint;
        result.size_bytes = request.data.empty() ? 0 : request.data.size() - 1;
        result.checksum = "invalid";
        return result;
    }
};

// 固定响应 provider，线程安全记录 listen 请求次数。
class StaticSongResourceProvider : public ports::SongResourceProvider {
public:
    ports::SongResourceProvider::Response response; // 固定 listen 响应
    std::atomic<int> call_count {0};                // listen 请求次数

    std::optional<Response> fetchSongResource(model::SongResourceFetchRequest) override
    {
        ++call_count;
        return response;
    }
};

// 顺序响应 provider，用于验证音频重试会重新请求 listen URL。
class SequenceSongResourceProvider : public ports::SongResourceProvider {
public:
    std::vector<std::optional<Response>> responses; // 顺序 listen 响应
    int call_count = 0;                             // listen 请求次数

    std::optional<Response> fetchSongResource(model::SongResourceFetchRequest) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const auto index = static_cast<std::size_t>(call_count++);
        if (index >= responses.size()) {
            return std::nullopt;
        }
        return responses[index];
    }

private:
    std::mutex mutex_; // 保护顺序响应游标
};

// 可脚本化内容 fetcher，支持延迟、失败和成功响应。
class ScriptedContentFetcher : public ports::SongResourceContentFetcher {
public:
    std::chrono::milliseconds delay {0};                                                           // 每次下载延迟
    std::unordered_map<std::string, std::vector<model::SongResourceContentFetchResult>> responses; // URL 到顺序结果
    std::vector<std::string> requested_urls;                                                       // 下载 URL 记录
    std::atomic<int> active {0};                                                                   // 当前并发下载数
    std::atomic<int> max_active {0};                                                               // 观察到的最大并发下载数

    model::SongResourceContentFetchResult fetchSongResourceContent(model::SongResourceContentFetchRequest request) override
    {
        const int current = active.fetch_add(1) + 1;
        int observed = max_active.load();
        while (current > observed && !max_active.compare_exchange_weak(observed, current)) {
        }
        if (delay.count() > 0) {
            std::this_thread::sleep_for(delay);
        }

        model::SongResourceContentFetchResult result;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            requested_urls.push_back(request.url);
            auto iter = responses.find(request.url);
            if (iter != responses.end() && !iter->second.empty()) {
                result = iter->second.front();
                iter->second.erase(iter->second.begin());
            } else {
                result.ok = true;
                result.status_code = 200;
                result.content_type = "text/plain";
                result.body = bytes("ok");
            }
        }
        active.fetch_sub(1);
        return result;
    }

private:
    std::mutex mutex_; // 保护响应脚本和请求记录
};

// 只支持流式接口的内容 fetcher，用于验证 workflow 不再依赖完整 body 下载。
class StreamingOnlyContentFetcher : public ports::SongResourceContentFetcher {
public:
    std::vector<std::string> chunks {"aa", "bb"}; // 顺序 body 分片
    std::atomic<int> stream_call_count {0};       // 流式下载调用次数

    model::SongResourceContentFetchResult fetchSongResourceContent(model::SongResourceContentFetchRequest) override
    {
        model::SongResourceContentFetchResult result;
        result.ok = false;
        result.error_category = "network";
        result.result_code = "full_fetch_used";
        result.message = "workflow should use streamSongResourceContent";
        return result;
    }

    model::SongResourceContentStreamResult streamSongResourceContent(model::SongResourceContentFetchRequest, model::SongResourceContentStreamCallbacks callbacks) override
    {
        ++stream_call_count;
        model::SongResourceContentStreamResult result;
        result.status_code = 200;
        result.content_type = "text/plain";

        model::SongResourceContentStreamInfo info;
        info.status_code = 200;
        info.content_type = result.content_type;
        if (callbacks.on_response && !callbacks.on_response(info)) {
            result.error_category = "persistence";
            result.result_code = "stream_consumer_failed";
            result.message = "response rejected";
            return result;
        }

        for (const auto &chunk : chunks) {
            if (callbacks.on_chunk && !callbacks.on_chunk(chunk)) {
                result.error_category = "persistence";
                result.result_code = "stream_consumer_failed";
                result.message = "chunk rejected";
                return result;
            }
            result.bytes_received += static_cast<std::uint64_t>(chunk.size());
        }

        result.ok = true;
        result.result_code = "ok";
        return result;
    }
};

// pacmtoken 刷新 fake，验证 440013 后只刷新内存 token，不暴露明文到事件。
class FakePacmtokenRefresher : public ports::MiguPacmtokenRefresher {
public:
    std::atomic<int> call_count {0}; // 刷新次数

    model::MiguPacmtokenRefreshResult refreshPacmtoken(std::string) override
    {
        ++call_count;
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

// 构造一条 Migu audio 资源任务。
model::SongResourceQueueTarget audioTarget()
{
    model::SongResourceQueueTarget target;
    target.resource_id = 101;
    target.song_source_ref_id = 11;
    target.source = "migu";
    target.source_song_id = "2643";
    target.source_content_id = "600902000006889266";
    target.source_resource_type = "2";
    target.audio_resource_type = "2";
    target.copyright_id = "60054701947";
    target.title = "夜曲";
    target.resource_kind = "audio";
    target.format_type = "ZQ24";
    return target;
}

// 构造同一首歌下的歌词资源任务。
model::SongResourceQueueTarget relatedTarget(std::uint64_t id, std::string kind)
{
    auto target = audioTarget();
    target.resource_id = id;
    target.resource_kind = std::move(kind);
    target.format_type.clear();
    target.remote_url.clear();
    return target;
}

// 构造直链文本资源任务。
model::SongResourceQueueTarget directTextTarget(std::uint64_t id, std::string url)
{
    auto target = audioTarget();
    target.resource_id = id;
    target.resource_kind = "lyric";
    target.format_type.clear();
    target.remote_url = std::move(url);
    return target;
}

// 构造 Migu listen 原始响应。
ports::SongResourceProvider::Response listenResponse(std::string url, std::string extra = {})
{
    ports::SongResourceProvider::Response response;
    response.status_code = 200;
    response.body = bytes(std::string(R"({"code":"000000","info":"操作成功","data":{"version":"v1","url":")") + url + R"(","audioFormatType":"ZQ24")" + extra + "}}");
    return response;
}

// 构造 listen 返回 cannotCode 的平台限制响应。
ports::SongResourceProvider::Response listenRestrictedResponse(std::string code, std::string message, std::string extra = {})
{
    ports::SongResourceProvider::Response response;
    response.status_code = 200;
    response.body = bytes(std::string(R"({"code":"000000","info":"操作成功","data":{"dialogInfo":{"text":")") + message + R"("},"cannotCode":")" + code + R"(")" + extra + "}}");
    return response;
}

// 构造资源下载失败结果。
model::SongResourceContentFetchResult failedContent(std::string category, std::string code)
{
    model::SongResourceContentFetchResult result;
    result.ok = false;
    result.error_category = std::move(category);
    result.result_code = std::move(code);
    result.message = "download failed";
    return result;
}

// 构造资源下载成功结果。
model::SongResourceContentFetchResult okContent(std::string content_type = "audio/flac")
{
    model::SongResourceContentFetchResult result;
    result.ok = true;
    result.status_code = 200;
    result.content_type = std::move(content_type);
    result.body = bytes("ok");
    return result;
}

// 记录 workflow 阶段事件，用于验证批次日志覆盖关键步骤。
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

void testWorkflowStreamsContentIntoBlobStore()
{
    auto queue = std::make_shared<FakeSongResourceQueue>();
    queue->pending = {directTextTarget(701, "https://example.test/lyric")};

    auto fetcher = std::make_shared<StreamingOnlyContentFetcher>();
    auto blob_store = std::make_shared<MemoryBlobStore>();
    use_cases::SongResourceDownloadWorkflow workflow(queue, nullptr, nullptr, fetcher, blob_store);

    model::SongResourceClaimQuery query;
    query.source = "migu";
    query.resource_kind = "lyric";
    query.limit = 1;
    query.batch_id = "workflow-stream-test";
    const auto result = workflow.processPending(query);

    require(result.claimed_count == 1, "one direct resource should be claimed");
    require(result.succeeded_count == 1, "streaming direct resource should succeed");
    require(fetcher->stream_call_count.load() == 1, "workflow should call streamSongResourceContent once");
    require(blob_store->saved.size() == 1, "streamed content should be committed to blob store once");
    const auto &saved = blob_store->saved.front();
    require(std::string(saved.data.begin(), saved.data.end()) == "aabb", "blob store should receive concatenated streamed chunks");
    require(queue->succeeded.size() == 1, "stream success should be written back once");
    require(queue->failed.empty(), "stream success should not create failure writeback");
}

void testWorkflowDownloadsAudioAndSingleLyric()
{
    const auto root = std::filesystem::current_path() / "build" / "song-resource-workflow-test";
    std::filesystem::remove_all(root);

    auto queue = std::make_shared<FakeSongResourceQueue>();
    queue->pending = {audioTarget()};
    queue->related_targets = {relatedTarget(201, "lyric"), relatedTarget(202, "mrc"), relatedTarget(203, "trc")};

    auto provider = std::make_shared<crawler::tests::MockMusicProvider>();
    ports::SongResourceProvider::Response listen_response;
    listen_response.status_code = 200;
    listen_response.body = bytes(R"({
        "code": "000000",
        "info": "操作成功",
        "data": {
            "version": "v1",
            "url": "https://example.test/audio.flac?Key=redacted",
            "audioFormatType": "ZQ24",
            "lrcUrl": "https://example.test/lyric",
            "mrcUrl": "https://example.test/mrc",
            "trcUrl": "https://example.test/trc"
        }
    })");
    provider->mock_song_resource_response = listen_response;

    auto http = std::make_shared<http_client::MockHttpClient>();
    http_client::HttpResponse audio_response;
    audio_response.status_code = 200;
    audio_response.content_type = "audio/flac";
    audio_response.body = bytes("audio-bytes");
    http->mock_responses.emplace("https://example.test/audio.flac?Key=redacted", audio_response);

    http_client::HttpResponse lyric_response;
    lyric_response.status_code = 200;
    lyric_response.content_type = "text/plain";
    lyric_response.body = bytes("[00:00.00] lyric");
    http->mock_responses.emplace("https://example.test/lyric", lyric_response);

    http_client::HttpResponse mrc_response;
    mrc_response.status_code = 200;
    mrc_response.content_type = "application/octet-stream";
    mrc_response.body = bytes("opaque-mrc");
    http->mock_responses.emplace("https://example.test/mrc", mrc_response);

    http_client::HttpResponse trc_response;
    trc_response.status_code = 200;
    trc_response.content_type = "text/plain";
    trc_response.body = bytes("translated lyric");
    http->mock_responses.emplace("https://example.test/trc", trc_response);

    auto parser = std::make_shared<migu_parser::MiguSongResourceParser>();
    auto fetcher = std::make_shared<http_client::HttpSongResourceContentFetcher>(http);
    auto storage = std::make_shared<filesystem_storage::FileStorage>(root);
    use_cases::SongResourceDownloadWorkflow workflow(queue, provider, parser, fetcher, storage);

    model::SongResourceClaimQuery query;
    query.source = "migu";
    query.resource_kind = "audio";
    query.limit = 1;
    query.batch_id = "workflow-test";
    const auto result = workflow.processPending(query);

    require(result.claimed_count == 1, "one audio resource should be claimed");
    require(result.succeeded_count == 1, "audio resource should succeed");
    require(result.failed_count == 0, "audio resource should not fail");
    require(result.related_succeeded_count == 3, "lyric, mrc and trc should be downloaded once");
    require(result.items.size() == 4, "audio, lyric, mrc and trc should be present in process items");
    require(std::count_if(result.items.begin(), result.items.end(), [](const model::SongResourceProcessItem &item) {
                return item.related;
            }) == 3,
            "three related text resources should be present in process items");
    require(std::all_of(result.items.begin(), result.items.end(), [](const model::SongResourceProcessItem &item) {
                return item.ok && item.error_category.empty();
            }),
            "successful process items should not carry error category");
    require(queue->succeeded.size() == 4, "audio, lyric, mrc and trc should be marked succeeded");
    require(queue->failed.empty(), "no resource should be marked failed");
    require(provider->last_song_resource_request.content_id == "600902000006889266", "contentId should be passed to provider");
    require(provider->last_song_resource_request.tone_flag == "ZQ24", "toneFlag should come from format_type");

    require(queue->succeeded[0].local_path == "migu/songs/600902000006889266/audio/ZQ24.flac", "audio local_path should be stable");
    require(queue->succeeded[1].local_path == "migu/songs/600902000006889266/lyrics/lyric.lrc", "lyric local_path should be song-level");
    require(queue->succeeded[2].local_path == "migu/songs/600902000006889266/lyrics/mrc.mrc", "mrc local_path should be song-level");
    require(queue->succeeded[3].local_path == "migu/songs/600902000006889266/lyrics/trc.trc", "trc local_path should be song-level");
    require(std::filesystem::exists(root / queue->succeeded[0].local_path), "audio file should exist");
    require(std::filesystem::exists(root / queue->succeeded[1].local_path), "lyric file should exist");
    require(std::filesystem::exists(root / queue->succeeded[2].local_path), "mrc file should exist");
    require(std::filesystem::exists(root / queue->succeeded[3].local_path), "trc file should exist");

    std::filesystem::remove_all(root);
}

void testWorkflowProcessesRelatedResourcesOncePerSong()
{
    const auto root = std::filesystem::current_path() / "build" / "song-resource-workflow-related-dedup-test";
    std::filesystem::remove_all(root);

    auto first = audioTarget();
    first.resource_id = 301;
    first.format_type = "PQ";
    auto second = audioTarget();
    second.resource_id = 302;
    second.format_type = "HQ";

    auto queue = std::make_shared<FakeSongResourceQueue>();
    queue->pending = {first, second};

    auto provider = std::make_shared<crawler::tests::MockMusicProvider>();
    ports::SongResourceProvider::Response listen_response;
    listen_response.status_code = 200;
    listen_response.body = bytes(R"({
        "code": "000000",
        "info": "操作成功",
        "data": {
            "version": "v1",
            "url": "https://example.test/audio.mp3?Key=redacted",
            "audioFormatType": "HQ",
            "lrcUrl": "https://example.test/lyric",
            "mrcUrl": "https://example.test/mrc"
        }
    })");
    provider->mock_song_resource_response = listen_response;

    auto http = std::make_shared<http_client::MockHttpClient>();
    http_client::HttpResponse audio_response;
    audio_response.status_code = 200;
    audio_response.content_type = "audio/mpeg";
    audio_response.body = bytes("audio-bytes");
    http->mock_responses.emplace("https://example.test/audio.mp3?Key=redacted", audio_response);

    auto parser = std::make_shared<migu_parser::MiguSongResourceParser>();
    auto fetcher = std::make_shared<http_client::HttpSongResourceContentFetcher>(http);
    auto storage = std::make_shared<filesystem_storage::FileStorage>(root);
    use_cases::SongResourceDownloadWorkflow workflow(queue, provider, parser, fetcher, storage);

    model::SongResourceClaimQuery query;
    query.source = "migu";
    query.resource_kind = "audio";
    query.limit = 2;
    query.batch_id = "workflow-related-dedup-test";
    const auto result = workflow.processPending(query);

    require(result.claimed_count == 2, "two audio formats should be claimed");
    require(result.succeeded_count == 2, "both audio formats should succeed");
    require(result.related_skipped_count == 2, "related text resources should be skipped only once per song");
    require(result.items.size() == 4, "result should contain two audio items and two synthetic related items");
    const auto related_count = std::count_if(result.items.begin(), result.items.end(), [](const model::SongResourceProcessItem &item) {
        return item.related;
    });
    require(related_count == 2, "duplicate audio formats should not duplicate related item details");

    std::filesystem::remove_all(root);
}

void testWorkflowHonorsResourceDownloadConcurrency()
{
    auto make_queue = [] {
        auto queue = std::make_shared<FakeSongResourceQueue>();
        queue->pending = {
            directTextTarget(401, "https://example.test/1.lrc"),
            directTextTarget(402, "https://example.test/2.lrc"),
            directTextTarget(403, "https://example.test/3.lrc"),
        };
        return queue;
    };

    auto serial_fetcher = std::make_shared<ScriptedContentFetcher>();
    serial_fetcher->delay = std::chrono::milliseconds(40);
    use_cases::SongResourceDownloadWorkflow serial_workflow(make_queue(), nullptr, nullptr, serial_fetcher, std::make_shared<MemoryBlobStore>(), {}, std::make_shared<ThreadedTaskExecutor>());
    model::SongResourceClaimQuery serial_query;
    serial_query.batch_id = "workflow-concurrency-serial-test";
    serial_query.resource_kind = "lyric";
    serial_query.limit = 3;
    serial_query.options.resource_download_concurrency = 1;
    const auto serial_result = serial_workflow.processPending(serial_query);
    require(serial_result.succeeded_count == 3, "serial direct resources should succeed");
    require(serial_fetcher->max_active.load() == 1, "concurrency=1 should never overlap downloads");

    auto concurrent_fetcher = std::make_shared<ScriptedContentFetcher>();
    concurrent_fetcher->delay = std::chrono::milliseconds(80);
    use_cases::SongResourceDownloadWorkflow concurrent_workflow(make_queue(), nullptr, nullptr, concurrent_fetcher, std::make_shared<MemoryBlobStore>(), {}, std::make_shared<ThreadedTaskExecutor>());
    model::SongResourceClaimQuery concurrent_query;
    concurrent_query.batch_id = "workflow-concurrency-parallel-test";
    concurrent_query.resource_kind = "lyric";
    concurrent_query.limit = 3;
    concurrent_query.options.resource_download_concurrency = 3;
    const auto concurrent_result = concurrent_workflow.processPending(concurrent_query);
    require(concurrent_result.succeeded_count == 3, "concurrent direct resources should succeed");
    require(concurrent_fetcher->max_active.load() >= 2, "concurrency=3 should allow overlapping downloads");
}

void testWorkflowDeduplicatesRelatedResourcesUnderConcurrency()
{
    auto first = audioTarget();
    first.resource_id = 501;
    first.format_type = "PQ";
    auto second = audioTarget();
    second.resource_id = 502;
    second.format_type = "HQ";

    auto queue = std::make_shared<FakeSongResourceQueue>();
    queue->pending = {first, second};
    queue->related_targets = {relatedTarget(601, "lyric"), relatedTarget(602, "mrc"), relatedTarget(603, "trc")};

    auto provider = std::make_shared<StaticSongResourceProvider>();
    provider->response = listenResponse(
        "https://example.test/audio.flac",
        R"(,"lrcUrl":"https://example.test/lyric","mrcUrl":"https://example.test/mrc","trcUrl":"https://example.test/trc")"
    );
    auto fetcher = std::make_shared<ScriptedContentFetcher>();
    auto parser = std::make_shared<migu_parser::MiguSongResourceParser>();
    use_cases::SongResourceDownloadWorkflow workflow(queue, provider, parser, fetcher, std::make_shared<MemoryBlobStore>(), {}, std::make_shared<ThreadedTaskExecutor>());

    model::SongResourceClaimQuery query;
    query.source = "migu";
    query.resource_kind = "audio";
    query.limit = 2;
    query.batch_id = "workflow-related-concurrent-dedup-test";
    query.options.resource_download_concurrency = 2;
    const auto result = workflow.processPending(query);

    require(result.succeeded_count == 2, "both audio formats should succeed under concurrency");
    require(result.related_succeeded_count == 3, "related resources should be claimed and saved once under concurrency");
    require(std::count_if(result.items.begin(), result.items.end(), [](const model::SongResourceProcessItem &item) {
                return item.related && item.ok;
            }) == 3,
            "concurrent audio formats should not duplicate related details");
    require(queue->succeeded.size() == 5, "two audio resources and three related resources should be written back");
}

void testWorkflowReportsRelatedFailureAndMissingClaim()
{
    const auto root = std::filesystem::current_path() / "build" / "song-resource-workflow-related-failure-test";
    std::filesystem::remove_all(root);

    auto queue = std::make_shared<FakeSongResourceQueue>();
    queue->pending = {audioTarget()};
    queue->related_targets = {relatedTarget(201, "lyric")};

    auto provider = std::make_shared<crawler::tests::MockMusicProvider>();
    ports::SongResourceProvider::Response listen_response;
    listen_response.status_code = 200;
    listen_response.body = bytes(R"({
        "code": "000000",
        "info": "操作成功",
        "data": {
            "version": "v1",
            "url": "https://example.test/audio.flac",
            "audioFormatType": "ZQ24",
            "lrcUrl": "https://example.test/lyric",
            "mrcUrl": "https://example.test/mrc"
        }
    })");
    provider->mock_song_resource_response = listen_response;

    auto http = std::make_shared<http_client::MockHttpClient>();
    http_client::HttpResponse audio_response;
    audio_response.status_code = 200;
    audio_response.content_type = "audio/flac";
    audio_response.body = bytes("audio-bytes");
    http->mock_responses.emplace("https://example.test/audio.flac", audio_response);

    auto parser = std::make_shared<migu_parser::MiguSongResourceParser>();
    auto fetcher = std::make_shared<http_client::HttpSongResourceContentFetcher>(http);
    auto storage = std::make_shared<filesystem_storage::FileStorage>(root);
    use_cases::SongResourceDownloadWorkflow workflow(queue, provider, parser, fetcher, storage);

    model::SongResourceClaimQuery query;
    query.source = "migu";
    query.resource_kind = "audio";
    query.limit = 1;
    query.batch_id = "workflow-related-failure-test";
    RecordingResourceListener listener;
    const auto result = workflow.processPending(query, &listener);

    require(result.succeeded_count == 1, "audio should still succeed when related lyric fails");
    require(result.related_failed_count == 1, "claimed lyric download failure should be counted");
    require(result.related_skipped_count == 1, "missing mrc claim should be counted as related skip");
    const auto failed_related = std::find_if(result.items.begin(), result.items.end(), [](const model::SongResourceProcessItem &item) {
        return item.resource_kind == "lyric" && !item.ok && !item.skipped;
    });
    require(failed_related != result.items.end(), "failed lyric should be present in process items");
    require(failed_related->error_category == "network", "failed lyric should expose network error category");
    const auto skipped_related = std::find_if(result.items.begin(), result.items.end(), [](const model::SongResourceProcessItem &item) {
        return item.resource_kind == "mrc" && item.skipped;
    });
    require(skipped_related != result.items.end(), "missing mrc claim should be present in process items");
    require(skipped_related->resource_id == 0, "missing related claim should use synthetic resource id 0");
    require(skipped_related->error_category == "resource", "missing related claim should expose resource error category");
    require(skipped_related->result_code == "related_not_claimed", "missing related claim should expose stable result code");
    require(skipped_related->synthetic, "missing related claim should be marked synthetic");
    require(result.synthetic_related_skipped_count == 1, "missing related claim should increment synthetic related skip count");
    require(listener.hasEvent("relatedResourceClaimStarted"), "workflow should emit related claim started event");
    require(listener.hasEvent("relatedResourceClaimed"), "workflow should emit related claimed event");
    require(listener.hasEvent("relatedResourceNotClaimed"), "workflow should emit related missing claim event");
    require(listener.hasEvent("resourceFailureWritebackCompleted"), "workflow should emit related failure writeback event");

    std::filesystem::remove_all(root);
}

void testWorkflowRetriesNetworkFailureWithFreshListenUrl()
{
    auto queue = std::make_shared<FakeSongResourceQueue>();
    queue->pending = {audioTarget()};

    auto provider = std::make_shared<SequenceSongResourceProvider>();
    provider->responses = {
        listenResponse("https://example.test/expired.flac"),
        listenResponse("https://example.test/fresh.flac"),
    };

    auto fetcher = std::make_shared<ScriptedContentFetcher>();
    fetcher->responses["https://example.test/expired.flac"] = {failedContent("network", "network_failed")};
    fetcher->responses["https://example.test/fresh.flac"] = {okContent()};

    auto parser = std::make_shared<migu_parser::MiguSongResourceParser>();
    use_cases::SongResourceDownloadWorkflow workflow(queue, provider, parser, fetcher, std::make_shared<MemoryBlobStore>());

    model::SongResourceClaimQuery query;
    query.source = "migu";
    query.resource_kind = "audio";
    query.limit = 1;
    query.batch_id = "workflow-retry-fresh-url-test";
    query.options.resource_download_retries = 1;
    RecordingResourceListener listener;
    const auto result = workflow.processPending(query, &listener);

    require(result.succeeded_count == 1, "network retry should succeed after fresh listen URL");
    require(provider->call_count == 2, "audio retry should request listen URL again");
    require(fetcher->requested_urls.size() == 2, "audio retry should download twice");
    require(fetcher->requested_urls[0] == "https://example.test/expired.flac", "first download should use initial URL");
    require(fetcher->requested_urls[1] == "https://example.test/fresh.flac", "retry download should use refreshed listen URL");
    require(queue->failed.empty(), "intermediate retry failure should not be written back");
    require(queue->succeeded.size() == 1, "final retry success should be written back once");
    require(listener.hasEvent("resourceClaimStarted"), "workflow should emit claim started event");
    require(listener.hasEvent("resourceClaimCompleted"), "workflow should emit claim completed event");
    require(listener.hasEvent("resourceAttemptStarted"), "workflow should emit attempt started event");
    require(listener.hasEvent("resourceRetryScheduled"), "workflow should emit retry scheduled event");
    require(listener.hasEvent("resourceBlobCommitStarted"), "workflow should emit blob commit started event");
    require(listener.hasEvent("resourceDbWritebackCompleted"), "workflow should emit success writeback event");
}

void testWorkflowRefreshesPacmtokenOn440013()
{
    auto queue = std::make_shared<FakeSongResourceQueue>();
    queue->pending = {audioTarget()};

    ports::SongResourceProvider::Response restricted;
    restricted.status_code = 200;
    restricted.body = bytes(R"({"code":"000000","info":"操作成功","data":{"dialogInfo":{"text":"需要刷新 pacmtoken"},"cannotCode":"440013"}})");

    auto provider = std::make_shared<SequenceSongResourceProvider>();
    provider->responses = {
        restricted,
        listenResponse("https://example.test/fresh-after-token.flac"),
    };
    auto fetcher = std::make_shared<ScriptedContentFetcher>();
    fetcher->responses["https://example.test/fresh-after-token.flac"] = {okContent()};
    auto parser = std::make_shared<migu_parser::MiguSongResourceParser>();
    auto refresher = std::make_shared<FakePacmtokenRefresher>();
    auto store = std::make_shared<migu_provider::MiguRuntimePacmtokenStore>();
    store->updatePacmtoken("old-token", 1);

    use_cases::SongResourceDownloadWorkflow workflow(queue, provider, parser, fetcher, std::make_shared<MemoryBlobStore>(), {}, nullptr, refresher, store);

    model::SongResourceClaimQuery query;
    query.source = "migu";
    query.resource_kind = "audio";
    query.limit = 1;
    query.batch_id = "workflow-refresh-440013-test";
    query.options.auto_refresh_pacmtoken = true;
    query.options.refresh_pacmtoken_on_440013 = true;
    query.options.resource_download_retries = 0;
    const auto result = workflow.processPending(query);

    require(result.succeeded_count == 1, "440013 should retry after pacmtoken refresh");
    require(provider->call_count == 2, "440013 retry should request listen URL again");
    require(refresher->call_count.load() >= 1, "440013 should refresh pacmtoken");
    require(store->currentPacmtoken().value_or("") == "new-token", "pacmtoken refresh should update runtime store");
    require(queue->failed.empty(), "440013 recovered by refresh should not be written as failure");
}

void testWorkflowSkipsAllAudioFormatsOn440016()
{
    auto queue = std::make_shared<FakeSongResourceQueue>();
    queue->pending = {audioTarget()};

    auto provider = std::make_shared<crawler::tests::MockMusicProvider>();
    provider->mock_song_resource_response = listenRestrictedResponse("440016", "应版权方要求，该数字专辑需要购买后即可畅享");

    auto http = std::make_shared<http_client::MockHttpClient>();
    auto parser = std::make_shared<migu_parser::MiguSongResourceParser>();
    auto fetcher = std::make_shared<http_client::HttpSongResourceContentFetcher>(http);
    auto storage = std::make_shared<filesystem_storage::FileStorage>(std::filesystem::current_path() / "build" / "song-resource-workflow-test");
    use_cases::SongResourceDownloadWorkflow workflow(queue, provider, parser, fetcher, storage);

    model::SongResourceClaimQuery query;
    query.source = "migu";
    query.resource_kind = "audio";
    query.limit = 1;
    query.batch_id = "workflow-skip-test";
    const auto result = workflow.processPending(query);

    require(result.claimed_count == 1, "one digital album resource should be claimed");
    require(result.succeeded_count == 0, "digital album resource should not succeed");
    require(result.failed_count == 0, "digital album resource should not be marked failed");
    require(result.skipped_count == 1, "digital album resource should be counted as skipped");
    require(result.items.size() == 1 && result.items.front().skipped, "digital album item should be skipped");
    require(result.items.front().error_category == "resource", "digital album skip item should expose resource category");
    require(queue->failed.empty(), "digital album should not use failure writeback");
    require(queue->skipped.size() == 1, "digital album should use skip writeback");
    require(queue->skipped.front().song_source_ref_id == 11, "skip should target the song source ref");
    require(queue->skipped.front().resource_kind == "audio", "440016 should only target audio resources");
    require(queue->skipped.front().format_type.empty(), "440016 should mark all audio formats skipped for the song");
    require(queue->skipped.front().result_code == "440016", "skip result code should preserve Migu cannotCode");
}

void testWorkflowDownloadsRelatedTextWhenAudioRestricted()
{
    const struct {
        const char *code;
        const char *message;
    } cases[] = {
        {"440015", "应版权方要求，该数字专辑购买后即可无限畅享"},
        {"440016", "应版权方要求，该数字专辑需要购买后即可畅享"},
    };

    for (const auto &test_case : cases) {
        auto queue = std::make_shared<FakeSongResourceQueue>();
        queue->pending = {audioTarget()};
        queue->related_targets = {relatedTarget(301, "lyric"), relatedTarget(302, "mrc"), relatedTarget(303, "trc")};

        auto provider = std::make_shared<crawler::tests::MockMusicProvider>();
        provider->mock_song_resource_response = listenRestrictedResponse(
            test_case.code,
            test_case.message,
            R"(,"lrcUrl":"https://example.test/lyric","mrcUrl":"https://example.test/mrc","trcUrl":"https://example.test/trc")"
        );

        auto fetcher = std::make_shared<ScriptedContentFetcher>();
        auto parser = std::make_shared<migu_parser::MiguSongResourceParser>();
        use_cases::SongResourceDownloadWorkflow workflow(queue, provider, parser, fetcher, std::make_shared<MemoryBlobStore>());

        model::SongResourceClaimQuery query;
        query.source = "migu";
        query.resource_kind = "audio";
        query.limit = 1;
        query.batch_id = std::string("workflow-restricted-related-test-") + test_case.code;
        const auto result = workflow.processPending(query);

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

void testWorkflowSkipsPermanentListenRestrictions()
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
        auto queue = std::make_shared<FakeSongResourceQueue>();
        queue->pending = {audioTarget()};

        auto provider = std::make_shared<crawler::tests::MockMusicProvider>();
        provider->mock_song_resource_response = listenRestrictedResponse(test_case.code, test_case.message);

        auto http = std::make_shared<http_client::MockHttpClient>();
        auto parser = std::make_shared<migu_parser::MiguSongResourceParser>();
        auto fetcher = std::make_shared<http_client::HttpSongResourceContentFetcher>(http);
        auto storage = std::make_shared<filesystem_storage::FileStorage>(std::filesystem::current_path() / "build" / "song-resource-workflow-test");
        use_cases::SongResourceDownloadWorkflow workflow(queue, provider, parser, fetcher, storage);

        model::SongResourceClaimQuery query;
        query.source = "migu";
        query.resource_kind = "audio";
        query.limit = 1;
        query.batch_id = std::string("workflow-permanent-skip-") + test_case.code;
        const auto result = workflow.processPending(query);

        require(result.failed_count == 0, std::string(test_case.code) + " should not be marked failed");
        require(result.skipped_count == 1, std::string(test_case.code) + " should be counted as skipped");
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

void testWorkflowFailsLoginRestrictedAudioAsRetryableResource()
{
    auto queue = std::make_shared<FakeSongResourceQueue>();
    queue->pending = {audioTarget()};

    auto provider = std::make_shared<crawler::tests::MockMusicProvider>();
    ports::SongResourceProvider::Response listen_response;
    listen_response.status_code = 200;
    listen_response.body = bytes(R"({
        "code": "000000",
        "info": "操作成功",
        "data": {
            "dialogInfo": {
                "text": "版权方要求，该歌曲需开通白金会员后播放"
            },
            "cannotCode": "440013"
        }
    })");
    provider->mock_song_resource_response = listen_response;

    auto http = std::make_shared<http_client::MockHttpClient>();
    auto parser = std::make_shared<migu_parser::MiguSongResourceParser>();
    auto fetcher = std::make_shared<http_client::HttpSongResourceContentFetcher>(http);
    auto storage = std::make_shared<filesystem_storage::FileStorage>(std::filesystem::current_path() / "build" / "song-resource-workflow-test");
    use_cases::SongResourceDownloadWorkflow workflow(queue, provider, parser, fetcher, storage);

    model::SongResourceClaimQuery query;
    query.source = "migu";
    query.resource_kind = "audio";
    query.limit = 1;
    query.batch_id = "workflow-login-restricted-test";
    const auto result = workflow.processPending(query);

    require(result.claimed_count == 1, "one login restricted resource should be claimed");
    require(result.failed_count == 1, "login restricted resource should remain retryable failed");
    require(result.skipped_count == 0, "login restricted resource should not be counted as skipped");
    require(result.items.size() == 1 && !result.items.front().skipped, "login restricted item should not be skipped");
    require(result.items.front().error_category == "resource", "login restricted failure should expose resource category");
    require(result.items.front().result_code == "440013", "failure result code should preserve Migu cannotCode");
    require(queue->failed.size() == 1, "login restricted resource should use failure writeback");
    require(queue->skipped.empty(), "login restricted resource should not use skip writeback");
    require(queue->failed.front().result_code == "440013", "failure writeback should preserve Migu cannotCode");
}

void testWorkflowSkipsKnownDigitalAlbumBeforeFetch()
{
    auto queue = std::make_shared<FakeSongResourceQueue>();
    auto target = audioTarget();
    target.source_album_kind = 2;
    queue->pending = {target};

    auto provider = std::make_shared<crawler::tests::MockMusicProvider>();
    auto http = std::make_shared<http_client::MockHttpClient>();
    auto parser = std::make_shared<migu_parser::MiguSongResourceParser>();
    auto fetcher = std::make_shared<http_client::HttpSongResourceContentFetcher>(http);
    auto storage = std::make_shared<filesystem_storage::FileStorage>(std::filesystem::current_path() / "build" / "song-resource-workflow-test");
    use_cases::SongResourceDownloadWorkflow workflow(queue, provider, parser, fetcher, storage);

    model::SongResourceClaimQuery query;
    query.source = "migu";
    query.resource_kind = "audio";
    query.limit = 1;
    query.batch_id = "workflow-known-digital-skip-test";
    RecordingResourceListener listener;
    const auto result = workflow.processPending(query, &listener);

    require(result.claimed_count == 1, "one known digital album resource should be claimed");
    require(result.succeeded_count == 0, "known digital album resource should not succeed");
    require(result.failed_count == 0, "known digital album resource should not fail");
    require(result.skipped_count == 1, "known digital album resource should be skipped");
    require(result.items.size() == 1 && result.items.front().error_category == "resource", "known digital album skip item should expose resource category");
    require(provider->last_song_resource_request.content_id.empty(), "known digital album should not call listen provider");
    require(queue->skipped.size() == 1, "known digital album should use skip writeback");
    require(queue->skipped.front().song_source_ref_id == 11, "known digital album skip should target song source ref");
    require(queue->skipped.front().resource_kind == "audio", "known digital album skip should only target audio");
    require(queue->skipped.front().format_type.empty(), "known digital album skip should cover all audio formats");
    require(queue->skipped.front().result_code == "digital_album", "known digital album skip should use stable local result code");
    require(listener.hasEvent("resourceSkipWritebackCompleted"), "known digital album should emit skip writeback event");
}

void testWorkflowDoesNotWriteCompletedWhenBlobValidationFails()
{
    auto queue = std::make_shared<FakeSongResourceQueue>();
    queue->pending = {audioTarget()};

    auto provider = std::make_shared<StaticSongResourceProvider>();
    provider->response = listenResponse("https://example.test/audio.flac");
    auto parser = std::make_shared<migu_parser::MiguSongResourceParser>();
    auto fetcher = std::make_shared<ScriptedContentFetcher>();
    fetcher->responses["https://example.test/audio.flac"] = {okContent()};

    use_cases::SongResourceDownloadWorkflow workflow(queue, provider, parser, fetcher, std::make_shared<InvalidBlobStore>());

    model::SongResourceClaimQuery query;
    query.source = "migu";
    query.resource_kind = "audio";
    query.limit = 1;
    query.batch_id = "workflow-blob-validation-test";
    const auto result = workflow.processPending(query);

    require(result.claimed_count == 1, "one resource should be claimed");
    require(result.succeeded_count == 0, "invalid blob save result must not count as success");
    require(result.failed_count == 1, "invalid blob save result should fail");
    require(result.items.size() == 1, "validation failure should return one item");
    require(result.items.front().error_category == "persistence", "validation failure should be persistence category");
    require(result.items.front().result_code == "persist_validation_failed", "validation failure should use stable result code");
    require(queue->succeeded.empty(), "invalid blob save result must not mark DB completed");
    require(queue->failed.size() == 1, "invalid blob save result should mark resource failed");
    require(queue->failed.front().result_code == "persist_validation_failed", "failure writeback should preserve validation result code");
}

} // namespace

int main()
{
    try {
        testWorkflowStreamsContentIntoBlobStore();
        testWorkflowDownloadsAudioAndSingleLyric();
        testWorkflowProcessesRelatedResourcesOncePerSong();
        testWorkflowHonorsResourceDownloadConcurrency();
        testWorkflowDeduplicatesRelatedResourcesUnderConcurrency();
        testWorkflowReportsRelatedFailureAndMissingClaim();
        testWorkflowRetriesNetworkFailureWithFreshListenUrl();
        testWorkflowRefreshesPacmtokenOn440013();
        testWorkflowSkipsAllAudioFormatsOn440016();
        testWorkflowDownloadsRelatedTextWhenAudioRestricted();
        testWorkflowSkipsPermanentListenRestrictions();
        testWorkflowFailsLoginRestrictedAudioAsRetryableResource();
        testWorkflowSkipsKnownDigitalAlbumBeforeFetch();
        testWorkflowDoesNotWriteCompletedWhenBlobValidationFails();
        std::cout << "SongResourceDownloadWorkflowTest passed." << std::endl;
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "SongResourceDownloadWorkflowTest failed: " << e.what() << std::endl;
        return 1;
    }
}
