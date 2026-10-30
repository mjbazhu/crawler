#pragma once
#include <cstddef>
#include <cstdint>

#include "crawler/application/model/SongResourceQueue.h"
#include "crawler/application/model/MiguPacmtoken.h"
#include "crawler/application/ports/execution/AsyncRuntime.h"
#include "crawler/application/ports/execution/ResourcePostProcessor.h"
#include "crawler/application/ports/execution/TaskExecutor.h"
#include "crawler/application/ports/parsing/SongResourceParser.h"
#include "crawler/application/ports/persistence/SongResourceQueue.h"
#include "crawler/application/ports/provider/MiguPacmtokenRefresher.h"
#include "crawler/application/ports/provider/MiguPacmtokenStore.h"
#include "crawler/application/ports/provider/SongResourceContentFetcher.h"
#include "crawler/application/ports/provider/SongResourceProvider.h"
#include "crawler/application/ports/storage/SongResourceBlobStore.h"
#include "crawler/application/ports/storage/SongResourceCloudUploader.h"
#include "crawler/application/use_cases/SongResourcePathPlanner.h"

#include <memory>
#include <string>
#include <unordered_set>
#include <mutex>

namespace orchestrator::modules::crawler::application::use_cases {

class AsyncSongResourceDownloadWorkflow; // 同步兼容入口委托的唯一资源编排

// 歌曲资源处理监听器，供 application 编排层实时记录批次 item 和事件。
class SongResourceProcessListener {
public:
    virtual ~SongResourceProcessListener() = default;

    // 下载和新任务边界协作取消；默认监听器不请求停止。
    virtual bool isCancellationRequested()
    {
        return false;
    }

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
        std::shared_ptr<application::ports::MiguPacmtokenStore> pacmtoken_store = nullptr,
        std::shared_ptr<application::ports::SongResourceCloudUploader> cloud_uploader = nullptr,
        application::ports::ResourceProcessingExtensions extensions = {}
    );

    // 处理一批待下载资源；默认不过滤资源类型，audio 成功或受限时顺手只认领一次 cover/lyric/mrc/trc。
    application::model::SongResourceProcessResult processPending(application::model::SongResourceClaimQuery query, SongResourceProcessListener *listener = nullptr);

    // 元数据响应已有地址时立即复用同一认领、下载和文件补偿流程。
    application::model::SongResourceProcessResult processAvailableRelated(application::model::SongResourceQueueTarget song, application::model::SongResourceFetchResult resources);

    // 处理单条已认领资源，供测试或手动编排使用。
    application::model::SongResourceProcessItem processClaimed(application::model::SongResourceQueueTarget target, std::string batch_id);
    // 在独立快照临时区使用同一流程及显式预算，保持两参数兼容入口。
    application::model::SongResourceProcessItem processClaimed(application::model::SongResourceQueueTarget target, std::string batch_id, application::model::SongResourceProcessOptions options);

private:
    std::shared_ptr<application::ports::AsyncRuntime> runtime_;   // 同步适配器的内联续体执行器
    std::shared_ptr<AsyncSongResourceDownloadWorkflow> workflow_; // 唯一资源编排，实例无跨批次可变状态
};

} // namespace orchestrator::modules::crawler::application::use_cases
