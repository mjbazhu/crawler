#include "BaiduTransferProtocol.h"

#include "crawler/infrastructure/storage/cloud/CloudFileSupport.h"
#include "crawler/infrastructure/storage/cloud/CloudTransferSupport.h"
#include "crawler/infrastructure/storage/filesystem/ResourceFileDigest.h"

#include <filesystem>
#include <stdexcept>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::storage::baidupcs::transfer {
namespace {
// 回读目录与预留一同托管，只清理自己创建且仍在固定父目录内的内容。
class ReadbackWorkspace {
public:
    // 一份数据加有界客户端状态开销，避免并发任务超过本端准入上限。
    ReadbackWorkspace(const std::filesystem::path &root, std::shared_ptr<ReadbackQuota> quota, std::uint64_t bytes) : quota_(std::move(quota)), bytes_(bytes + 65536)
    {
        auto used = quota_->reserved.load();
        do {
            if (bytes_ > quota_->limit || used > quota_->limit - bytes_) throw std::runtime_error("readback quota exhausted");
        } while (!quota_->reserved.compare_exchange_weak(used, used + bytes_));
        try {
            const auto base = std::filesystem::weakly_canonical(std::filesystem::absolute(root));
            parent_ = std::filesystem::weakly_canonical(base / ".resource-cloud-readback");
            if (!cloud::safeRelativePath(pathUtf8(parent_.lexically_relative(base)))) throw std::runtime_error("readback parent escaped root");
            std::filesystem::create_directories(parent_);
            directory_ = parent_ / cloud::uniqueToken();
            if (!std::filesystem::create_directory(directory_)) throw std::runtime_error("readback directory unavailable");
        } catch (...) {
            quota_->reserved.fetch_sub(bytes_);
            throw;
        }
    }
    // runner 返回时进程已经结束，再删除本次临时文件。
    ~ReadbackWorkspace()
    {
        std::error_code ignored;
        if (directory_.parent_path() == parent_ && std::filesystem::weakly_canonical(directory_.parent_path(), ignored) == parent_) std::filesystem::remove_all(directory_, ignored);
        quota_->reserved.fetch_sub(bytes_);
    }
    // 只将自己的目录传给客户端。
    const std::filesystem::path &directory() const
    {
        return directory_;
    }
    // 外部进程的磁盘写入每次取消轮询检查；拒绝链接与超限内容。
    bool overBudget() const
    {
        try {
            std::uint64_t total = 0;
            for (const auto &entry : std::filesystem::recursive_directory_iterator(directory_)) {
                if (entry.is_symlink()) return true;
                if (entry.is_regular_file()) {
                    const auto size = entry.file_size();
                    if (size > bytes_ || total > bytes_ - size) return true;
                    total += size;
                }
            }
            return false;
        } catch (...) {
            return true;
        }
    }

private:
    std::shared_ptr<ReadbackQuota> quota_;     // 本目标共享准入
    std::uint64_t bytes_ = 0;                  // 本次预留量
    std::filesystem::path parent_, directory_; // 固定父目录与独占子目录
};
} // namespace

result::ServiceResult<model::ResourceAttemptInspection> verify(const BaiduPcsGoConfig &config, const process::CommandRunner &runner, std::shared_ptr<ReadbackQuota> quota, const model::ResourceTransferAttempt &attempt, const model::ResourceTransferContext &context, bool allow_absent)
{
    using Result = result::ServiceResult<model::ResourceAttemptInspection>;
    using Observation = model::ResourceAttemptObservation;
    using Error = result::ServiceErrorType;
    const auto observed = meta(config, runner, attempt.path, context);
    if (observed.state == MetaState::Missing) return Result::success({allow_absent ? Observation::Absent : Observation::Unknown, {}});
    if (observed.state == MetaState::Directory || (observed.state == MetaState::File && observed.size != attempt.size_bytes)) return Result::success({Observation::Conflict, {}});
    if (observed.state == MetaState::Authentication) return Result::failure(Error::ExternalApiError, "cloud_auth_required");
    if (observed.state != MetaState::File) return Result::success({Observation::Unknown, {}});
    if (context.stopped()) return Result::failure(Error::ExternalApiError, context.remaining() == 0 ? "timeout" : "cancelled");
    try {
        ReadbackWorkspace workspace(config.local_root, std::move(quota), attempt.size_bytes);
        const auto downloaded = run(config, runner, {"download", "--saveto", pathUtf8(workspace.directory()), "--retry", "0", "-p", "1", "-l", "1", attempt.path}, context, [&workspace] { return workspace.overBudget(); });
        if (downloaded.exit_code != 0 || downloaded.cancelled || downloaded.timed_out || workspace.overBudget() || context.stopped()) return Result::failure(Error::ExternalApiError, "baidu_readback_unknown");
        const auto leaf = attempt.path.substr(attempt.path.find_last_of('/') + 1);
        const auto file = workspace.directory() / std::filesystem::u8path(leaf);
        const auto digest = filesystem::digestResourceFile(file, attempt.size_bytes, context);
        if (!digest.isSuccess()) return Result::failure(digest.error(), digest.message());
        if (digest.data()->size_bytes != attempt.size_bytes || digest.data()->sha256 != attempt.checksum) return Result::success({Observation::Conflict, {}});
        return Result::success({Observation::Matched, cloud::cloudTransferReceipt(attempt, "baidupcs", "sha256", digest.data()->sha256, "remote_readback_sha256")});
    } catch (...) {
        return Result::failure(Error::InternalError, "baidu_readback_unavailable");
    }
}

} // namespace orchestrator::modules::crawler::infrastructure::storage::baidupcs::transfer
