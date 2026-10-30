#include "MySqlResourceTransferJobsSupport.h"
#include "MySqlResourceTransferRelated.h"

#include <algorithm>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::transfer_jobs {

bool safePath(std::string_view path)
{
    if (path.empty() || path.size() > 4096 || path.find("://") != std::string_view::npos || path.find('\\') != std::string_view::npos) return false;
    if (std::any_of(path.begin(), path.end(), [](unsigned char ch) { return ch < 32 || ch == 127; })) return false;
    std::size_t start = 0;
    while (start <= path.size()) {
        const auto end = path.find('/', start), length = (end == std::string_view::npos ? path.size() : end) - start;
        if (path.substr(start, length) == ".." || path.substr(start, length) == ".") return false;
        if (end == std::string_view::npos) break;
        start = end + 1;
    }
    return true;
}

boost::asio::awaitable<bool> complete(std::shared_ptr<MySqlConnectionPool> pool, model::ResourceTransferJob job, model::ResourceTransferReceipt receipt)
{
    if (!safePath(receipt.path) || !hash(receipt.checksum) || receipt.size_bytes == 0 || receipt.verification_method.empty() || !code(receipt.verification_method)) throw std::invalid_argument("invalid receipt");
    const bool local = job.destination == model::ResourceDestination::Local;
    if (job.policy.strict_sha256 && receipt.verification != model::ResourceVerification::Sha256Verified) throw std::invalid_argument("strict verification required");
    if (!model::supportsResourceTransferAdapter(job.destination, receipt.adapter) || receipt.adapter != job.adapter) throw std::invalid_argument("receipt adapter mismatch");
    if (local) {
        if (receipt.verification != model::ResourceVerification::Sha256Verified || receipt.verification_method != "local_sha256") throw std::invalid_argument("unverified local receipt");
    } else {
        if (receipt.verification == model::ResourceVerification::Sha256Verified) {
            if (receipt.remote_hash_algorithm != "sha256" || receipt.remote_hash_value != receipt.checksum) throw std::invalid_argument("missing remote sha256 evidence");
        } else if (receipt.verification == model::ResourceVerification::HashMatched) {
            if (receipt.adapter != "rclone") throw std::invalid_argument("adapter requires remote sha256 evidence");
            const auto parsed = model::parseLegacyResourceChecksum("rclone:md5:" + receipt.remote_hash_value);
            if (receipt.remote_hash_algorithm != "md5" || parsed.algorithm != "md5") throw std::invalid_argument("missing remote md5 evidence");
        } else
            throw std::invalid_argument("unverified cloud receipt");
    }
    auto work = [job, receipt = std::move(receipt), local](boost::mysql::pooled_connection &connection) -> boost::asio::awaitable<bool> {
        co_await lockRelatedScope(connection, job);
        if (!(co_await current(connection, job))) {
            if (co_await current(connection, job, false)) {
                const auto actual = co_await task(connection, job.task_id);
                if (actual->cancel_requested && !actual->attempt_path.empty())
                    (void)co_await finish(connection, job, "uncertain", "cancelled_reconcile_required", "cancelled", true);
                else
                    (void)co_await finish(connection, job, actual->cancel_requested ? "cancelled" : "blocked", actual->cancel_requested ? "cancelled" : "version_changed", "");
            }
            co_return false;
        }
        const auto actual = co_await task(connection, job.task_id);
        const auto resource = co_await snapshot(connection, job.resource);
        if (actual->policy.strict_sha256 && receipt.verification != model::ResourceVerification::Sha256Verified) {
            (void)co_await finish(connection, job, "uncertain", "strict_verification_required", "content", true);
            co_return false;
        }
        if (actual->attempt_path != receipt.path || actual->attempt_checksum != receipt.checksum || actual->attempt_size_bytes != receipt.size_bytes || resource->reference_sha256 != receipt.checksum || resource->reference_size != receipt.size_bytes) {
            (void)co_await finish(connection, job, "uncertain", "content_conflict", "content", true);
            co_return false;
        }
        // 与音频确认同事务受理关联子操作，崩溃不能留下成功音频但漏排关联资源。
        co_await enqueueRelated(connection, *actual);
        boost::mysql::results changed;
        const auto batch = "rt:" + std::to_string(job.task_id);
        if (local) {
            changed = co_await query(connection, table(job.resource.owner) + "/transfer_complete", receipt.path, receipt.checksum, receipt.size_bytes, job.version.content_generation, job.version.target_revision, job.resource.resource_id, job.native_claim_generation, batch, job.version.content_generation, job.source_revision);
        } else {
            const std::string verification = receipt.verification == model::ResourceVerification::Sha256Verified ? "sha256_verified" : "hash_matched";
            changed = co_await query(connection, "resource_cloud_receipts/transfer_complete", receipt.uploaded ? 1 : 0, receipt.path, receipt.checksum, job.version.content_generation, job.version.target_revision, receipt.path, receipt.checksum, receipt.size_bytes, job.version.content_generation, job.version.target_revision, verification, receipt.verification_method, receipt.remote_hash_algorithm, receipt.remote_hash_value, receipt.adapter, std::string(model::resourceOwnerName(job.resource.owner)), job.resource.resource_id, std::string(model::resourceDestinationName(job.destination)), job.native_claim_generation, batch, job.version.content_generation, job.version.target_revision);
        }
        if (changed.affected_rows() != 1) throw std::logic_error("native receipt changed");
        co_return co_await finish(connection, job, "succeeded", "ok", "");
    };
    co_return co_await transaction<bool>(std::move(pool), std::move(work));
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::transfer_jobs
