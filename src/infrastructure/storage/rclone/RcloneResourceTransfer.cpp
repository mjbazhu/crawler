#include "crawler/infrastructure/storage/rclone/RcloneResourceTransfer.h"

#include "RcloneTransferProtocol.h"
#include "crawler/infrastructure/storage/cloud/CloudFileSupport.h"
#include "crawler/infrastructure/storage/cloud/CloudTransferSupport.h"
#include "crawler/infrastructure/storage/rclone/RcloneRcSupport.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::storage::rclone {
namespace model = application::model;
namespace result = application::result;

RcloneResourceTransfer::RcloneResourceTransfer(std::shared_ptr<http::client::HttpClient> client, RcloneConfig config, model::ResourceDestination provider) : provider_(provider), client_(std::move(client)), config_(detail::normalizeCloudConfig(std::move(config)))
{
    if (!client_ || !model::supportsResourceTransferAdapter(provider_, "rclone") || config_.api_max_response_bytes == 0 || config_.api_max_response_bytes > 8 * 1024 * 1024) throw std::invalid_argument("invalid rclone transfer configuration");
}

result::ServiceResult<model::ResourceAttemptInspection> RcloneResourceTransfer::inspect(const model::ResourceTransferJob &job, const model::ResourceTransferContext &context)
{
    if (job.adapter != "rclone" || job.destination != provider_ || !cloud::cloudAttemptRelativePath(job, config_.target_fs + '/')) return result::ServiceResult<model::ResourceAttemptInspection>::failure(result::ServiceErrorType::InvalidParam, "invalid_cloud_attempt");
    return transfer::verify(*client_, config_, {job.attempt_path, job.attempt_checksum, job.attempt_size_bytes}, {}, context, false, job.policy.bytes_per_second);
}

result::ServiceResult<std::shared_ptr<application::ports::ResourceTransferWrite>> RcloneResourceTransfer::prepare(const model::ResourceTransferJob &job, std::shared_ptr<const model::ResourceContentSnapshot> snapshot, const model::ResourceTransferContext &context)
{
    if (job.adapter != "rclone") return result::ServiceResult<std::shared_ptr<application::ports::ResourceTransferWrite>>::failure(result::ServiceErrorType::InvalidParam, "adapter_changed");
    return cloud::prepareCloudTransfer(job, std::move(snapshot), provider_, config_.target_fs + '/', [client = client_, config = config_, rate = job.policy.bytes_per_second, strict = job.policy.strict_sha256](const auto &attempt, const auto &file, const auto &digest, const auto &budget) {
        auto expected = digest;
        // 严格策略不接受远端 MD5 快捷确认，必要时直接回读精确对象 SHA-256。
        if (strict) expected.md5.clear();
        return transfer::publish(*client, config, attempt, file, expected, budget, rate); }, context);
}

namespace transfer {
result::ServiceResult<model::ResourceTransferReceipt> publish(http::client::HttpClient &client, const RcloneConfig &config, const model::ResourceTransferAttempt &attempt, const model::ResourceContentSnapshot &snapshot, const filesystem::ResourceFileDigest &digest, const model::ResourceTransferContext &context, std::uint64_t bytes_per_second)
{
    using Result = result::ServiceResult<model::ResourceTransferReceipt>;
    using Error = result::ServiceErrorType;
    const auto observed = verify(client, config, attempt, digest.md5, context, true, bytes_per_second);
    if (!observed.isSuccess()) return Result::failure(Error::CommitUncertain, "rclone_verification_unknown");
    if (observed.data()->observation == model::ResourceAttemptObservation::Matched) return Result::success(observed.data()->receipt);
    if (observed.data()->observation == model::ResourceAttemptObservation::Conflict) return Result::failure(Error::Conflict, "cloud_content_mismatch");
    if (observed.data()->observation != model::ResourceAttemptObservation::Absent) return Result::failure(Error::CommitUncertain, "rclone_verification_unknown");
    if (context.stopped()) return Result::failure(Error::ExternalApiError, context.remaining() == 0 ? "timeout" : "cancelled");
    const auto relative = attempt.path.substr(config.target_fs.size() + 1);
    const auto slash = relative.find_last_of('/');
    const auto boundary = "crawler-" + cloud::uniqueToken();
    auto body = multipart(snapshot, relative.substr(slash + 1), boundary, context, bytes_per_second);
    nlohmann::json call_config {{"Retries", 1}, {"LowLevelRetries", 1}};
    if (bytes_per_second) call_config["BwLimitFile"] = std::to_string(bytes_per_second) + 'B';
    http::client::HttpRequestOptions options {std::chrono::milliseconds(context.remaining()), config.api_max_response_bytes};
    const auto response = client.streamPostWithOptions(config.rc_url + "/operations/uploadfile?fs=" + detail::queryEncode(config.target_fs) + "&_config=" + detail::queryEncode(call_config.dump()) + "&remote=" + detail::queryEncode(relative.substr(0, slash)), std::move(body), detail::rcHeaders(config, "multipart/form-data; boundary=" + boundary), options);
    if (!parse(response).isSuccess()) return Result::failure(response.request_started ? Error::CommitUncertain : Error::ExternalApiError, "rclone_upload_uncertain");
    const auto verified = verify(client, config, attempt, digest.md5, context, false, bytes_per_second);
    if (!verified.isSuccess() || verified.data()->observation != model::ResourceAttemptObservation::Matched) return Result::failure(Error::CommitUncertain, verified.isSuccess() && verified.data()->observation == model::ResourceAttemptObservation::Conflict ? "cloud_content_mismatch" : "rclone_verification_unknown");
    auto receipt = verified.data()->receipt;
    receipt.uploaded = true;
    return Result::success(std::move(receipt));
}
} // namespace transfer
} // namespace orchestrator::modules::crawler::infrastructure::storage::rclone
