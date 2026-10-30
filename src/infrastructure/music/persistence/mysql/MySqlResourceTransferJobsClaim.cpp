#include "MySqlResourceTransferJobsSupport.h"
#include "MySqlLegacyResourceTransferClaim.h"

#include <algorithm>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::transfer_jobs {

boost::asio::awaitable<std::optional<model::ResourceTransferJob>> claim(std::shared_ptr<MySqlConnectionPool> pool, model::ResourceDestination destination, std::string fingerprint, std::string adapter)
{
    if (model::resourceDestinationName(destination).empty() || !hash(fingerprint) || !model::supportsResourceTransferAdapter(destination, adapter)) throw std::invalid_argument("invalid claim target");
    std::uint64_t id = 0;
    std::string operation_id;
    {
        auto connection = co_await pool->coAcquire();
        // 两个固定状态各取索引首项，再合并两行，避免大积压下跨状态排序全队列。
        const auto provider = std::string(model::resourceDestinationName(destination));
        const auto candidate = co_await query(connection, "resource_transfer_tasks/next", provider, provider);
        if (candidate.rows().empty()) co_return std::nullopt;
        id = MySqlValueReader::uint64At(candidate.rows().front(), 0);
        operation_id = MySqlValueReader::stringAt(candidate.rows().front(), 1);
    }
    auto work = [id, operation_id, destination, fingerprint, adapter](boost::mysql::pooled_connection &connection) -> boost::asio::awaitable<std::optional<model::ResourceTransferJob>> {
        if (!(co_await legacyClaimAllowed(connection, operation_id, destination))) co_return std::nullopt;
        const auto parent = co_await operation(connection, operation_id, true);
        if (!parent) co_return std::nullopt;
        // 父操作锁使同一操作的短事务串行，传输过程不持有此锁。
        const auto found = std::find_if(parent->tasks.begin(), parent->tasks.end(), [id](const auto &item) { return item.task_id == id; });
        if (found == parent->tasks.end()) co_return std::nullopt;
        const auto resource = co_await snapshot(connection, found->resource);
        const auto states = co_await targets(connection);
        auto actual = co_await task(connection, id);
        if (!actual || actual->destination != destination || (actual->state != "pending" && actual->state != "retry")) co_return std::nullopt;
        auto &job = *actual;
        const auto owner = std::string(model::resourceOwnerName(job.resource.owner)), provider = std::string(model::resourceDestinationName(destination));
        const auto occupied = co_await query(connection, "resource_transfer_slots/lock", owner, job.resource.resource_id, provider);
        if (occupied.rows().empty() || MySqlValueReader::uint64At(occupied.rows().front(), 0) != id) {
            (void)co_await finish(connection, job, "busy", "ownership_changed", "");
            co_return std::nullopt;
        }
        const auto &target = states.at(static_cast<std::size_t>(destination));
        std::string blocked;
        if (!resource)
            blocked = "resource_not_found";
        else if (resource->generation != job.version.content_generation || resource->source_revision != job.source_revision || resource->source_identity != job.source_identity)
            blocked = "source_changed";
        else if (target.revision != job.version.target_revision || target.fingerprint != job.target_fingerprint || fingerprint != job.target_fingerprint)
            blocked = "target_changed";
        if (job.cancel_requested) {
            (void)co_await finish(connection, job, "cancelled", "cancelled", "");
            co_return std::nullopt;
        }
        if (!blocked.empty()) {
            (void)co_await finish(connection, job, "blocked", blocked, "");
            co_return std::nullopt;
        }
        model::ResourceTransferSkip skip = resource->skip;
        boost::mysql::results cloud;
        if (destination != model::ResourceDestination::Local) {
            cloud = co_await query(connection, "resource_cloud_receipts/transfer_lock", owner, job.resource.resource_id, provider);
            if (cloud.rows().empty()) throw std::logic_error("missing native receipt");
            const auto row = cloud.rows().front();
            skip = {{MySqlValueReader::uint64At(row, 9), MySqlValueReader::uint64At(row, 10)}, MySqlValueReader::stringAt(row, 11), MySqlValueReader::stringAt(row, 12), MySqlValueReader::stringAt(row, 8) == "skip"};
        }
        if ((skip.enabled && skip.version == job.version && !skip.baseline_id.empty() && !skip.reason.empty()) || resource->status == 4) {
            (void)co_await finish(connection, job, "skipped", resource->status == 4 ? "restricted" : "skip_existing", "");
            co_return std::nullopt;
        }
        if (!(co_await query(connection, "resource_baseline_blocks/find", owner, job.resource.resource_id, resource->generation)).rows().empty()) {
            (void)co_await finish(connection, job, "blocked", "historical_scope_excluded", "");
            co_return std::nullopt;
        }
        if (!model::supportsResourceTransferAdapter(destination, job.adapter) || job.adapter != adapter) {
            const bool uncertain = !job.attempt_path.empty();
            (void)co_await finish(connection, job, uncertain ? "uncertain" : "blocked", job.adapter.empty() ? "adapter_unresolved" : "adapter_changed", "invalid_target", uncertain);
            co_return std::nullopt;
        }
        bool ready = target.state == "active" && target.baseline_ready;
        for (const auto &state : states) ready = ready && state.revision > 0 && state.state != "maintenance" && (state.state != "active" || (state.baseline_ready && hash(state.fingerprint)));
        if (!ready) {
            // 暂停只阻止认领，不取消已登记任务；恢复后仍使用冻结版本。
            co_return std::nullopt;
        }
        const auto batch = "rt:" + std::to_string(id);
        if (destination == model::ResourceDestination::Local) {
            const auto changed = co_await query(connection, table(job.resource.owner) + "/transfer_claim", batch, job.resource.resource_id);
            if (changed.affected_rows() != 1) {
                (void)co_await finish(connection, job, "busy", "legacy_busy", "");
                co_return std::nullopt;
            }
            job.native_claim_generation = resource->claim_generation + 1;
        } else {
            const auto row = cloud.rows().front();
            if (MySqlValueReader::stringAt(row, 0) == "running") {
                (void)co_await finish(connection, job, "busy", "legacy_busy", "");
                co_return std::nullopt;
            }
            job.native_claim_generation = MySqlValueReader::uint64At(row, 1) + 1;
            (void)co_await query(connection, "resource_cloud_receipts/transfer_claim", job.version.content_generation, job.version.target_revision, batch, owner, job.resource.resource_id, provider);
        }
        const auto changed = co_await query(connection, "resource_transfer_tasks/claim", job.native_claim_generation, id);
        if (changed.affected_rows() != 1) throw std::logic_error("claim changed");
        co_return co_await task(connection, id);
    };
    co_return co_await transaction<std::optional<model::ResourceTransferJob>>(std::move(pool), std::move(work), true);
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::transfer_jobs
