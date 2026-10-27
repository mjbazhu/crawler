#include "crawler/application/use_cases/SongResourceDownloadWorkflow.h"
#include "crawler/infrastructure/storage/filesystem/FileStorage.h"

#include <chrono>
#include <filesystem>
#include <iostream>
#include <stdexcept>

namespace crawler = orchestrator::modules::crawler;
namespace model = crawler::application::model;
namespace ports = crawler::application::ports;
namespace cases = crawler::application::use_cases;
namespace storage = crawler::infrastructure::storage::filesystem;

// 所有检查在 Debug/Release 下都生效。
void require(bool value, const char *message)
{
    if (!value) throw std::runtime_error(message);
}

// 单线程测试队列，验证成功前文件可见、旧文件仍保留和回写代次。
class ReceiptQueue final : public ports::SongResourceQueue {
public:
    std::shared_ptr<storage::FileStorage> files; // 被核对的真实文件 adapter
    model::SongResourceQueueTarget target;       // 单次认领目标
    int failures = 0;                            // 失败回写次数
    int commits = 0;                             // 成功回写次数
    bool reject = false;                         // 模拟过期认领
    bool uncertain = false;                      // 模拟断线后仍无法确认

    // 只认领一次，输入目标已经包含稳定来源与认领代次。
    std::vector<model::SongResourceQueueTarget> claimPending(model::SongResourceClaimQuery) override
    {
        return {target};
    }
    // 本测试不合成关联资源。
    std::optional<model::SongResourceQueueTarget> claimRelated(model::SongResourceRelatedClaimRequest) override
    {
        return std::nullopt;
    }
    // 数据库确认时必须已经存在完整新文件，而且旧文件还没被删。
    bool markSucceeded(model::SongResourceSuccessUpdate update) override
    {
        require(update.claim_generation == target.claim_generation, "writeback lost claim generation");
        require(files->exists(update.local_path), "success preceded committed file");
        require(files->exists(target.local_path), "old file deleted before DB commit");
        ++commits;
        return !reject;
    }
    // 不确定提交通过既有结果类型表达，不得当成明确拒绝删除文件。
    crawler::application::result::ServiceResult<bool> confirmSucceeded(model::SongResourceSuccessUpdate update) override
    {
        using Result = crawler::application::result::ServiceResult<bool>;
        if (uncertain) return Result::failure(crawler::application::result::ServiceErrorType::InternalError, "fixture disconnect");
        return Result::success(markSucceeded(std::move(update)));
    }
    // 正常失败应释放本代次认领。
    bool markFailed(model::SongResourceFailureUpdate update) override
    {
        require(update.claim_generation == target.claim_generation, "failure lost claim generation");
        ++failures;
        return true;
    }
    // 本测试不触发永久限制。
    bool markSkipped(model::SongResourceSkipUpdate) override
    {
        return true;
    }
};

// 流式 fixture，覆盖流中异常和存储字节数不匹配。
class Content final : public ports::SongResourceContentFetcher {
public:
    bool throw_after_chunk = false; // 模拟下载过程中异常
    bool invalid_type = false;      // HTTP 200 错误页不能保存为资源
    bool wrong_size = false;        // 模拟上游统计与实际内容不符
    int calls = 0;                  // 是否真正开始下载

    // 防止兼容入口退回整文件内存聚合。
    model::SongResourceContentFetchResult fetchSongResourceContent(model::SongResourceContentFetchRequest) override
    {
        throw std::runtime_error("buffered download must not be used");
    }
    // 真实调用 FileStorage 会话，不能用内存替身掩盖补偿问题。
    model::SongResourceContentStreamResult streamSongResourceContent(model::SongResourceContentFetchRequest, model::SongResourceContentStreamCallbacks callbacks) override
    {
        ++calls;
        model::SongResourceContentStreamResult result;
        result.content_type = "text/plain";
        if (!callbacks.on_response({200, invalid_type ? "application/json" : "text/plain"}) || !callbacks.on_chunk("new bytes")) {
            result.result_code = "stream_consumer_failed";
            return result;
        }
        if (throw_after_chunk) throw std::runtime_error("fixture stream interrupted");
        result.ok = true;
        result.bytes_received = wrong_size ? 99 : 9;
        return result;
    }
};

