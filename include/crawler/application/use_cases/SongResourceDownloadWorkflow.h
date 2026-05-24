#pragma once

#include "crawler/application/model/SongResourceQueue.h"
#include "crawler/application/model/MiguPacmtoken.h"
#include "crawler/application/ports/execution/TaskExecutor.h"
#include "crawler/application/ports/parsing/SongResourceParser.h"
#include "crawler/application/ports/persistence/SongResourceQueue.h"
#include "crawler/application/ports/provider/MiguPacmtokenRefresher.h"
#include "crawler/application/ports/provider/MiguPacmtokenStore.h"
#include "crawler/application/ports/provider/SongResourceContentFetcher.h"
#include "crawler/application/ports/provider/SongResourceProvider.h"
#include "crawler/application/ports/storage/SongResourceBlobStore.h"
#include "crawler/application/use_cases/SongResourcePathPlanner.h"

#include <memory>
#include <string>
#include <unordered_set>
#include <mutex>

namespace orchestrator::modules::crawler::application::use_cases {

// 歌曲资源处理监听器，供 application 编排层实时记录批次 item 和事件。
class SongResourceProcessListener {
public:
    virtual ~SongResourceProcessListener() = default;

    // 批次阶段事件；payload 必须脱敏，不得包含临时下载 URL、Cookie 或 token。
    virtual void onBatchEvent(std::string event_type, std::string payload)
    {
        (void)event_type;
        (void)payload;
    }

    // 资源项开始处理；实现方不得在这里执行长时间阻塞逻辑。
    virtual void onItemStarted(const application::model::SongResourceProcessItem &item) = 0;

    // 资源项处理结束；成功、失败和跳过都会回调。
    virtual void onItemFinished(const application::model::SongResourceProcessItem &item) = 0;

    // 资源项阶段事件；payload 必须脱敏，不得包含临时下载 URL、Cookie 或 token。
    virtual void onItemEvent(const application::model::SongResourceProcessItem &item, std::string event_type, std::string payload)
    {
        (void)item;
        (void)event_type;
        (void)payload;
    }

    // pacmtoken 已刷新；实现方不得记录 token 明文。
    virtual void onPacmtokenRefreshed(bool refreshed, std::size_t pacmtoken_length, std::uint64_t updated_at)
    {
        (void)refreshed;
        (void)pacmtoken_length;
        (void)updated_at;
    }
};

// 歌曲资源下载 workflow，按资源队列行编排 provider、parser、下载、保存和回写。
class SongResourceDownloadWorkflow {
public:
    // 保存 workflow 依赖；依赖均为窄 port，便于后续替换下载或存储实现。
    SongResourceDownloadWorkflow(
        std::shared_ptr<application::ports::SongResourceQueue> resource_queue,
        std::shared_ptr<application::ports::SongResourceProvider> resource_provider,
        std::shared_ptr<application::ports::SongResourceParser> resource_parser,
        std::shared_ptr<application::ports::SongResourceContentFetcher> content_fetcher,
        std::shared_ptr<application::ports::SongResourceBlobStore> blob_store,
        SongResourcePathPlanner path_planner = {},
        std::shared_ptr<application::ports::TaskExecutor> resource_executor = nullptr,
        std::shared_ptr<application::ports::MiguPacmtokenRefresher> pacmtoken_refresher = nullptr,
        std::shared_ptr<application::ports::MiguPacmtokenStore> pacmtoken_store = nullptr
    );

    // 处理一批待下载资源；默认处理 audio，并顺手只认领一次 lyric/mrc/trc。
    application::model::SongResourceProcessResult processPending(application::model::SongResourceClaimQuery query, SongResourceProcessListener *listener = nullptr);

    // 处理单条已认领资源，供测试或手动编排使用。
    application::model::SongResourceProcessItem processClaimed(application::model::SongResourceQueueTarget target, std::string batch_id);

private:
    struct ProcessRunState;

    application::model::SongResourceProcessItem processAudio(application::model::SongResourceQueueTarget target, const std::string &batch_id, application::model::SongResourceProcessResult *batch_result, SongResourceProcessListener *listener, ProcessRunState *state);
    application::model::SongResourceProcessItem processDirectResource(application::model::SongResourceQueueTarget target, const std::string &batch_id, const application::model::SongResourceProcessOptions &options, SongResourceProcessListener *listener);
    application::model::SongResourceProcessItem downloadAndStore(application::model::SongResourceQueueTarget target, std::string remote_url, const std::string &batch_id, const application::model::SongResourceProcessOptions &options);
    application::model::SongResourceProcessItem downloadAndStoreAttempt(application::model::SongResourceQueueTarget target, std::string remote_url, const std::string &batch_id, const application::model::SongResourceProcessOptions &options, bool write_failure, SongResourceProcessListener *listener);
    void processRelatedTextResources(const application::model::SongResourceQueueTarget &audio_target, const application::model::SongResourceFetchResult &fetch_result, const std::string &batch_id, application::model::SongResourceProcessResult &batch_result, SongResourceProcessListener *listener, ProcessRunState *state);
    bool refreshPacmtoken(ProcessRunState &state, SongResourceProcessListener *listener, bool force);
    application::model::SongResourceProcessItem fail(application::model::SongResourceQueueTarget target, std::string category, std::string code, std::string message, const std::string &batch_id, SongResourceProcessListener *listener = nullptr);
    application::model::SongResourceProcessItem skip(application::model::SongResourceQueueTarget target, std::string category, std::string code, std::string message, const std::string &batch_id, SongResourceProcessListener *listener = nullptr);

    std::shared_ptr<application::ports::SongResourceQueue> resource_queue_;           // 资源队列 port
    std::shared_ptr<application::ports::SongResourceProvider> resource_provider_;     // 资源 URL provider
    std::shared_ptr<application::ports::SongResourceParser> resource_parser_;         // 资源响应 parser
    std::shared_ptr<application::ports::SongResourceContentFetcher> content_fetcher_; // 资源内容下载器
    std::shared_ptr<application::ports::SongResourceBlobStore> blob_store_;           // 资源保存 port
    SongResourcePathPlanner path_planner_;                                            // 相对路径规划器
    std::shared_ptr<application::ports::TaskExecutor> resource_executor_;             // 资源下载并发执行器
    std::shared_ptr<application::ports::MiguPacmtokenRefresher> pacmtoken_refresher_; // pacmtoken 刷新 port
    std::shared_ptr<application::ports::MiguPacmtokenStore> pacmtoken_store_;         // pacmtoken 运行期存储
};

} // namespace orchestrator::modules::crawler::application::use_cases
