#pragma once

#include "crawler/application/model/SongResourceQueue.h"

#include <optional>
#include <vector>

namespace orchestrator::modules::crawler::application::ports {

// 歌曲资源队列 port，只负责资源任务认领和状态回写。
class SongResourceQueue {
public:
    virtual ~SongResourceQueue() = default;

    // 认领待处理资源，成功认领的记录会置为 status=1。
    virtual std::vector<application::model::SongResourceQueueTarget> claimPending(application::model::SongResourceClaimQuery query) = 0;

    // 认领同一歌曲下的歌词类资源；若已完成或正在处理则返回空。
    virtual std::optional<application::model::SongResourceQueueTarget> claimRelated(application::model::SongResourceRelatedClaimRequest request) = 0;

    // 标记资源保存成功。
    virtual bool markSucceeded(application::model::SongResourceSuccessUpdate update) = 0;

    // 标记资源处理失败。
    virtual bool markFailed(application::model::SongResourceFailureUpdate update) = 0;

    // 标记资源跳过；用于不应继续重试的永久限制。
    virtual bool markSkipped(application::model::SongResourceSkipUpdate update) = 0;
};

} // namespace orchestrator::modules::crawler::application::ports
