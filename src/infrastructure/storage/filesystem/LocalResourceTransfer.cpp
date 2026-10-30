#include "crawler/infrastructure/storage/filesystem/LocalResourceTransfer.h"

#include "crawler/infrastructure/storage/cloud/CloudFileSupport.h"
#include "crawler/infrastructure/storage/filesystem/ResourceFileDigest.h"

#include <array>
#include <fstream>
#include <stdexcept>
#include <thread>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::storage::filesystem {
namespace {
namespace model = application::model;
namespace result = application::result;

// 本地完整文件发布和 DB 确认分开，析构默认保留已发布未确认文件。
class LocalWrite final : public application::ports::ResourceTransferWrite {
public:
    // 会话拥有 staging；快照租约保持至物理提交结束。
    LocalWrite(std::unique_ptr<application::ports::SongResourceBlobWriteSession> session, std::shared_ptr<const model::ResourceContentSnapshot> snapshot) : session_(std::move(session)), snapshot_(std::move(snapshot))
    {}
    // 异常路径不让底层会话误删已发布对象。
    ~LocalWrite() override
    {
        if (!settled_) settle(published_ ? model::ResourceTransferSettlement::Defer : model::ResourceTransferSettlement::Abort);
    }
    // 实际路径在创建会话时即固定。
    model::ResourceTransferAttempt attempt() const override
    {
        return {session_->plannedPath(), snapshot_->checksum(), snapshot_->sizeBytes()};
    }
    // 文件自身再次计算 SHA-256，拒绝读取时被替换的快照。
    result::ServiceResult<model::ResourceTransferReceipt> publish(const model::ResourceTransferContext &context) override
    {
        using Result = result::ServiceResult<model::ResourceTransferReceipt>;
        if (context.stopped()) return Result::failure(result::ServiceErrorType::ExternalApiError, "cancelled");
        const auto saved = session_->commit();
        published_ = saved.ok;
        if (!saved.ok) return Result::failure(result::ServiceErrorType::InternalError, "local_publish_failed");
        if (saved.local_path != session_->plannedPath() || saved.checksum != snapshot_->checksum() || saved.size_bytes != snapshot_->sizeBytes()) return Result::failure(result::ServiceErrorType::Conflict, "content_conflict");
        model::ResourceTransferReceipt receipt;
        receipt.path = saved.local_path;
        receipt.checksum = saved.checksum;
        receipt.size_bytes = saved.size_bytes;
        receipt.adapter = "filesystem";
        receipt.verification_method = "local_sha256";
        receipt.verification = model::ResourceVerification::Sha256Verified;
        return Result::success(std::move(receipt));
    }
    // noexcept 边界保守保留，数据库确认后的清理失败不回滚其他目标。
    void settle(model::ResourceTransferSettlement settlement) noexcept override
    {
        if (settled_) return;
        try {
            if (settlement == model::ResourceTransferSettlement::Accept)
                session_->accept();
            else if (settlement == model::ResourceTransferSettlement::Defer)
                session_->defer();
            else
                session_->abort();
            settled_ = true;
        } catch (...) {
            try {
                session_->defer();
                settled_ = true;
            } catch (...) {}
        }
    }

private:
    std::unique_ptr<application::ports::SongResourceBlobWriteSession> session_; // 独占临时对象和日志
    std::shared_ptr<const model::ResourceContentSnapshot> snapshot_;            // 不可变输入租约
    bool published_ = false;                                                    // 已有正式完整文件
    bool settled_ = false;                                                      // 已确认或交给恢复
};

// 输入路径即使相对安全，也必须在规范根内，不能穿过已有符号链接。
bool inside(const std::filesystem::path &root, const std::filesystem::path &path)
{
    std::error_code error;
    const auto base = std::filesystem::weakly_canonical(std::filesystem::absolute(root), error);
    if (error) return false;
    const auto full = std::filesystem::weakly_canonical(std::filesystem::absolute(path), error);
    return !error && cloud::safeRelativePath(full.lexically_relative(base).generic_string());
}
} // namespace

LocalResourceTransfer::LocalResourceTransfer(std::shared_ptr<FileStorage> files) : files_(std::move(files))
{
    if (!files_) throw std::invalid_argument("local transfer store required");
}

