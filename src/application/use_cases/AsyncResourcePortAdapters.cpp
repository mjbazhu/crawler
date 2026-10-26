#include "crawler/application/use_cases/AsyncResourcePortAdapters.h"

#include <span>
#include <string>
#include <mutex>
#include <utility>
#include <vector>

namespace orchestrator::modules::crawler::application::use_cases {

BlockingAsyncSongResourceQueue::BlockingAsyncSongResourceQueue(std::shared_ptr<application::ports::SongResourceQueue> queue, std::shared_ptr<application::ports::AsyncRuntime> runtime) : queue_(std::move(queue)),
                                                                                                                                               runtime_(std::move(runtime))
{}

application::async::Task<std::vector<application::model::SongResourceQueueTarget>> BlockingAsyncSongResourceQueue::claimPending(application::model::SongResourceClaimQuery query)
{
    if (!queue_ || !runtime_) {
        co_return std::vector<application::model::SongResourceQueueTarget> {};
    }
    co_return co_await runtime_->postBlocking<std::vector<application::model::SongResourceQueueTarget>>([queue = queue_, query = std::move(query)]() mutable {
        return queue->claimPending(std::move(query));
    });
}

application::async::Task<std::optional<application::model::SongResourceQueueTarget>> BlockingAsyncSongResourceQueue::claimRelated(application::model::SongResourceRelatedClaimRequest request)
{
    if (!queue_ || !runtime_) {
        co_return std::nullopt;
    }
    co_return co_await runtime_->postBlocking<std::optional<application::model::SongResourceQueueTarget>>([queue = queue_, request = std::move(request)]() mutable {
        return queue->claimRelated(std::move(request));
    });
}

application::async::Task<bool> BlockingAsyncSongResourceQueue::markSucceeded(application::model::SongResourceSuccessUpdate update)
{
    if (!queue_ || !runtime_) {
        co_return false;
    }
    co_return co_await runtime_->postBlocking<bool>([queue = queue_, update = std::move(update)]() mutable {
        return queue->markSucceeded(std::move(update));
    });
}

application::async::Task<bool> BlockingAsyncSongResourceQueue::markFailed(application::model::SongResourceFailureUpdate update)
{
    if (!queue_ || !runtime_) {
        co_return false;
    }
    co_return co_await runtime_->postBlocking<bool>([queue = queue_, update = std::move(update)]() mutable {
        return queue->markFailed(std::move(update));
    });
}

application::async::Task<bool> BlockingAsyncSongResourceQueue::markSkipped(application::model::SongResourceSkipUpdate update)
{
    if (!queue_ || !runtime_) {
        co_return false;
    }
    co_return co_await runtime_->postBlocking<bool>([queue = queue_, update = std::move(update)]() mutable {
        return queue->markSkipped(std::move(update));
    });
}

BlockingAsyncSongResourceProvider::BlockingAsyncSongResourceProvider(std::shared_ptr<application::ports::SongResourceProvider> provider, std::shared_ptr<application::ports::AsyncRuntime> runtime) : provider_(std::move(provider)),
                                                                                                                                                                                                 runtime_(std::move(runtime))
{}

application::async::Task<std::optional<BlockingAsyncSongResourceProvider::Response>> BlockingAsyncSongResourceProvider::fetchSongResource(application::model::SongResourceFetchRequest request)
{
    if (!provider_ || !runtime_) {
        co_return std::nullopt;
    }
    co_return co_await runtime_->postBlocking<std::optional<Response>>([provider = provider_, request = std::move(request)]() mutable {
        return provider->fetchSongResource(std::move(request));
    });
}

BlockingAsyncSongResourceContentFetcher::BlockingAsyncSongResourceContentFetcher(std::shared_ptr<application::ports::SongResourceContentFetcher> fetcher, std::shared_ptr<application::ports::AsyncRuntime> runtime) : fetcher_(std::move(fetcher)),
                                                                                                                                                                                                             runtime_(std::move(runtime))
{}

application::async::Task<application::model::SongResourceContentStreamResult> BlockingAsyncSongResourceContentFetcher::streamSongResourceContent(
    application::model::SongResourceContentFetchRequest request,
    application::ports::AsyncSongResourceContentStreamCallbacks callbacks
)
{
    application::model::SongResourceContentStreamResult result;
    if (!fetcher_ || !runtime_) {
        result.error_category = "invalid_target";
        result.result_code = "missing_dependency";
        result.message = "async content fetcher dependency is not configured";
        co_return result;
    }

    auto fetched = co_await runtime_->postBlocking<application::model::SongResourceContentFetchResult>([fetcher = fetcher_, request = std::move(request)]() mutable {
        return fetcher->fetchSongResourceContent(std::move(request));
    });
    result.status_code = fetched.status_code;
    result.content_type = fetched.content_type;
    if (!fetched.ok) {
        result.error_category = std::move(fetched.error_category);
        result.result_code = std::move(fetched.result_code);
        result.message = std::move(fetched.message);
        co_return result;
    }

    application::model::SongResourceContentStreamInfo info;
    info.status_code = fetched.status_code;
    info.content_type = fetched.content_type;
    if (callbacks.on_response && !(co_await callbacks.on_response(info))) {
        result.error_category = "persistence";
        result.result_code = "stream_consumer_failed";
        result.message = "resource stream response was rejected by downstream writer";
        co_return result;
    }
    if (!fetched.body.empty() && callbacks.on_chunk) {
        std::span<const char> chunk(fetched.body.data(), fetched.body.size());
        if (!(co_await callbacks.on_chunk(chunk))) {
            result.error_category = "persistence";
            result.result_code = "stream_consumer_failed";
            result.message = "resource stream chunk write failed";
            co_return result;
        }
    }

    result.ok = true;
    result.bytes_received = static_cast<std::uint64_t>(fetched.body.size());
    result.result_code = "ok";
    co_return result;
}

class BlockingAsyncSongResourceBlobStore::Session : public application::ports::AsyncBlobWriteSession {
public:
    struct SharedSession {
        explicit SharedSession(std::unique_ptr<application::ports::SongResourceBlobWriteSession> session) : session_(std::move(session))
        {}