// 取消发生在下载前，已认领的目标仍需准确结束。
class Cancelled final : public cases::SongResourceProcessListener {
public:
    bool isCancellationRequested() override
    {
        return true;
    }
    void onItemStarted(const model::SongResourceProcessItem &) override
    {}
    void onItemFinished(const model::SongResourceProcessItem &) override
    {}
};

// 统计目录内文件，包含文件日志和临时分片。
std::size_t countFiles(const std::filesystem::path &root)
{
    std::size_t count = 0;
    for (const auto &entry : std::filesystem::recursive_directory_iterator(root))
        if (entry.is_regular_file()) ++count;
    return count;
}

int main()
{
    try {
        const auto root = std::filesystem::temp_directory_path() / ("crawler_resource_recovery_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
        auto files = std::make_shared<storage::FileStorage>(root);
        auto queue = std::make_shared<ReceiptQueue>();
        queue->files = files;
        queue->target.resource_id = 41;
        queue->target.song_source_ref_id = 7;
        queue->target.claim_generation = 9;
        queue->target.source_content_id = "fixture-content";
        queue->target.resource_kind = "lyric";
        queue->target.remote_url = "https://example.test/lyric.lrc";
        queue->target.local_path = "migu/songs/fixture-content/lyrics/old.lrc";
        require(files->save(queue->target.local_path, {'o', 'l', 'd'}), "old file fixture failed");
        auto fetcher = std::make_shared<Content>();
        cases::SongResourceDownloadWorkflow workflow(queue, nullptr, nullptr, fetcher, files);
        model::SongResourceClaimQuery query;
        query.options.auto_refresh_pacmtoken = false;
        query.options.resource_download_retries = 0;

        queue->reject = true;
        auto rejected = workflow.processPending(query);
        require(rejected.failed_count == 1 && queue->failures == 1, "rejected writeback was reported as success");
        require(countFiles(root) == 1 && files->exists(queue->target.local_path), "rejected writeback left an orphan or deleted old file");

        fetcher->throw_after_chunk = true;
        auto interrupted = workflow.processPending(query);
        require(interrupted.failed_count == 1 && queue->failures == 2, "stream exception left the claim running");
        require(countFiles(root) == 1, "stream exception leaked staging or journal");
        fetcher->throw_after_chunk = false;
        fetcher->wrong_size = true;
        require(workflow.processPending(query).failed_count == 1, "size mismatch became success");
        require(countFiles(root) == 1, "size mismatch left committed bytes");
        fetcher->wrong_size = false;
        fetcher->invalid_type = true;
        require(workflow.processPending(query).items.front().result_code == "invalid_resource_content_type", "HTTP 200 error page became a resource");
        require(countFiles(root) == 1, "invalid resource content left bytes");
        fetcher->invalid_type = false;

        Cancelled listener;
        const auto before = fetcher->calls;
        require(workflow.processPending(query, &listener).items.front().result_code == "cancelled", "cancellation did not release the target");
        require(fetcher->calls == before && countFiles(root) == 1, "cancelled target downloaded bytes");

        queue->uncertain = true;
        auto unknown = workflow.processPending(query);
        require(unknown.items.front().result_code == "writeback_uncertain", "uncertain commit was treated as a rejection");
        require(countFiles(root) == 3, "uncertain commit lost its object or journal");
        require(files->recoverResourceWrites([](auto, auto, const auto &) { return std::optional<model::ResourceWriteDisposition> {}; }) == 0, "offline DB deleted uncertain file");
        require(files->recoverResourceWrites([](auto, auto, const auto &) { return model::ResourceWriteDisposition::Active; }) == 0, "active write was reclaimed");
        require(files->recoverResourceWrites([](auto, auto, const auto &) { return model::ResourceWriteDisposition::Discard; }) == 1, "expired orphan was not reclaimed");
        require(countFiles(root) == 1, "orphan recovery left file or journal");

        queue->uncertain = false;
        queue->reject = false;
        auto success = workflow.processPending(query);
        require(success.succeeded_count == 1 && files->exists(success.items.front().local_path), "valid write did not persist");
        require(!files->exists(queue->target.local_path) && countFiles(root) == 1, "successful replacement left old file or pending journal");
        std::filesystem::remove_all(root);
        std::cout << "PASS: DB rejection, stream exception, validation, cancellation, uncertain commit, journal recovery, replacement\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
