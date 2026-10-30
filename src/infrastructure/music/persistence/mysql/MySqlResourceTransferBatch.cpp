#include "MySqlResourceTransferBatch.h"
#include "MySqlResourceTransferPreparedQueries.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <stdexcept>
#include <string>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::transfer_jobs {
boost::asio::awaitable<void> persistProcess(PreparedQueries &statements, std::vector<PlannedItem> items)
{
    if (items.empty() || items.size() > 600) throw std::logic_error("invalid process task batch");
    const auto operation_id = items.front().job.operation_id;
    auto rows = nlohmann::json::array();
    for (const auto &item : items) {
        const auto &job = item.job;
        if (job.action != model::ResourceTransferAction::Process || job.operation_id != operation_id || !job.attempt_path.empty()) throw std::logic_error("invalid process batch plan");
        rows.push_back({{"owner_type", model::resourceOwnerName(job.resource.owner)}, {"resource_id", job.resource.resource_id}, {"destination", model::resourceDestinationName(job.destination)}, {"content_generation", job.version.content_generation}, {"source_revision", job.source_revision}, {"source_identity", job.source_identity}, {"target_revision", job.version.target_revision}, {"target_fingerprint", job.target_fingerprint}, {"state", job.state}, {"timeout_ms", job.policy.timeout_ms}, {"retries", job.policy.retries}, {"max_bytes", job.policy.max_bytes}, {"bytes_per_second", job.policy.bytes_per_second}, {"busy_task_id", job.busy_task_id}, {"result_code", job.result_code}, {"attempt_path", job.attempt_path}, {"attempt_checksum", job.attempt_checksum}, {"attempt_size_bytes", job.attempt_size_bytes}});
    }
    const auto inserted = co_await statements.execute("resource_transfer_tasks/insert_process_batch", operation_id, rows.dump());
    if (inserted.affected_rows() != items.size()) throw std::logic_error("incomplete process task batch");
    // 依赖真实 task_id 的写入由同一事务内 SELECT 获取，不假定自增连续或由 LAST_INSERT_ID 推算。
    const auto slots = co_await statements.execute("resource_transfer_slots/insert_process_batch", operation_id);
    const auto pending = std::count_if(items.begin(), items.end(), [](const auto &item) { return item.job.state == "pending"; });
    if (slots.affected_rows() != static_cast<std::uint64_t>(pending)) throw std::logic_error("incomplete process slot batch");
    (void)co_await statements.execute("resource_cloud_receipts/ensure_process_batch", operation_id);
}
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::transfer_jobs
