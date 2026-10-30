#include "MySqlResourceTransferJobsSupport.h"
#include "MySqlResourceTransferPreparedQueries.h"
#include "MySqlResourceTransferBatch.h"
#include "MySqlResourceTransferRelated.h"

#include "crawler/application/use_cases/ResourceTransferPolicy.h"

#include <algorithm>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::transfer_jobs {
namespace {
// 已审阅的 skip 必须同时匹配内容、目标和证据标识。
bool skipped(const model::ResourceTransferSkip &skip, model::ResourceTransferVersion version)
{
    return skip.enabled && skip.version == version && !skip.baseline_id.empty() && !skip.reason.empty();
}

// 三个状态行共同提供全局维护闸门，暂停的其他端不阻塞当前端。
bool ready(const std::array<Target, 3> &states, const Target &target)
{
    for (const auto &state : states)
        if (state.revision == 0 || state.state == "maintenance" || (state.state == "active" && (!state.baseline_ready || !hash(state.fingerprint)))) return false;
    return target.state == "active" && target.baseline_ready && hash(target.fingerprint);
}

// 策略与技术配置独立判定；坏配置只影响本端。
bool validProfile(const model::ResourceTransferTargetProfile &profile, const Target &target)
{
    if (profile.adapter == "baidupcs" && profile.policy.bytes_per_second) return false;
    return profile.available && model::supportsResourceTransferAdapter(profile.destination, profile.adapter) && hash(profile.fingerprint) && profile.fingerprint == target.fingerprint && profile.policy.timeout_ms >= 100 && profile.policy.timeout_ms <= 600000 && profile.policy.retries >= 0 && profile.policy.retries <= 5 && profile.policy.max_bytes > 0 && profile.policy.max_bytes <= std::uint64_t(4) * 1024 * 1024 * 1024;
}
} // namespace