application::result::ServiceResult<application::model::ResourceAttemptInspection> LocalResourceTransfer::inspect(const model::ResourceTransferJob &job, const model::ResourceTransferContext &context)
{
    using Result = result::ServiceResult<model::ResourceAttemptInspection>;
    if (job.adapter != "filesystem" || job.destination != model::ResourceDestination::Local || !cloud::safeRelativePath(job.attempt_path)) return Result::failure(result::ServiceErrorType::InvalidParam, "invalid_attempt_path");
    const auto file = files_->getFullPath(job.attempt_path);
    if (!inside(files_->getFullPath(""), file)) return Result::failure(result::ServiceErrorType::InvalidParam, "invalid_attempt_path");
    std::error_code error;
    const bool exists = std::filesystem::exists(file, error);
    if (error) return Result::failure(result::ServiceErrorType::InternalError, "local_inspection_unknown");
    if (!exists) return Result::success({model::ResourceAttemptObservation::Absent, {}});
    const auto digest = digestResourceFile(file, job.policy.max_bytes, context);
    if (!digest.isSuccess()) return Result::failure(digest.error(), digest.message());
    model::ResourceAttemptInspection inspected;
    inspected.observation = digest.data()->sha256 == job.attempt_checksum && digest.data()->size_bytes == job.attempt_size_bytes ? model::ResourceAttemptObservation::Matched : model::ResourceAttemptObservation::Conflict;
    if (inspected.observation == model::ResourceAttemptObservation::Matched) {
        inspected.receipt.path = job.attempt_path;
        inspected.receipt.checksum = digest.data()->sha256;
        inspected.receipt.size_bytes = digest.data()->size_bytes;
        inspected.receipt.adapter = "filesystem";
        inspected.receipt.verification_method = "local_sha256";
        inspected.receipt.verification = model::ResourceVerification::Sha256Verified;
    }
    return Result::success(std::move(inspected));
}

application::result::ServiceResult<std::shared_ptr<application::ports::ResourceTransferWrite>> LocalResourceTransfer::prepare(const model::ResourceTransferJob &job, std::shared_ptr<const model::ResourceContentSnapshot> snapshot, const model::ResourceTransferContext &context)
{
    using Result = result::ServiceResult<std::shared_ptr<application::ports::ResourceTransferWrite>>;
    if (!snapshot || !cloud::safeRelativePath(snapshot->relativePath()) || job.destination != model::ResourceDestination::Local || job.adapter != "filesystem") return Result::failure(result::ServiceErrorType::InvalidParam, "invalid_local_snapshot");
    model::SongResourceBlobStreamSaveRequest request;
    request.resource_id = job.resource.resource_id;
    request.claim_generation = job.native_claim_generation;
    request.owner_kind = model::resourceOwnerName(job.resource.owner);
    request.require_database_ack = true;
    request.relative_path_hint = "resource-data/" + std::string(model::resourceOwnerName(job.resource.owner)) + "/" + std::to_string(job.resource.resource_id) + "/g" + std::to_string(job.version.content_generation) + "/task-" + std::to_string(job.task_id) + "/" + snapshot->relativePath();
    if (!inside(files_->getFullPath(""), files_->getFullPath(request.relative_path_hint))) return Result::failure(result::ServiceErrorType::InvalidParam, "invalid_local_path");
    auto session = files_->beginSongResourceBlobWrite(std::move(request));
    if (!session || session->plannedPath().empty()) return Result::failure(result::ServiceErrorType::InternalError, "local_staging_failed");
    try {
        std::ifstream input(snapshot->filePath(), std::ios::binary);
        if (!input) return Result::failure(result::ServiceErrorType::InternalError, "snapshot_open_failed");
        std::array<char, 64 * 1024> chunk {};
        std::uint64_t total = 0;
        std::string error;
        const auto started = std::chrono::steady_clock::now();
        while (input) {
            if (context.stopped()) return Result::failure(result::ServiceErrorType::ExternalApiError, context.remaining() == 0 ? "timeout" : "cancelled");
            input.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
            const auto count = input.gcount();
            if (count <= 0) break;
            total += static_cast<std::uint64_t>(count);
            if (total > snapshot->sizeBytes() || total > job.policy.max_bytes) return Result::failure(result::ServiceErrorType::Conflict, "snapshot_changed");
            if (!session->write(std::string_view(chunk.data(), static_cast<std::size_t>(count)), error)) return Result::failure(result::ServiceErrorType::InternalError, "local_staging_failed");
            if (job.policy.bytes_per_second > 0) {
                const auto due = started + std::chrono::milliseconds(static_cast<std::int64_t>(total * 1000 / job.policy.bytes_per_second));
                while (std::chrono::steady_clock::now() < due) {
                    if (context.stopped()) return Result::failure(result::ServiceErrorType::ExternalApiError, "cancelled");
                    std::this_thread::sleep_for(std::chrono::milliseconds(10));
                }
            }
        }
        if (input.bad() || total != snapshot->sizeBytes()) return Result::failure(result::ServiceErrorType::Conflict, "snapshot_changed");
        return Result::success(std::make_shared<LocalWrite>(std::move(session), std::move(snapshot)));
    } catch (const std::exception &) {
        return Result::failure(result::ServiceErrorType::InternalError, "local_staging_failed");
    }
}

} // namespace orchestrator::modules::crawler::infrastructure::storage::filesystem
