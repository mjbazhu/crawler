#pragma once

#include "crawler/application/ports/execution/AsyncRuntime.h"
#include "crawler/application/ports/persistence/AsyncSongResourceQueue.h"
#include "crawler/application/ports/persistence/SongResourceQueue.h"
#include "crawler/application/ports/provider/AsyncSongResourceContentFetcher.h"
#include "crawler/application/ports/provider/AsyncSongResourceProvider.h"
#include "crawler/application/ports/provider/SongResourceContentFetcher.h"
#include "crawler/application/ports/provider/SongResourceProvider.h"
#include "crawler/application/ports/storage/AsyncSongResourceBlobStore.h"
#include "crawler/application/ports/storage/AsyncSongResourceBatchCloudUploader.h"
#include "crawler/application/ports/storage/AsyncSongResourceCloudUploader.h"
#include "crawler/application/ports/storage/SongResourceBlobStore.h"
#include "crawler/application/ports/storage/SongResourceBatchCloudUploader.h"
#include "crawler/application/ports/storage/SongResourceCloudUploader.h"

#include <functional>
#include <memory>
#include <optional>
#include <vector>

namespace orchestrator::modules::crawler::application::use_cases {

// 把同步资源队列包装成异步队列，阻塞调用投递给 AsyncRuntime。
class BlockingAsyncSongResourceQueue : public application::ports::AsyncSongResourceQueue {
public:
    BlockingAsyncSongResourceQueue(std::shared_ptr<application::ports::SongResourceQueue> queue, std::shared_ptr<application::ports::AsyncRuntime> runtime);

    application::async::Task<std::vector<application::model::SongResourceQueueTarget>> claimPending(application::model::SongResourceClaimQuery query) override;
    application::async::Task<std::optional<application::model::SongResourceQueueTarget>> claimRelated(application::model::SongResourceRelatedClaimRequest request) override;
    application::async::Task<bool> markSucceeded(application::model::SongResourceSuccessUpdate update) override;
    // 保留底层仓储对不确定提交的区分，不能退化为 bool。
    application::async::Task<application::result::ServiceResult<bool>> confirmSucceeded(application::model::SongResourceSuccessUpdate update) override;
    application::async::Task<bool> markFailed(application::model::SongResourceFailureUpdate update) override;
    application::async::Task<bool> markSkipped(application::model::SongResourceSkipUpdate update) override;

private:
    std::shared_ptr<application::ports::SongResourceQueue> queue_; // 同步队列 port
    std::shared_ptr<application::ports::AsyncRuntime> runtime_;    // 阻塞调用隔离 runtime
};

// 把同步资源 provider 包装成异步 provider，作为测试和兼容 fallback。
class BlockingAsyncSongResourceProvider : public application::ports::AsyncSongResourceProvider {
public:
    BlockingAsyncSongResourceProvider(std::shared_ptr<application::ports::SongResourceProvider> provider, std::shared_ptr<application::ports::AsyncRuntime> runtime);

    application::async::Task<std::optional<Response>> fetchSongResource(application::model::SongResourceFetchRequest request) override;

private:
    std::shared_ptr<application::ports::SongResourceProvider> provider_; // 同步 provider
    std::shared_ptr<application::ports::AsyncRuntime> runtime_;          // 阻塞调用隔离 runtime
};

// 把同步内容下载器包装成异步下载器；仅用于测试/兼容，不作为生产长跑下载路径。
class BlockingAsyncSongResourceContentFetcher : public application::ports::AsyncSongResourceContentFetcher {
public:
    BlockingAsyncSongResourceContentFetcher(std::shared_ptr<application::ports::SongResourceContentFetcher> fetcher, std::shared_ptr<application::ports::AsyncRuntime> runtime);

    application::async::Task<application::model::SongResourceContentStreamResult> streamSongResourceContent(
        application::model::SongResourceContentFetchRequest request,
        application::ports::AsyncSongResourceContentStreamCallbacks callbacks
    ) override;

private:
    std::shared_ptr<application::ports::SongResourceContentFetcher> fetcher_; // 同步内容下载器
    std::shared_ptr<application::ports::AsyncRuntime> runtime_;               // 阻塞调用隔离 runtime
};

// 把同步 BlobStore 包装成异步 BlobStore，慢写入由 runtime 隔离。
class BlockingAsyncSongResourceBlobStore : public application::ports::AsyncSongResourceBlobStore {
public:
    BlockingAsyncSongResourceBlobStore(std::shared_ptr<application::ports::SongResourceBlobStore> store, std::shared_ptr<application::ports::AsyncRuntime> runtime);

    application::async::Task<std::unique_ptr<application::ports::AsyncBlobWriteSession>> open(application::model::SongResourceBlobStreamSaveRequest request) override;

private:
    class Session;

    std::shared_ptr<application::ports::SongResourceBlobStore> store_; // 同步 blob store
    std::shared_ptr<application::ports::AsyncRuntime> runtime_;        // 阻塞调用隔离 runtime
};

// 把同步云端上传 port 包装成异步 port，外部命令执行由 runtime 隔离。
class BlockingAsyncSongResourceCloudUploader : public application::ports::AsyncSongResourceCloudUploader {
public:
    BlockingAsyncSongResourceCloudUploader(std::shared_ptr<application::ports::SongResourceCloudUploader> uploader, std::shared_ptr<application::ports::AsyncRuntime> runtime);

    application::async::Task<application::model::SongResourceCloudUploadResult> upload(application::model::SongResourceCloudUploadRequest request) override;

private:
    std::shared_ptr<application::ports::SongResourceCloudUploader> uploader_; // 同步上传 port
    std::shared_ptr<application::ports::AsyncRuntime> runtime_;               // 阻塞调用隔离 runtime
};

// 把同步批量云端上传 port 包装成异步 port，外部命令执行由 runtime 隔离。
class BlockingAsyncSongResourceBatchCloudUploader : public application::ports::AsyncSongResourceBatchCloudUploader {
public:
    BlockingAsyncSongResourceBatchCloudUploader(std::shared_ptr<application::ports::SongResourceBatchCloudUploader> uploader, std::shared_ptr<application::ports::AsyncRuntime> runtime);

    application::async::Task<application::model::SongResourceBatchCloudUploadResult> uploadBatch(application::model::SongResourceBatchCloudUploadRequest request) override;

private:
    std::shared_ptr<application::ports::SongResourceBatchCloudUploader> uploader_; // 同步批量上传 port
    std::shared_ptr<application::ports::AsyncRuntime> runtime_;                    // 阻塞调用隔离 runtime
};

} // namespace orchestrator::modules::crawler::application::use_cases
