#include "crawler/infrastructure/storage/baidupcs/BaiduResourceTransfer.h"

#include "BaiduTransferProtocol.h"
#include "crawler/infrastructure/storage/cloud/CloudTransferSupport.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::storage::baidupcs {
namespace model = application::model;
namespace result = application::result;

BaiduResourceTransfer::BaiduResourceTransfer(BaiduPcsGoConfig config, execution::process::CommandRunner runner, std::uint64_t temporary_bytes) : config_(transfer::normalize(std::move(config))), runner_(std::move(runner)), quota_(std::make_shared<transfer::ReadbackQuota>(temporary_bytes))
{
    if (!runner_) runner_ = execution::process::runCommand;
    if (temporary_bytes < 65536 || temporary_bytes > (std::uint64_t {1} << 40)) throw std::invalid_argument("invalid Baidu readback quota");
}

result::ServiceResult<model::ResourceAttemptInspection> BaiduResourceTransfer::inspect(const model::ResourceTransferJob &job, const model::ResourceTransferContext &context)
{
    if (job.adapter != "baidupcs" || job.destination != model::ResourceDestination::Baidu || !cloud::cloudAttemptRelativePath(job, config_.cloud_root == "/" ? "/" : config_.cloud_root + '/')) return result::ServiceResult<model::ResourceAttemptInspection>::failure(result::ServiceErrorType::InvalidParam, "invalid_cloud_attempt");
    if (job.policy.bytes_per_second) return result::ServiceResult<model::ResourceAttemptInspection>::failure(result::ServiceErrorType::InvalidParam, "baidu_task_rate_unsupported");
    if (job.attempt_size_bytes > quota_->limit - 65536) return result::ServiceResult<model::ResourceAttemptInspection>::failure(result::ServiceErrorType::InvalidParam, "baidu_readback_quota_exceeded");
    return transfer::verify(config_, runner_, quota_, {job.attempt_path, job.attempt_checksum, job.attempt_size_bytes}, context, false);
}

result::ServiceResult<std::shared_ptr<application::ports::ResourceTransferWrite>> BaiduResourceTransfer::prepare(const model::ResourceTransferJob &job, std::shared_ptr<const model::ResourceContentSnapshot> snapshot, const model::ResourceTransferContext &context)
{
    if (job.adapter != "baidupcs") return result::ServiceResult<std::shared_ptr<application::ports::ResourceTransferWrite>>::failure(result::ServiceErrorType::InvalidParam, "adapter_changed");
    if (job.policy.bytes_per_second) return result::ServiceResult<std::shared_ptr<application::ports::ResourceTransferWrite>>::failure(result::ServiceErrorType::InvalidParam, "baidu_task_rate_unsupported");
    if (snapshot && snapshot->sizeBytes() > quota_->limit - 65536) return result::ServiceResult<std::shared_ptr<application::ports::ResourceTransferWrite>>::failure(result::ServiceErrorType::InvalidParam, "baidu_readback_quota_exceeded");
    const auto prefix = config_.cloud_root == "/" ? "/" : config_.cloud_root + '/';
    return cloud::prepareCloudTransfer(job, std::move(snapshot), model::ResourceDestination::Baidu, prefix, [config = config_, runner = runner_, quota = quota_](const auto &attempt, const auto &file, const auto &, const auto &budget) { return transfer::publish(config, runner, quota, attempt, file, budget); }, context);
}

namespace transfer {
result::ServiceResult<model::ResourceTransferReceipt> publish(const BaiduPcsGoConfig &config, const process::CommandRunner &runner, std::shared_ptr<ReadbackQuota> quota, const model::ResourceTransferAttempt &attempt, const model::ResourceContentSnapshot &snapshot, const model::ResourceTransferContext &context)
{
    using Result = result::ServiceResult<model::ResourceTransferReceipt>;
    using Error = result::ServiceErrorType;
    const auto observed = verify(config, runner, quota, attempt, context, true);
    if (!observed.isSuccess()) return Result::failure(Error::CommitUncertain, "baidu_verification_unknown");
    if (observed.data()->observation == model::ResourceAttemptObservation::Matched) return Result::success(observed.data()->receipt);
    if (observed.data()->observation == model::ResourceAttemptObservation::Conflict) return Result::failure(Error::Conflict, "cloud_content_mismatch");
    if (observed.data()->observation != model::ResourceAttemptObservation::Absent) return Result::failure(Error::CommitUncertain, "baidu_verification_unknown");
    if (context.stopped()) return Result::failure(Error::ExternalApiError, context.remaining() == 0 ? "timeout" : "cancelled");
    const auto parent = attempt.path.substr(0, attempt.path.find_last_of('/'));
    const auto uploaded = run(config, runner, {"upload", "-p", std::to_string((std::clamp)(config.upload_parallel, 1, 16)), "-l", "1", "--retry", "0", "--policy", "skip", snapshot.filePath(), parent}, context);
    if (uploaded.exit_code != 0 || uploaded.cancelled || uploaded.timed_out || context.stopped()) return Result::failure(Error::CommitUncertain, "baidu_upload_uncertain");
    const auto verified = verify(config, runner, quota, attempt, context, false);
    if (!verified.isSuccess() || verified.data()->observation != model::ResourceAttemptObservation::Matched) return Result::failure(Error::CommitUncertain, verified.isSuccess() && verified.data()->observation == model::ResourceAttemptObservation::Conflict ? "cloud_content_mismatch" : "baidu_verification_unknown");
    auto receipt = verified.data()->receipt;
    receipt.uploaded = true;
    return Result::success(std::move(receipt));
}
} // namespace transfer
} // namespace orchestrator::modules::crawler::infrastructure::storage::baidupcs
