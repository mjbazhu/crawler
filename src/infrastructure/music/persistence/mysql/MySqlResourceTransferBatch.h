#pragma once

#include "MySqlResourceTransferJobsSupport.h"

#include <array>
#include <cstdint>
#include <optional>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::transfer_jobs {
// 判定结果保留旧任务身份，非 Process 路径仍按原顺序处理修复和关联继承。
struct PlannedItem {
    model::ResourceTransferJob job;  // 原策略得出的完整持久任务字段
    std::uint64_t previous_task = 0; // 旧独占槽，仅逐项登记路径使用
};
// 复用原条件读和锁定顺序；Process 规划不写入新任务、槽或云回执。
boost::asio::awaitable<PlannedItem> planItem(PreparedQueries &statements, model::ResourceTransferJob job, const std::optional<Snapshot> &resource, const std::array<Target, 3> &states, const model::ResourceTransferTargetProfile *profile);
// 只处理唯一资源目标键的 Process；三个批量 DML 严格逐个等待，任一错误立即退出。
boost::asio::awaitable<void> persistProcess(PreparedQueries &statements, std::vector<PlannedItem> items);
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::transfer_jobs
