#pragma once
#include <mutex>
#include <unordered_set>
#include <string>
#include <cstdint>
#include "crawler/application/use_cases/AsyncSongResourceDownloadWorkflow.h"

namespace orchestrator::modules::crawler::application::use_cases {
struct AsyncSongResourceDownloadWorkflow::ProcessRunState {
    application::model::SongResourceProcessOptions options; // 本次批处理有效选项
    application::model::SongResourceProcessResult result;   // 批次聚合结果
    std::mutex result_mutex;                                // 保护 result
    std::mutex related_mutex;                               // 保护关联资源去重
    std::mutex pacmtoken_mutex;                             // 保护 pacmtoken 刷新状态
    std::unordered_set<std::string> processed_related_keys; // 同一批次内关联资源去重键
    std::uint64_t last_pacmtoken_refresh_at = 0;            // 最近刷新时间
    bool pacmtoken_refreshing = false;                      // 是否已有协程在刷新 token
};

} // namespace orchestrator::modules::crawler::application::use_cases