        std::mutex mutex_;                                                          // 保护同步写入会话
        std::unique_ptr<application::ports::SongResourceBlobWriteSession> session_; // 同步写入会话
    };

    Session(std::unique_ptr<application::ports::SongResourceBlobWriteSession> session, std::shared_ptr<application::ports::AsyncRuntime> runtime) : shared_(std::make_shared<SharedSession>(std::move(session))),
                                                                                                                                                     runtime_(std::move(runtime))
    {}

    application::async::Task<application::ports::AsyncBlobWriteResult> write(std::span<const char> chunk) override
    {
        application::ports::AsyncBlobWriteResult result;
        if (!shared_ || !runtime_) {
            result.error = "blob_write_session_closed";
            co_return result;
        }
        std::string bytes(chunk.data(), chunk.data() + chunk.size());
        co_return co_await runtime_->postBlocking<application::ports::AsyncBlobWriteResult>([shared = shared_, bytes = std::move(bytes)]() mutable {
            application::ports::AsyncBlobWriteResult write_result;
            std::lock_guard<std::mutex> lock(shared->mutex_);
            if (!shared->session_) {
                write_result.error = "blob_write_session_closed";
                return write_result;
            }
            std::string error;
            write_result.ok = shared->session_->write(std::string_view(bytes.data(), bytes.size()), error);
            write_result.error = std::move(error);
            return write_result;
        });
    }

    application::async::Task<application::model::SongResourceBlobSaveResult> commit() override
    {
        if (!shared_ || !runtime_) {
            application::model::SongResourceBlobSaveResult result;
            result.error = "blob_write_session_closed";
            co_return result;
        }
        co_return co_await runtime_->postBlocking<application::model::SongResourceBlobSaveResult>([shared = shared_]() {
            std::lock_guard<std::mutex> lock(shared->mutex_);
            if (!shared->session_) {
                application::model::SongResourceBlobSaveResult result;
                result.error = "blob_write_session_closed";
                return result;
            }
            return shared->session_->commit();
        });
    }