boost::asio::awaitable<PlannedItem> planItem(PreparedQueries &statements, model::ResourceTransferJob job, const std::optional<Snapshot> &resource, const std::array<Target, 3> &states, const model::ResourceTransferTargetProfile *profile)
{
    auto &connection = statements.connection();
    const auto owner = std::string(model::resourceOwnerName(job.resource.owner));
    const auto destination = std::string(model::resourceDestinationName(job.destination));
    const auto &target = states.at(static_cast<std::size_t>(job.destination));
    job.version = {resource ? resource->generation : 0, target.revision};
    job.source_revision = resource ? resource->source_revision : 0;
    job.source_identity = resource ? resource->source_identity : "";
    job.target_fingerprint = target.fingerprint;
    job.adapter = profile ? profile->adapter : "";
    if (profile) job.policy = profile->policy;
    const bool producer_only = job.action == model::ResourceTransferAction::ProduceArtifacts;
    if (producer_only) {
        if (job.resource.owner != model::ResourceOwner::Song) throw std::invalid_argument("producer requires Song MRC");
        if (resource) {
            const auto kind = co_await statements.execute("song_resources/producer_kind", job.resource.resource_id);
            if (kind.rows().empty() || MySqlValueReader::stringAt(kind.rows().front(), 0) != "mrc") throw std::invalid_argument("producer requires MRC source");
        }
    }
    model::ResourceTransferSkip skip;
    bool already_current = false, native_busy = false, verification_required = false, verification_adapter_changed = false;
    boost::mysql::results cloud;
    if (resource && job.destination == model::ResourceDestination::Local) {
        skip = resource->skip;
        native_busy = resource->status == 1;
        already_current = resource->local_generation == job.version.content_generation && resource->local_revision == target.revision && !resource->local_path.empty() && hash(resource->reference_sha256) && model::parseLegacyResourceChecksum(resource->checksum).sha256 == resource->reference_sha256 && resource->size_bytes == resource->reference_size;
    } else if (resource) {
        cloud = co_await statements.execute("resource_cloud_receipts/transfer_lock", owner, job.resource.resource_id, destination);
        if (!cloud.rows().empty()) {
            const auto row = cloud.rows().front();
            native_busy = MySqlValueReader::stringAt(row, 0) == "running";
            skip = {{MySqlValueReader::uint64At(row, 9), MySqlValueReader::uint64At(row, 10)}, MySqlValueReader::stringAt(row, 11), MySqlValueReader::stringAt(row, 12), MySqlValueReader::stringAt(row, 8) == "skip"};
            const auto verification = MySqlValueReader::stringAt(row, 13);
            already_current = MySqlValueReader::uint64At(row, 3) == job.version.content_generation && MySqlValueReader::uint64At(row, 4) == target.revision && hash(resource->reference_sha256) && MySqlValueReader::stringAt(row, 5) == resource->reference_sha256 && MySqlValueReader::uint64At(row, 6) == resource->reference_size && !MySqlValueReader::stringAt(row, 7).empty() && (verification == "sha256_verified" || verification == "hash_matched");
            verification_required = already_current && job.policy.strict_sha256 && verification != "sha256_verified";
            if (verification_required) {
                verification_adapter_changed = MySqlValueReader::stringAt(row, 14) != job.adapter;
                already_current = false;
                if (job.action == model::ResourceTransferAction::Repair) {
                    job.attempt_path = MySqlValueReader::stringAt(row, 7);
                    job.attempt_checksum = MySqlValueReader::stringAt(row, 5);
                    job.attempt_size_bytes = MySqlValueReader::uint64At(row, 6);
                }
            }
        }
    }
    job.state = "blocked";
    job.result_code = "resource_not_found";
    std::uint64_t previous_task = 0;
    if (!profile)
        job.result_code = "destination_disabled";
    else if (producer_only) {
        // 显式生成只读 MRC 输入；不解除旧 skip，也不登记原始文件传输槽。
        if (resource && resource->status == 4)
            job.result_code = "restricted";
        else if (resource && !(co_await statements.execute("resource_baseline_blocks/find", owner, job.resource.resource_id, resource->generation)).rows().empty())
            job.result_code = "historical_scope_excluded";
        else if (resource && hash(resource->source_identity)) {
            if (!ready(states, target))
                job.result_code = "dispatch_blocked";
            else if (!validProfile(*profile, target)) {
                job.state = "unavailable";
                job.result_code = "target_unavailable";
            } else if (!profile->produces_artifacts)
                job.result_code = "producer_disabled";
            else {
                job.state = "skipped";
                job.result_code = "producer_only";
            }
        }
    } else if (resource && skipped(skip, job.version)) {
        job.state = "skipped";
        job.result_code = "skip_existing";
    } else if (resource && resource->status == 4) {
        job.state = "skipped";
        job.result_code = "restricted";
    } else if (resource && !(co_await statements.execute("resource_baseline_blocks/find", owner, job.resource.resource_id, resource->generation)).rows().empty()) {
        job.result_code = "historical_scope_excluded";
    } else if (resource && hash(resource->source_identity)) {
        const auto reads = co_await statements.slotAndLatest(owner, job.resource.resource_id, destination, job.version.content_generation, job.version.target_revision);
        const auto &occupied = reads.first;
        std::string occupied_state;
        if (!occupied.rows().empty()) {
            previous_task = MySqlValueReader::uint64At(occupied.rows().front(), 0);
            occupied_state = MySqlValueReader::stringAt(occupied.rows().front(), 1);
        }
        const auto &last = reads.second;
        const bool cancelled = job.action == model::ResourceTransferAction::Process && !last.rows().empty() && MySqlValueReader::stringAt(last.rows().front(), 0) == "cancelled";
        bool repair = job.action == model::ResourceTransferAction::Repair && occupied_state == "uncertain";
        if (repair) {
            const auto old = co_await task(connection, previous_task);
            // 旧对象只能交给原工具核对，切换工具不能退休或接管未决副作用。
            repair = old && old->adapter == job.adapter && model::supportsResourceTransferAdapter(job.destination, old->adapter);
            if (repair && old->version == job.version && old->target_fingerprint == job.target_fingerprint && old->attempt_checksum == resource->reference_sha256 && old->attempt_size_bytes == resource->reference_size) {
                job.attempt_path = old->attempt_path;
                job.attempt_checksum = old->attempt_checksum;
                job.attempt_size_bytes = old->attempt_size_bytes;
            }
        }
        application::use_cases::ResourceTransferPolicyInput input;
        input.enabled = true;
        input.cancelled = cancelled;
        input.version = job.version;
        input.dispatch_ready = ready(states, target);
        input.already_current = already_current && !previous_task;
        input.active = native_busy || (previous_task && !repair);
        input.available = validProfile(*profile, target);
        input.waiting_input = job.resource.owner == model::ResourceOwner::SongArtifact && resource->local_path.empty() && job.attempt_path.empty() && !(profile->produces_artifacts && profile->producer_key_available);
        const auto decision = application::use_cases::evaluateResourceTransfer(input);
        using Decision = application::use_cases::ResourceWorkDecision;
        switch (decision) {
        case Decision::Transfer:
            job.state = "pending";
            job.result_code = "";
            if (verification_required && job.action != model::ResourceTransferAction::Repair) {
                job.state = "blocked";
                job.result_code = "verification_required";
                break;
            }
            if (verification_adapter_changed) {
                job.state = "blocked";
                job.result_code = "adapter_changed";
                break;
            }
            if (repair && previous_task) {
                (void)co_await statements.execute("resource_transfer_slots/release", previous_task);
                const auto old = co_await task(connection, previous_task);
                if (old) (void)co_await statements.execute("resource_transfer_tasks/invalidate", previous_task, old->claim_generation);
            }
            break;
        case Decision::AlreadyCurrent:
            job.state = "succeeded";
            job.result_code = "already_current";
            break;
        case Decision::Busy:
            job.state = "busy";
            job.result_code = occupied_state == "uncertain" ? "reconcile_required" : "busy";
            job.busy_task_id = previous_task;
            break;
        case Decision::WaitingInput:
            job.state = "waiting_input";
            job.result_code = "waiting_input";
            break;
        case Decision::Unavailable:
            job.state = "unavailable";
            job.result_code = "target_unavailable";
            break;
        case Decision::Cancelled: job.result_code = "cancelled_policy"; break;
        default: job.result_code = "dispatch_blocked"; break;
        }
    }
    // 故障配置也要能持久记录该端错误，不能因预算越界使其他端登记回滚。
    if (job.policy.timeout_ms < 100 || job.policy.timeout_ms > 600000 || job.policy.retries < 0 || job.policy.retries > 5 || job.policy.max_bytes == 0)
        job.policy = {};
    co_return PlannedItem {std::move(job), previous_task};
}

