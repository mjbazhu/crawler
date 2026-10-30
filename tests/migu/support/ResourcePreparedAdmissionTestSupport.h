#pragma once

#include "migu/support/ResourceArchiveMySqlSupport.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceTransferJobs.h"

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <set>
#include <string>
#include <utility>
#include <vector>

namespace orchestrator::modules::crawler::tests::prepared_admission {
namespace mysql = infrastructure::music::persistence::mysql;
namespace model = application::model;
// 多资源、多端各用不同绑定值，覆盖请求内 statement 复用和失败后的再次借用。
inline void verify(const std::shared_ptr<archive::mysql::MySqlConnectionPool> &pool)
{
    using D = model::ResourceDestination;
    archive::activate(pool);
    std::vector<std::uint64_t> ids;
    for (int index = 0; index < 10; ++index) ids.push_back(archive::singer(pool, 7000 + index));
    auto targets = archive::profiles();
    for (std::size_t index = 0; index < targets.size(); ++index) {
        targets[index].policy.timeout_ms = 3000 + static_cast<int>(index) * 100;
        targets[index].policy.max_bytes = 100000 + index;
        targets[index].policy.bytes_per_second = (std::uint64_t(1) << 63) + index;
        targets[index].policy.retries = static_cast<int>(index);
    }
    auto input = archive::request(model::ResourceOwner::Singer, ids.front(), "prepared-admission-batch", {D::Local, D::Baidu, D::Gdrive});
    input.resources.clear();
    for (const auto id : ids) input.resources.push_back({model::ResourceOwner::Singer, id});
    input.require_write_admission = true;
    mysql::MySqlResourceTransferJobs jobs(pool);
    const std::array<std::string, 4> tables {"resource_transfer_operations", "resource_transfer_tasks", "resource_transfer_slots", "resource_cloud_receipts"};
    std::array<std::uint64_t, 4> before {};
    for (std::size_t i = 0; i < tables.size(); ++i) before[i] = archive::scalar(pool, "SELECT COUNT(*) FROM " + tables[i]);
    // 最后一项才失败，确认前29项及幂等键全部回滚，而非只回滚失败任务。
    archive::execute(pool, "CREATE TRIGGER prepared_admission_late_failure BEFORE INSERT ON resource_transfer_tasks FOR EACH ROW BEGIN IF NEW.owner_type='singer' AND NEW.resource_id=" + std::to_string(ids.back()) + " AND NEW.destination='gdrive' THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='fixture late admission failure'; END IF; END");
    const auto failed = jobs.enqueue(input, targets);
    archive::execute(pool, "DROP TRIGGER prepared_admission_late_failure");
    archive::require(!failed.isSuccess(), "late statement failure was accepted");
    for (std::size_t i = 0; i < tables.size(); ++i) archive::require(archive::scalar(pool, "SELECT COUNT(*) FROM " + tables[i]) == before[i], "late batch failure left partial work");
    // 后段 cloud ensure 失败时，新 tasks 与 slots 也必须随外层事务全部回滚。
    archive::execute(pool, "CREATE TRIGGER prepared_admission_cloud_failure BEFORE INSERT ON resource_cloud_receipts FOR EACH ROW BEGIN IF NEW.owner_type='singer' AND NEW.resource_id=" + std::to_string(ids.back()) + " AND NEW.cloud_provider='gdrive' THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='fixture late cloud ensure failure'; END IF; END");
    const auto cloud_failed = jobs.enqueue(input, targets);
    archive::execute(pool, "DROP TRIGGER prepared_admission_cloud_failure");
    archive::require(!cloud_failed.isSuccess(), "late cloud ensure failure was accepted");
    for (std::size_t i = 0; i < tables.size(); ++i) archive::require(archive::scalar(pool, "SELECT COUNT(*) FROM " + tables[i]) == before[i], "late cloud ensure failure left task or slot work");
    // 同一幂等键重试，验证回滚后新请求不复用已经归还连接的 statement。
    const auto accepted = archive::get(jobs.enqueue(input, targets));
    archive::require(accepted.tasks.size() == 30 && !accepted.replayed, "prepared batch did not create thirty tasks");
    std::set<std::pair<std::uint64_t, D>> observed;
    for (const auto &task : accepted.tasks) {
        const auto index = static_cast<std::size_t>(task.destination);
        archive::require(index < targets.size() && std::find(ids.begin(), ids.end(), task.resource.resource_id) != ids.end(), "prepared batch mixed resource or destination parameters");
        archive::require(task.resource.owner == model::ResourceOwner::Singer && task.state == "pending" && task.operation_id == accepted.operation_id, "prepared task retained another statement binding");
        archive::require(task.policy.timeout_ms == targets[index].policy.timeout_ms && task.policy.retries == targets[index].policy.retries && task.policy.max_bytes == targets[index].policy.max_bytes && task.policy.bytes_per_second == targets[index].policy.bytes_per_second && task.adapter == targets[index].adapter, "prepared task mixed per-target policy");
        archive::require(observed.emplace(task.resource.resource_id, task.destination).second, "prepared task duplicated a resource destination");
    }
    archive::require(observed.size() == 30, "prepared task matrix incomplete");
    for (std::size_t i = 0; i < accepted.tasks.size(); ++i) {
        archive::require(accepted.tasks[i].resource.resource_id == ids[i / 3] && accepted.tasks[i].destination == static_cast<D>(i % 3), "process batch changed canonical task order");
        if (i) archive::require(accepted.tasks[i - 1].task_id < accepted.tasks[i].task_id, "process batch reused or reversed task IDs");
    }
    for (std::size_t i = 0; i < tables.size(); ++i) {
        const auto delta = i == 0 ? 1 : i == 3 ? 20 :
                                                 30;
        archive::require(archive::scalar(pool, "SELECT COUNT(*) FROM " + tables[i]) == before[i] + delta, "prepared batch count mismatch");
    }
    const auto replay = archive::get(jobs.enqueue(input, targets));
    archive::require(replay.replayed && replay.operation_id == accepted.operation_id && replay.tasks.size() == 30, "prepared batch lost idempotent replay");
    archive::require(archive::scalar(pool, "SELECT COUNT(*) FROM resource_transfer_tasks") == before[1] + 30, "prepared replay added tasks");
    auto busy = input;
    busy.idempotency_key = "prepared-admission-busy";
    const auto repeated = archive::get(jobs.enqueue(std::move(busy), targets));
    archive::require(repeated.tasks.size() == 30, "prepared repeated request lost task matrix");
    for (const auto &task : repeated.tasks) {
        const auto original = std::find_if(accepted.tasks.begin(), accepted.tasks.end(), [&](const auto &candidate) { return candidate.resource == task.resource && candidate.destination == task.destination; });
        archive::require(original != accepted.tasks.end() && task.state == "busy" && task.busy_task_id == original->task_id && task.operation_id == repeated.operation_id, "prepared repeated request mixed occupied slot bindings");
    }
    archive::require(archive::scalar(pool, "SELECT COUNT(*) FROM resource_transfer_slots") == before[2] + 30 && archive::scalar(pool, "SELECT COUNT(*) FROM resource_cloud_receipts") == before[3] + 20, "prepared repeated request duplicated work ownership");
}
} // namespace orchestrator::modules::crawler::tests::prepared_admission
