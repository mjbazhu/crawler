#include "crawler/infrastructure/storage/cloud/CloudTransferSupport.h"

#include "crawler/infrastructure/storage/cloud/CloudFileSupport.h"

#include <algorithm>
#include <filesystem>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::storage::cloud {
namespace {
namespace model = application::model;
namespace result = application::result;
using Error = result::ServiceErrorType;
// 受控布局不依赖原始 URL、认证或源站文件名。
std::string resourcePrefix(const model::ResourceTransferJob &job)
{
    return "resources/v1/" + std::string(model::resourceOwnerName(job.resource.owner)) + '/' + std::to_string(job.resource.resource_id) + "/g" + std::to_string(job.version.content_generation) + '/';
}
// 只接受实际标准小写摘要。
bool validSha(std::string_view value)
{
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); });
}
// 每个写会话只由一个任务线程使用；不同会话共享只读后端，彼此无可变状态。
class CloudWrite final : public application::ports::ResourceTransferWrite {
public:
    // 快照租约与实际摘要冻结到本次尝试。
    CloudWrite(model::ResourceTransferAttempt attempt, std::shared_ptr<const model::ResourceContentSnapshot> snapshot, filesystem::ResourceFileDigest digest, CloudPublisher publish) : attempt_(std::move(attempt)), snapshot_(std::move(snapshot)), digest_(std::move(digest)), publish_(std::move(publish))
    {}
    // 精确路径先交给 application 持久登记。
    model::ResourceTransferAttempt attempt() const override
    {
        return attempt_;
    }
    // 不重复发布同一会话，未知异常保留对象意图。
    result::ServiceResult<model::ResourceTransferReceipt> publish(const model::ResourceTransferContext &context) override
    {
        using Result = result::ServiceResult<model::ResourceTransferReceipt>;
        if (settled_ || published_) return Result::failure(Error::CommitUncertain, "cloud_session_already_published");
        if (context.stopped()) return Result::failure(Error::ExternalApiError, context.remaining() == 0 ? "timeout" : "cancelled");
        published_ = true;
        try {
            return publish_(attempt_, *snapshot_, digest_, context);
        } catch (...) {
            return Result::failure(Error::CommitUncertain, "cloud_publish_uncertain");
        }
    }
    // 远端对象不由析构删除；回执/任务意图保留给显式核对和后续孤儿治理。
    void settle(model::ResourceTransferSettlement) noexcept override
    {
        settled_ = true;
    }

private:
    model::ResourceTransferAttempt attempt_;                         // 本次唯一远端对象
    std::shared_ptr<const model::ResourceContentSnapshot> snapshot_; // 防止源快照提前回收
    filesystem::ResourceFileDigest digest_;                          // 实测 SHA-256 与可用兼容 hash
    CloudPublisher publish_;                                         // 具体后端及其不可变配置
    bool published_ = false;                                         // 仅本会话线程访问
    bool settled_ = false;                                           // 防止补偿后重用
};
} // namespace

application::result::ServiceResult<std::shared_ptr<application::ports::ResourceTransferWrite>> prepareCloudTransfer(const model::ResourceTransferJob &job, std::shared_ptr<const model::ResourceContentSnapshot> snapshot, model::ResourceDestination provider, std::string prefix, CloudPublisher publish, const model::ResourceTransferContext &context)
{
    using Result = result::ServiceResult<std::shared_ptr<application::ports::ResourceTransferWrite>>;
    if (job.destination != provider || provider == model::ResourceDestination::Local || job.task_id == 0 || job.claim_generation == 0 || job.resource.resource_id == 0 || job.version.content_generation == 0 || model::resourceOwnerName(job.resource.owner).empty() || !snapshot || !publish || prefix.empty()) return Result::failure(Error::InvalidParam, "invalid_cloud_task");
    if (context.stopped()) return Result::failure(Error::ExternalApiError, context.remaining() == 0 ? "timeout" : "cancelled");
    if (!validSha(snapshot->checksum()) || !safeRelativePath(snapshot->relativePath()) || snapshot->sizeBytes() == 0 || snapshot->sizeBytes() > job.policy.max_bytes) return Result::failure(Error::InvalidParam, "invalid_cloud_snapshot");
    const auto digest = filesystem::digestResourceFile(std::filesystem::u8path(snapshot->filePath()), job.policy.max_bytes, context);
    if (!digest.isSuccess()) return Result::failure(digest.error(), digest.message());
    if (digest.data()->sha256 != snapshot->checksum() || digest.data()->size_bytes != snapshot->sizeBytes()) return Result::failure(Error::Conflict, "snapshot_changed");
    const auto utf8_leaf = std::filesystem::u8path(snapshot->filePath()).filename().generic_u8string();
    const std::string leaf(utf8_leaf.begin(), utf8_leaf.end());
    if (!safeRelativePath(leaf)) return Result::failure(Error::InvalidParam, "invalid_cloud_snapshot_name");
    // 原生工具保留本地文件名，独占目录隔离不同认领/版本，晚到写入不能覆盖新尝试。
    const auto path = prefix + resourcePrefix(job) + "t" + std::to_string(job.task_id) + "-c" + std::to_string(job.claim_generation) + '-' + uniqueToken() + '/' + leaf;
    if (path.size() > 4096) return Result::failure(Error::InvalidParam, "cloud_path_too_long");
    return Result::success(std::make_shared<CloudWrite>(model::ResourceTransferAttempt {path, digest.data()->sha256, digest.data()->size_bytes}, std::move(snapshot), *digest.data(), std::move(publish)));
}

std::optional<std::string> cloudAttemptRelativePath(const model::ResourceTransferJob &job, std::string_view prefix)
{
    if (!job.attempt_path.starts_with(prefix) || !validSha(job.attempt_checksum) || job.attempt_size_bytes == 0 || job.attempt_size_bytes > job.policy.max_bytes) return std::nullopt;
    const auto relative = job.attempt_path.substr(prefix.size());
    if (!safeRelativePath(relative) || !relative.starts_with(resourcePrefix(job))) return std::nullopt;
    return relative;
}

model::ResourceTransferReceipt cloudTransferReceipt(const model::ResourceTransferAttempt &attempt, std::string adapter, std::string algorithm, std::string value, std::string method)
{
    model::ResourceTransferReceipt receipt;
    receipt.path = attempt.path;
    receipt.checksum = attempt.checksum;
    receipt.size_bytes = attempt.size_bytes;
    receipt.adapter = std::move(adapter);
    receipt.verification = algorithm == "sha256" ? model::ResourceVerification::Sha256Verified : model::ResourceVerification::HashMatched;
    receipt.remote_hash_algorithm = std::move(algorithm);
    receipt.remote_hash_value = std::move(value);
    receipt.verification_method = std::move(method);
    return receipt;
}

} // namespace orchestrator::modules::crawler::infrastructure::storage::cloud
