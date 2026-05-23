#pragma once

#include "crawler/application/async/Task.h"
#include "crawler/application/model/SongResourceQueue.h"

#include <optional>
#include <vector>

namespace orchestrator::modules::crawler::application::ports {

// 异步歌曲资源队列 port，只负责资源任务认领和状态回写。
class AsyncSongResourceQueue {
public:
    virtual ~AsyncSongResourceQueue() = default;

    // 异步认领待处理资源。
    virtual application::async::Task<std::vector<application::model::SongResourceQueueTarget>> claimPending(application::model::SongResourceClaimQuery query) = 0;

    // 异步认领同一歌曲下的歌词类资源。
    virtual application::async::Task<std::optional<application::model::SongResourceQueueTarget>> claimRelated(application::model::SongResourceRelatedClaimRequest request) = 0;

    // 异步标记资源保存成功。
    virtual application::async::Task<bool> markSucceeded(application::model::SongResourceSuccessUpdate update) = 0;

    // 异步标记资源处理失败。
    virtual application::async::Task<bool> markFailed(application::model::SongResourceFailureUpdate update) = 0;

    // 异步标记资源跳过。
    virtual application::async::Task<bool> markSkipped(application::model::SongResourceSkipUpdate update) = 0;
};

} // namespace orchestrator::modules::crawler::application::ports