    application::async::Task<void> abort() override
    {
        if (!shared_ || !runtime_) {
            co_return;
        }
        co_await runtime_->postBlocking<void>([shared = shared_]() {
            std::lock_guard<std::mutex> lock(shared->mutex_);
            if (shared->session_) {
                shared->session_->abort();
            }
        });
    }

private:
    std::shared_ptr<SharedSession> shared_;                     // 跨 blocking 任务持有同步会话
    std::shared_ptr<application::ports::AsyncRuntime> runtime_; // 阻塞调用隔离 runtime
};

BlockingAsyncSongResourceBlobStore::BlockingAsyncSongResourceBlobStore(std::shared_ptr<application::ports::SongResourceBlobStore> store, std::shared_ptr<application::ports::AsyncRuntime> runtime) : store_(std::move(store)),
                                                                                                                                   runtime_(std::move(runtime))
{}

application::async::Task<std::unique_ptr<application::ports::AsyncBlobWriteSession>> BlockingAsyncSongResourceBlobStore::open(application::model::SongResourceBlobStreamSaveRequest request)
{
    if (!store_ || !runtime_) {
        co_return nullptr;
    }
    auto session = co_await runtime_->postBlocking<std::unique_ptr<application::ports::SongResourceBlobWriteSession>>([store = store_, request = std::move(request)]() mutable {
        return store->beginSongResourceBlobWrite(std::move(request));
    });
    if (!session) {
        co_return nullptr;
    }
    co_return std::make_unique<Session>(std::move(session), runtime_);
}

BlockingAsyncSongResourceCloudUploader::BlockingAsyncSongResourceCloudUploader(std::shared_ptr<application::ports::SongResourceCloudUploader> uploader, std::shared_ptr<application::ports::AsyncRuntime> runtime) : uploader_(std::move(uploader)),
                                                                                                                                                                                                         runtime_(std::move(runtime))
{}

application::async::Task<application::model::SongResourceCloudUploadResult> BlockingAsyncSongResourceCloudUploader::upload(application::model::SongResourceCloudUploadRequest request)
{
    application::model::SongResourceCloudUploadResult result;
    if (!uploader_ || !runtime_) {
        result.error_category = "invalid_target";
        result.result_code = "missing_dependency";
        result.message = "cloud uploader dependency is not configured";
        co_return result;
    }
    co_return co_await runtime_->postBlocking<application::model::SongResourceCloudUploadResult>([uploader = uploader_, request = std::move(request)]() mutable {
        return uploader->upload(std::move(request));
    });
}

BlockingAsyncSongResourceBatchCloudUploader::BlockingAsyncSongResourceBatchCloudUploader(std::shared_ptr<application::ports::SongResourceBatchCloudUploader> uploader, std::shared_ptr<application::ports::AsyncRuntime> runtime) : uploader_(std::move(uploader)),
                                                                                                                                                                                                                         runtime_(std::move(runtime))
{}

application::async::Task<application::model::SongResourceBatchCloudUploadResult> BlockingAsyncSongResourceBatchCloudUploader::uploadBatch(application::model::SongResourceBatchCloudUploadRequest request)
{
    application::model::SongResourceBatchCloudUploadResult result;
    if (!uploader_ || !runtime_) {
        result.error_category = "invalid_target";
        result.result_code = "missing_dependency";
        result.message = "batch cloud uploader dependency is not configured";
        co_return result;
    }
    co_return co_await runtime_->postBlocking<application::model::SongResourceBatchCloudUploadResult>([uploader = uploader_, request = std::move(request)]() mutable {
        return uploader->uploadBatch(std::move(request));
    });
}

} // namespace orchestrator::modules::crawler::application::use_cases
