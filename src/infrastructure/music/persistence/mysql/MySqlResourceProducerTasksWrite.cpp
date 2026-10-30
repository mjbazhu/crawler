#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceProducerTasks.h"
#include "MySqlResourceProducerTasksSupport.h"
#include "MySqlProducedArtifactRegistrySupport.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {
namespace jobs = transfer_jobs;
namespace producers = producer_jobs;
namespace model = application::model;

application::result::ServiceResult<std::string> MySqlResourceProducerTasks::complete(const model::ResourceProducerTask &value, const model::LyricArtifactBundle &bundle, std::uint64_t source_size)
{
    auto work = [value, bundle, source_size](boost::mysql::pooled_connection &connection) -> boost::asio::awaitable<std::string> {
        (void)co_await jobs::operation(connection, value.source_job.operation_id, true);
        (void)co_await jobs::snapshot(connection, value.source_job.resource);
        // 所有资源行在目标/任务之前取锁，防止与产物认领产生锁序反转。
        (void)co_await jobs::query(connection, "song_resource_artifacts/producer_list", value.source_job.resource.resource_id);
        (void)co_await jobs::targets(connection);
        const auto actual = co_await producers::read(connection, value.producer_id);
        if (!actual || actual->claim_generation != value.claim_generation || actual->source_job.task_id != value.source_job.task_id || actual->processor_version != bundle.processor_version || actual->schema_version != bundle.schema_version || source_size > actual->policy.max_bytes) throw std::logic_error("producer completion identity changed");
        if (actual->state == "succeeded" && !actual->child_operation_id.empty()) co_return actual->child_operation_id;
        const auto expires_at = actual->claimed_at + static_cast<std::uint64_t>((actual->policy.timeout_ms + 999) / 1000);
        if (jobs::now() > expires_at) throw std::logic_error("producer completion exceeded frozen budget");
        if (!(co_await producers::current(connection, value))) throw std::logic_error("producer completion is stale");
        const auto &parent = actual->source_job;
        const auto artifacts = co_await produced_artifacts::recordOnConnection(connection, parent, bundle, source_size);
        model::ResourceTransferRequest child;
        child.resources = artifacts;
        child.destinations = {parent.destination};
        child.parent_operation_id = parent.operation_id;
        child.parent_task_id = parent.task_id;
        child.parent_claim_generation = parent.claim_generation;
        child.child_kind = "artifact";
        child.source_options = parent.source_options;
        child.idempotency_key = "producer:" + parent.operation_id + ':' + std::to_string(parent.resource.resource_id) + ':' + std::string(model::resourceDestinationName(parent.destination));
        auto profile = co_await jobs::frozenProfile(connection, parent);
        // 本认领已经证明 key 可用，子产物按父传输预算保存，不能受 MRC 输入上限替代。
        profile.producer_key_available = true;
        const auto operation = co_await jobs::enqueueOnConnection(connection, std::move(child), {profile}, *actual);
        if (jobs::now() > expires_at) throw std::logic_error("producer registration exceeded frozen budget");
        if (!(co_await producers::finish(connection, *actual, "succeeded", "ok", {}, operation.operation_id))) throw std::logic_error("producer completion claim changed");
        co_return operation.operation_id;
    };
    return jobs::run(pool_, jobs::transaction<std::string>(pool_, std::move(work)), true, std::chrono::milliseconds((std::clamp)(value.policy.timeout_ms, 100, 5000)));
}

application::result::ServiceResult<bool> MySqlResourceProducerTasks::fail(const model::ResourceProducerTask &value, model::ResourceTransferJobFailure failure)
{
    auto work = [value, failure = std::move(failure)](boost::mysql::pooled_connection &connection) -> boost::asio::awaitable<bool> {
        if (failure.result_code.empty() || !jobs::code(failure.result_code) || !jobs::code(failure.error_category)) throw std::invalid_argument("invalid producer failure");
        const bool valid = co_await producers::current(connection, value);
        const auto actual = co_await producers::read(connection, value.producer_id);
        if (!actual || actual->state != "running" || actual->claim_generation != value.claim_generation) co_return false;
        const bool cancelled = actual->cancel_requested || actual->source_job.cancel_requested || actual->source_job.state == "cancelled";
        if (valid && !cancelled && failure.source_restricted && actual->source_job.resource.owner == model::ResourceOwner::Song) {
            const auto &parent = actual->source_job;
            (void)co_await jobs::query(connection, "song_resources/transfer_restricted", failure.result_code, parent.resource.resource_id, parent.version.content_generation, parent.source_revision);
            co_return co_await producers::finish(connection, *actual, "failed", failure.result_code, "copyright");
        }
        const bool retry = valid && failure.retryable && actual->attempt_count <= actual->policy.retries;
        const std::string state = cancelled ? "cancelled" : !valid                             ? "blocked" :
                                                        failure.result_code == "waiting_input" ? "waiting_input" :
                                                        retry                                  ? "retry" :
                                                                                                 "failed";
        co_return co_await producers::finish(connection, *actual, state, cancelled ? "cancelled" : failure.result_code, failure.error_category);
    };
    return jobs::run(pool_, jobs::transaction<bool>(pool_, std::move(work)), true, std::chrono::seconds(5));
}
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
