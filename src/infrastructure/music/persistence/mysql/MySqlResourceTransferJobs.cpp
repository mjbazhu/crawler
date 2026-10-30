#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceTransferJobs.h"

#include "MySqlResourceTransferJobsSupport.h"
#include "MySqlResourceTransferRelated.h"

#include <algorithm>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {
namespace jobs = transfer_jobs;
namespace model = application::model;

MySqlResourceTransferJobs::MySqlResourceTransferJobs(std::shared_ptr<MySqlConnectionPool> pool) : pool_(std::move(pool))
{
    if (!pool_) throw std::invalid_argument("resource transfer pool required");
}

application::result::ServiceResult<model::ResourceTransferOperation> MySqlResourceTransferJobs::enqueue(model::ResourceTransferRequest request, std::vector<model::ResourceTransferTargetProfile> profiles)
{
    return jobs::run(pool_, jobs::enqueue(pool_, std::move(request), std::move(profiles)), true);
}

application::result::ServiceResult<model::ResourceTransferOperation> MySqlResourceTransferJobs::find(std::string operation_id)
{
    auto work = [id = std::move(operation_id)](boost::mysql::pooled_connection &connection) -> boost::asio::awaitable<model::ResourceTransferOperation> {
        if (id.empty() || id.size() > 64) throw std::invalid_argument("invalid operation id");
        auto value = co_await jobs::operation(connection, id, false);
        if (!value) throw jobs::MissingOperation();
        co_return *value;
    };
    return jobs::run(pool_, jobs::transaction<model::ResourceTransferOperation>(pool_, std::move(work)));
}

application::result::ServiceResult<std::optional<model::ResourceTransferJob>> MySqlResourceTransferJobs::claim(model::ResourceDestination destination, std::string fingerprint, std::string adapter)
{
    return jobs::run(pool_, jobs::claim(pool_, destination, std::move(fingerprint), std::move(adapter)), true);
}

application::result::ServiceResult<bool> MySqlResourceTransferJobs::shouldStop(const model::ResourceTransferJob &job)
{
    auto work = [job](boost::mysql::pooled_connection &connection) -> boost::asio::awaitable<bool> { co_return !(co_await jobs::current(connection, job)); };
    return jobs::run(pool_, jobs::transaction<bool>(pool_, std::move(work)), false, std::chrono::milliseconds((std::clamp)(job.policy.timeout_ms, 100, 1000)));
}

application::result::ServiceResult<bool> MySqlResourceTransferJobs::beginAttempt(const model::ResourceTransferJob &job, model::ResourceTransferAttempt attempt)
{
    auto work = [job, attempt = std::move(attempt)](boost::mysql::pooled_connection &connection) -> boost::asio::awaitable<bool> {
        if (!jobs::hash(attempt.checksum) || !jobs::safePath(attempt.path) || attempt.size_bytes == 0) throw std::invalid_argument("invalid attempt");
        if (!(co_await jobs::current(connection, job))) co_return false;
        const auto actual = co_await jobs::task(connection, job.task_id);
        if (attempt.size_bytes > actual->policy.max_bytes) throw std::invalid_argument("attempt exceeds frozen budget");
        const auto resource = co_await jobs::snapshot(connection, job.resource);
        if (!resource->reference_sha256.empty() && (resource->reference_sha256 != attempt.checksum || resource->reference_size != attempt.size_bytes)) {
            (void)co_await jobs::finish(connection, job, "failed", "content_conflict", "content");
            co_return false;
        }
        // 第一份完整合法快照在副作用之前绑定参考；不依赖任何目标已经完成。
        (void)co_await jobs::query(connection, jobs::table(job.resource.owner) + "/transfer_reference", attempt.checksum, attempt.size_bytes, job.resource.resource_id, job.version.content_generation, job.source_revision, attempt.checksum, attempt.size_bytes);
        (void)co_await jobs::query(connection, "resource_transfer_tasks/attempt", attempt.path, attempt.checksum, attempt.size_bytes, job.task_id, job.claim_generation);
        co_await jobs::recordRelated(connection, *actual, attempt.related_mask);
        if (job.destination != model::ResourceDestination::Local) {
            (void)co_await jobs::query(connection, "resource_cloud_receipts/transfer_attempt", attempt.path, attempt.checksum, attempt.size_bytes, job.version.content_generation, job.version.target_revision, job.task_id, job.claim_generation, std::string(model::resourceOwnerName(job.resource.owner)), job.resource.resource_id, std::string(model::resourceDestinationName(job.destination)), job.native_claim_generation, "rt:" + std::to_string(job.task_id));
        }
        co_return true;
    };
    return jobs::run(pool_, jobs::transaction<bool>(pool_, std::move(work)), true);
}

application::result::ServiceResult<bool> MySqlResourceTransferJobs::complete(const model::ResourceTransferJob &job, model::ResourceTransferReceipt receipt)
{
    return jobs::run(pool_, jobs::complete(pool_, job, std::move(receipt)), true);
}

application::result::ServiceResult<bool> MySqlResourceTransferJobs::fail(const model::ResourceTransferJob &job, model::ResourceTransferJobFailure failure)
{
    auto work = [job, failure = std::move(failure)](boost::mysql::pooled_connection &connection) -> boost::asio::awaitable<bool> {
        if (failure.result_code.empty() || !jobs::code(failure.result_code) || !jobs::code(failure.error_category)) throw std::invalid_argument("invalid failure code");
        const bool digital_album = failure.source_restricted && (failure.result_code == "440016" || failure.result_code == "digital_album");
        if (failure.related_mask || digital_album) co_await jobs::lockRelatedScope(connection, job, digital_album);
        if (!(co_await jobs::current(connection, job, false))) co_return false;
        const auto actual = co_await jobs::task(connection, job.task_id);
        if (failure.source_restricted && job.resource.owner == model::ResourceOwner::Song && co_await jobs::current(connection, job)) {
            if (failure.related_mask) {
                co_await jobs::recordRelated(connection, *actual, *failure.related_mask);
                co_await jobs::enqueueRelated(connection, *actual);
            }
            if (digital_album)
                (void)co_await jobs::query(connection, "song_resources/transfer_digital_album", failure.result_code, job.resource.resource_id, job.version.content_generation, job.source_revision);
            else
                (void)co_await jobs::query(connection, "song_resources/transfer_restricted", failure.result_code, job.resource.resource_id, job.version.content_generation, job.source_revision);
            co_return co_await jobs::finish(connection, job, "skipped", failure.result_code, "copyright");
        }
        if (failure.uncertain) co_return co_await jobs::finish(connection, job, "uncertain", failure.result_code, failure.error_category, true);
        if (failure.result_code == "waiting_input") co_return co_await jobs::finish(connection, job, "waiting_input", "waiting_input", "");
        const auto stop = !(co_await jobs::current(connection, job));
        const bool retry = failure.retryable && !stop && actual->attempt_count <= actual->policy.retries;
        if (retry) (void)co_await jobs::query(connection, "resource_transfer_tasks/clear_attempt", job.task_id, job.claim_generation);
        co_return co_await jobs::finish(connection, job, actual->cancel_requested ? "cancelled" : retry ? "retry" :
                                                                                              stop      ? "blocked" :
                                                                                                          "failed",
                                        actual->cancel_requested ? "cancelled" : failure.result_code,
                                        failure.error_category);
    };
    return jobs::run(pool_, jobs::transaction<bool>(pool_, std::move(work)), true);
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
