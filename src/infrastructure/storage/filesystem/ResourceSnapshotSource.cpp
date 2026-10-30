#include "crawler/infrastructure/storage/filesystem/ResourceSnapshotSource.h"

#include "ResourceSnapshotSourceSupport.h"
#include "crawler/infrastructure/storage/cloud/CloudFileSupport.h"
#include "crawler/infrastructure/storage/filesystem/MetadataCoverStore.h"
#include "crawler/infrastructure/storage/filesystem/ResourceTokenRefresh.h"

#include <array>
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::storage::filesystem {
namespace snapshot_source {

Workspace::Workspace(std::filesystem::path base, std::shared_ptr<std::array<std::atomic<std::uint64_t>, 3>> reserved, std::size_t index, std::uint64_t bytes) : reserved_(std::move(reserved)), index_(index), bytes_(bytes)
{
    base = std::filesystem::weakly_canonical(std::filesystem::absolute(base));
    parent_ = std::filesystem::weakly_canonical(base / ".resource-snapshots");
    if (!cloud::safeRelativePath(parent_.lexically_relative(base).generic_string())) throw std::invalid_argument("unsafe snapshot root");
    std::filesystem::create_directories(parent_);
    root = parent_ / cloud::uniqueToken();
    if (!std::filesystem::create_directory(root)) throw std::runtime_error("snapshot identity collision");
}
Workspace::~Workspace()
{
    std::error_code error;
    if (root.parent_path() == parent_ && std::filesystem::weakly_canonical(root.parent_path(), error) == parent_) std::filesystem::remove_all(root, error);
    reserved_->at(index_).fetch_sub(bytes_);
}
} // namespace snapshot_source
namespace {
namespace model = application::model;
namespace result = application::result;

// 完整文件与租约共用生命周期；任何消费者结束都不删除另一路的快照。
class SnapshotFile final : public model::ResourceContentSnapshot {
public:
    // 保存独占 workspace 和完整实际摘要。
    SnapshotFile(std::shared_ptr<snapshot_source::Workspace> workspace, std::string relative, ResourceFileDigest digest) : workspace_(std::move(workspace)), relative_(std::move(relative)), digest_(std::move(digest))
    {
        const auto utf8 = (workspace_->root / std::filesystem::u8path(relative_)).generic_u8string();
        path_.assign(reinterpret_cast<const char *>(utf8.data()), utf8.size());
    }
    // 私有文件只在租约存在期间可读。
    const std::string &filePath() const override
    {
        return path_;
    }
    // 目标路径规划不依赖正式 local 回执。
    const std::string &relativePath() const override
    {
        return relative_;
    }
    // 只返回实际读取过的 SHA-256。
    const std::string &checksum() const override
    {
        return digest_.sha256;
    }
    // 返回完整实际大小。
    std::uint64_t sizeBytes() const override
    {
        return digest_.size_bytes;
    }
    // 与已经完整取得的音频快照一起交给持久意图，不再请求listen。
    std::uint8_t relatedMask() const override
    {
        return workspace_->related_mask;
    }

private:
    std::shared_ptr<snapshot_source::Workspace> workspace_; // 唯一目录的共享读租约
    std::string relative_;                                  // 来源路径规划结果
    std::string path_;                                      // 私有物理文件
    ResourceFileDigest digest_;                             // 实测完整内容
};

} // namespace

ResourceSnapshotSource::ResourceSnapshotSource(ResourceSnapshotDependencies dependencies, std::array<std::filesystem::path, 3> staging_roots, std::uint64_t per_target_quota_bytes) : dependencies_(std::move(dependencies)), roots_(std::move(staging_roots)), reserved_(std::make_shared<std::array<std::atomic<std::uint64_t>, 3>>()), quota_(per_target_quota_bytes)
{
    if (!dependencies_.descriptions || quota_ == 0) throw std::invalid_argument("resource snapshot dependencies required");
    if (!dependencies_.token_refresh) dependencies_.token_refresh = std::make_shared<ResourceTokenRefresh>();
    for (auto &reserved : *reserved_) reserved.store(0);
}

application::result::ServiceResult<std::shared_ptr<const application::model::ResourceContentSnapshot>> ResourceSnapshotSource::acquire(const model::ResourceTransferJob &job, const model::ResourceTransferContext &context)
{
    using Result = result::ServiceResult<std::shared_ptr<const model::ResourceContentSnapshot>>;
    if (context.stopped()) return Result::failure(result::ServiceErrorType::ExternalApiError, context.remaining() == 0 ? "timeout" : "cancelled");
    auto bounded_job = job;
    bounded_job.policy.timeout_ms = (std::min)(job.policy.timeout_ms, context.remaining());
    const auto description = dependencies_.descriptions->describe(bounded_job);
    if (!description.isSuccess()) return Result::failure(description.error(), description.message());
    const auto index = static_cast<std::size_t>(job.destination);
    if (index >= roots_.size() || roots_[index].empty() || job.policy.max_bytes == 0 || job.policy.max_bytes > quota_) return Result::failure(result::ServiceErrorType::InvalidParam, "snapshot_root_unavailable");
    auto used = reserved_->at(index).load();
    do {
        if (used > quota_ - job.policy.max_bytes) return Result::failure(result::ServiceErrorType::TransientExternalApiError, "snapshot_quota_exhausted");
    } while (!reserved_->at(index).compare_exchange_weak(used, used + job.policy.max_bytes));
    std::shared_ptr<snapshot_source::Workspace> workspace;
    try {
        workspace = std::make_shared<snapshot_source::Workspace>(roots_[index], reserved_, index, job.policy.max_bytes);
    } catch (const std::exception &) {
        reserved_->at(index).fetch_sub(job.policy.max_bytes);
        return Result::failure(result::ServiceErrorType::InternalError, "snapshot_staging_failed");
    }
    try {
        auto file = result::ServiceResult<std::string>::failure(result::ServiceErrorType::InvalidParam, "invalid_source_description");
        auto dependencies = dependencies_.bind_context ? dependencies_.bind_context(context) : dependencies_;
        if (!dependencies.token_refresh) dependencies.token_refresh = dependencies_.token_refresh;
        const auto execution = dependencies.execution_context.value_or(context);
        const auto &input = *description.data();
        if (const auto target = std::get_if<model::SongResourceQueueTarget>(&input))
            file = snapshot_source::song(dependencies, job, *target, workspace, execution);
        else if (const auto target = std::get_if<model::MetadataCoverRecord>(&input)) {
            if (!dependencies.covers) return Result::failure(result::ServiceErrorType::InvalidParam, "missing_cover_source");
            auto files = std::make_shared<FileStorage>(workspace->root, false);
            MetadataCoverStore store(dependencies.covers, files, dependencies.cover_headers);
            model::MetadataCoverSaveRequest request;
            request.target = *target;
            request.target.file = {};
            request.timeout_ms = execution.remaining();
            request.max_bytes = job.policy.max_bytes;
            request.cancelled = [execution] { return execution.stopped(); };
            const auto saved = store.save(std::move(request));
            if (saved.status != 2) return Result::failure(saved.error_category == "network" ? result::ServiceErrorType::TransientExternalApiError : result::ServiceErrorType::ExternalApiError, saved.result_code);
            store.accept(saved.local_path);
            file = result::ServiceResult<std::string>::success(saved.local_path);
        } else if (const auto target = std::get_if<model::ResourceArtifactInput>(&input))
            file = snapshot_source::artifact(dependencies, *target, workspace, job, execution);
        if (!file.isSuccess()) {
            if (workspace->related_parsed && context.related_discovered) context.related_discovered(workspace->related_mask);
            return Result::failure(file.error(), file.message());
        }
        if (!cloud::safeRelativePath(*file.data())) return Result::failure(result::ServiceErrorType::InvalidParam, "invalid_snapshot_path");
        const auto digest = digestResourceFile(workspace->root / *file.data(), job.policy.max_bytes, execution);
        if (!digest.isSuccess()) return Result::failure(digest.error(), digest.message());
        auto snapshot = std::make_shared<SnapshotFile>(std::move(workspace), *file.data(), *digest.data());
        return Result::success(std::move(snapshot));
    } catch (const std::exception &) {
        return Result::failure(result::ServiceErrorType::InternalError, "snapshot_acquisition_failed");
    }
}

} // namespace orchestrator::modules::crawler::infrastructure::storage::filesystem
