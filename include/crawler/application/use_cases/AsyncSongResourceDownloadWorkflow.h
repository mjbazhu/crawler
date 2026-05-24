#pragma once

#include "crawler/application/model/MiguPacmtoken.h"
#include "crawler/application/model/SongResourceQueue.h"
#include "crawler/application/ports/execution/AsyncRuntime.h"
#include "crawler/application/ports/parsing/SongResourceParser.h"
#include "crawler/application/ports/persistence/AsyncSongResourceQueue.h"
#include "crawler/application/ports/provider/AsyncSongResourceContentFetcher.h"
#include "crawler/application/ports/provider/AsyncSongResourceProvider.h"
#include "crawler/application/ports/provider/MiguPacmtokenRefresher.h"
#include "crawler/application/ports/provider/MiguPacmtokenStore.h"
#include "crawler/application/ports/storage/AsyncSongResourceBlobStore.h"
#include "crawler/application/use_cases/SongResourceDownloadWorkflow.h"
#include "crawler/application/use_cases/SongResourcePathPlanner.h"

#include <atomic>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_set>

namespace orchestrator::modules::crawler::application::use_cases {

// 异步歌曲资源下载 workflow，仅用于 resources/process async=true。
class AsyncSongResourceDownloadWorkflow {
public:
    // 保存异步 workflow 依赖；application 层只依赖异步 port，不依赖 Asio。
    AsyncSongResourceDownloadWorkflow(
        std::shared_ptr<application::ports::AsyncRuntime> runtime,
        std::shared_ptr<application::ports::AsyncSongResourceQueue> resource_queue,
        std::shared_ptr<application::ports::AsyncSongResourceProvider> resource_provider,
        std::shared_ptr<application::ports::SongResourceParser> resource_parser,
        std::shared_ptr<application::ports::AsyncSongResourceContentFetcher> content_fetcher,
        std::shared_ptr<application::ports::AsyncSongResourceBlobStore> blob_store,
        SongResourcePathPlanner path_planner = {},
        std::shared_ptr<application::ports::MiguPacmtokenRefresher> pacmtoken_refresher = nullptr,
        std::shared_ptr<application::ports::MiguPacmtokenStore> pacmtoken_store = nullptr
    );

    // 异步处理一批待下载资源；主资源并发处理，关联 lyric/mrc/trc 按歌曲去重。
    application::async::Task<application::model::SongResourceProcessResult> processPending(application::model::SongResourceClaimQuery query, SongResourceProcessListener *listener = nullptr);

private:
    struct ProcessRunState;

    application::async::Task<void> processWorker(std::shared_ptr<ProcessRunState> state, application::model::SongResourceQueueTarget target, std::string batch_id, SongResourceProcessListener *listener);
    application::async::Task<application::model::SongResourceProcessItem> processAudio(application::model::SongResourceQueueTarget target, const std::string &batch_id, std::shared_ptr<ProcessRunState> state, SongResourceProcessListener *listener);
    application::async::Task<application::model::SongResourceProcessItem> processDirectResource(application::model::SongResourceQueueTarget target, const std::string &batch_id, const application::model::SongResourceProcessOptions &options, SongResourceProcessListener *listener);
    application::async::Task<application::model::SongResourceProcessItem> downloadAndStoreAttempt(application::model::SongResourceQueueTarget target, std::string remote_url, const std::string &batch_id, const application::model::SongResourceProcessOptions &options, bool write_failure, SongResourceProcessListener *listener);
    application::async::Task<void> processRelatedTextResources(const application::model::SongResourceQueueTarget &audio_target, const application::model::SongResourceFetchResult &fetch_result, const std::string &batch_id, std::shared_ptr<ProcessRunState> state, SongResourceProcessListener *listener);
    application::async::Task<bool> refreshPacmtoken(std::shared_ptr<ProcessRunState> state, SongResourceProcessListener *listener, bool force);
    application::async::Task<application::model::SongResourceProcessItem> fail(application::model::SongResourceQueueTarget target, std::string category, std::string code, std::string message, const std::string &batch_id, SongResourceProcessListener *listener = nullptr);
    application::async::Task<application::model::SongResourceProcessItem> skip(application::model::SongResourceQueueTarget target, std::string category, std::string code, std::string message, const std::string &batch_id, SongResourceProcessListener *listener = nullptr);
    application::async::Task<void> notifyBatchEvent(SongResourceProcessListener *listener, std::string event_type, std::string payload);
    application::async::Task<void> notifyItemStarted(SongResourceProcessListener *listener, application::model::SongResourceProcessItem item);
    application::async::Task<void> notifyItemFinished(SongResourceProcessListener *listener, application::model::SongResourceProcessItem item);
    application::async::Task<void> notifyItemEvent(SongResourceProcessListener *listener, application::model::SongResourceProcessItem item, std::string event_type, std::string payload);
    application::async::Task<void> notifyPacmtokenRefreshed(SongResourceProcessListener *listener, bool refreshed, std::size_t pacmtoken_length, std::uint64_t updated_at);

    std::shared_ptr<application::ports::AsyncRuntime> runtime_;                            // application async runtime
    std::shared_ptr<application::ports::AsyncSongResourceQueue> resource_queue_;           // 异步资源队列
    std::shared_ptr<application::ports::AsyncSongResourceProvider> resource_provider_;     // 异步资源 URL provider
    std::shared_ptr<application::ports::SongResourceParser> resource_parser_;              // 响应 parser
    std::shared_ptr<application::ports::AsyncSongResourceContentFetcher> content_fetcher_; // 异步流式内容下载器
    std::shared_ptr<application::ports::AsyncSongResourceBlobStore> blob_store_;           // 异步 blob store
    SongResourcePathPlanner path_planner_;                                                 // 相对路径规划器
    std::shared_ptr<application::ports::MiguPacmtokenRefresher> pacmtoken_refresher_;      // 同步 pacmtoken 刷新 port
    std::shared_ptr<application::ports::MiguPacmtokenStore> pacmtoken_store_;              // pacmtoken 运行期存储
};

} // namespace orchestrator::modules::crawler::application::use_cases
