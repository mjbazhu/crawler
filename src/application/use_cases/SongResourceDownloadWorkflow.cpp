#include "crawler/application/use_cases/SongResourceDownloadWorkflow.h"

#include "crawler/application/use_cases/AsyncResourcePortAdapters.h"
#include "crawler/application/use_cases/AsyncSongResourceDownloadWorkflow.h"

#include <exception>
#include <span>
#include <utility>

namespace orchestrator::modules::crawler::application::use_cases {
namespace {

// 同步调用线程运行短续体；只有下载交给已有受限 executor，不创建新线程池。
class CompatibilityResourceRuntime final : public application::ports::AsyncRuntime {
public:
    // 同步适配器不持有队列，实例可由多个批次共享。
    bool spawn(application::async::Task<void> task) override
    {
        task.startDetached();
        return true;
    }

protected:
    // 同步 port 本来就在兼容工作线程上，不再重复投递造成单线程池死锁。
    void postBlockingCallback(std::function<void()> task) override
    {
        task();
    }
};

// 把同步流式下载桥接到唯一编排，逐块交付，不聚合整首音频到内存。
class CompatibilityStreamFetcher final : public application::ports::AsyncSongResourceContentFetcher {
public:
    // executor 只调度下载；回调续体内联完成，避免等待同一个 worker pool。
    CompatibilityStreamFetcher(std::shared_ptr<application::ports::SongResourceContentFetcher> fetcher, std::shared_ptr<application::ports::TaskExecutor> executor, std::shared_ptr<application::ports::AsyncRuntime> runtime) :
        fetcher_(std::move(fetcher)), executor_(std::move(executor)), runtime_(std::move(runtime))
    {}

    // 每条下载拥有自己的 callbacks 和完成源，跨线程只共享不可变依赖。
    application::async::Task<application::model::SongResourceContentStreamResult> streamSongResourceContent(
        application::model::SongResourceContentFetchRequest request,
        application::ports::AsyncSongResourceContentStreamCallbacks callbacks
    ) override
    {
        application::async::TaskCompletionSource<application::model::SongResourceContentStreamResult> done;
        auto waiting = done.task();
        auto work = [fetcher = fetcher_, runtime = runtime_, request = std::move(request), callbacks = std::move(callbacks), done]() mutable {
            try {
                application::model::SongResourceContentStreamCallbacks sync;
                sync.on_response = [&](const application::model::SongResourceContentStreamInfo &info) {
                    return !callbacks.on_response || runtime->runForTest(callbacks.on_response(info));
                };
                sync.on_chunk = [&](std::string_view bytes) {
                    return !callbacks.on_chunk || runtime->runForTest(callbacks.on_chunk(std::span<const char>(bytes.data(), bytes.size())));
                };
                done.set_value(fetcher->streamSongResourceContent(std::move(request), std::move(sync)));
            } catch (...) {
                done.set_exception(std::current_exception());
            }
        };
        if (executor_) {
            try {
                executor_->submit(std::move(work));
            } catch (...) {
                done.set_exception(std::current_exception());
            }
        } else {
            work();
        }
        co_return co_await waiting;
    }

private:
    std::shared_ptr<application::ports::SongResourceContentFetcher> fetcher_; // 同步流式 port
    std::shared_ptr<application::ports::TaskExecutor> executor_;              // 现有有界下载执行器
    std::shared_ptr<application::ports::AsyncRuntime> runtime_;               // 内联回调续体
};

} // namespace

SongResourceDownloadWorkflow::SongResourceDownloadWorkflow(
    std::shared_ptr<application::ports::SongResourceQueue> resource_queue,
    std::shared_ptr<application::ports::SongResourceProvider> resource_provider,
    std::shared_ptr<application::ports::SongResourceParser> resource_parser,
    std::shared_ptr<application::ports::SongResourceContentFetcher> content_fetcher,
    std::shared_ptr<application::ports::SongResourceBlobStore> blob_store,
    SongResourcePathPlanner path_planner,
    std::shared_ptr<application::ports::TaskExecutor> resource_executor,
    std::shared_ptr<application::ports::MiguPacmtokenRefresher> pacmtoken_refresher,
    std::shared_ptr<application::ports::MiguPacmtokenStore> pacmtoken_store,
    std::shared_ptr<application::ports::SongResourceCloudUploader> cloud_uploader,
    application::ports::ResourceProcessingExtensions extensions
) :
    runtime_(std::make_shared<CompatibilityResourceRuntime>())
{
    workflow_ = std::make_shared<AsyncSongResourceDownloadWorkflow>(
        runtime_,
        resource_queue ? std::make_shared<BlockingAsyncSongResourceQueue>(resource_queue, runtime_) : nullptr,
        resource_provider ? std::make_shared<BlockingAsyncSongResourceProvider>(resource_provider, runtime_) : nullptr,
        std::move(resource_parser),
        content_fetcher ? std::make_shared<CompatibilityStreamFetcher>(content_fetcher, resource_executor, runtime_) : nullptr,
        blob_store ? std::make_shared<BlockingAsyncSongResourceBlobStore>(blob_store, runtime_) : nullptr,
        std::move(path_planner),
        std::move(pacmtoken_refresher),
        std::move(pacmtoken_store),
        nullptr,
        cloud_uploader ? std::make_shared<BlockingAsyncSongResourceCloudUploader>(cloud_uploader, runtime_) : nullptr,
        nullptr,
        std::move(extensions)
    );
}

application::model::SongResourceProcessResult SongResourceDownloadWorkflow::processPending(application::model::SongResourceClaimQuery query, SongResourceProcessListener *listener)
{
    return runtime_->runForTest(workflow_->processPending(std::move(query), listener));
}

application::model::SongResourceProcessResult SongResourceDownloadWorkflow::processAvailableRelated(application::model::SongResourceQueueTarget song, application::model::SongResourceFetchResult resources)
{
    return runtime_->runForTest(workflow_->processAvailableRelated(std::move(song), std::move(resources)));
}

application::model::SongResourceProcessItem SongResourceDownloadWorkflow::processClaimed(application::model::SongResourceQueueTarget target, std::string batch_id)
{
    return runtime_->runForTest(workflow_->processClaimed(std::move(target), std::move(batch_id)));
}

application::model::SongResourceProcessItem SongResourceDownloadWorkflow::processClaimed(application::model::SongResourceQueueTarget target, std::string batch_id, application::model::SongResourceProcessOptions options)
{
    return runtime_->runForTest(workflow_->processClaimed(std::move(target), std::move(batch_id), std::move(options)));
}

} // namespace orchestrator::modules::crawler::application::use_cases