boost::asio::awaitable<bool> registerItem(PreparedQueries &statements, model::ResourceTransferJob job, const std::optional<Snapshot> &resource, const std::array<Target, 3> &states, const model::ResourceTransferTargetProfile *profile)
{
    auto planned = co_await planItem(statements, std::move(job), resource, states, profile);
    auto &accepted = planned.job;
    auto &connection = statements.connection();
    const auto owner = std::string(model::resourceOwnerName(accepted.resource.owner));
    const auto destination = std::string(model::resourceDestinationName(accepted.destination));
    const auto previous_task = planned.previous_task;
    auto inserted = co_await statements.execute("resource_transfer_tasks/insert", accepted.operation_id, owner, accepted.resource.resource_id, destination, accepted.version.content_generation, accepted.source_revision, accepted.source_identity, accepted.version.target_revision, accepted.target_fingerprint, accepted.state, accepted.policy.timeout_ms, accepted.policy.retries, accepted.policy.max_bytes, accepted.policy.bytes_per_second, accepted.busy_task_id, accepted.result_code, accepted.attempt_path, accepted.attempt_checksum, accepted.attempt_size_bytes);
    accepted.task_id = inserted.last_insert_id();
    if (accepted.state == "pending" && accepted.process_related_resources && previous_task && !accepted.attempt_path.empty()) co_await inheritRelated(connection, previous_task, accepted.task_id);
    if (accepted.state == "pending") {
        (void)co_await statements.execute("resource_transfer_slots/insert", owner, accepted.resource.resource_id, destination, accepted.task_id);
        if (accepted.destination != model::ResourceDestination::Local) {
            const auto id = accepted.resource.resource_id;
            (void)co_await statements.execute("resource_cloud_receipts/transfer_ensure", owner, id, destination, accepted.resource.owner == model::ResourceOwner::Song ? id : 0, accepted.resource.owner == model::ResourceOwner::Singer ? id : 0, accepted.resource.owner == model::ResourceOwner::Album ? id : 0, accepted.resource.owner == model::ResourceOwner::SongArtifact ? id : 0);
        }
    }
    co_return true;
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::transfer_jobs
