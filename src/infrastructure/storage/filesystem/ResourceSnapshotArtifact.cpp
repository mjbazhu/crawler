#include "ResourceSnapshotSourceSupport.h"
#include "crawler/infrastructure/storage/cloud/CloudFileSupport.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::storage::filesystem::snapshot_source {
namespace model = application::model;
namespace result = application::result;
namespace {
// 只在本次 workspace 活着期间读取输入，不拥有正式本地对象。
class Input final : public model::ResourceContentSnapshot {
public:
    Input(std::filesystem::path root, std::string relative, ResourceFileDigest digest) : relative_(std::move(relative)), digest_(std::move(digest))
    {
        const auto utf8 = (root / std::filesystem::u8path(relative_)).generic_u8string();
        path_.assign(reinterpret_cast<const char *>(utf8.data()), utf8.size());
    }
    const std::string &filePath() const override
    {
        return path_;
    }
    const std::string &relativePath() const override
    {
        return relative_;
    }
    const std::string &checksum() const override
    {
        return digest_.sha256;
    }
    std::uint64_t sizeBytes() const override
    {
        return digest_.size_bytes;
    }

private:
    std::string path_;
    std::string relative_;
    ResourceFileDigest digest_;
};
// 优先读取已经存在的受控产物；不存在才走显式任务的 producer 来源。
std::optional<std::filesystem::path> existing(const ResourceSnapshotDependencies &dependencies, const model::ResourceArtifactInput &input)
{
    if (!dependencies.artifacts || input.local_path.empty()) return std::nullopt;
    if (!cloud::safeRelativePath(input.local_path)) throw std::invalid_argument("invalid artifact path");
    const auto base = std::filesystem::weakly_canonical(std::filesystem::absolute(dependencies.artifacts->getFullPath("")));
    const auto path = std::filesystem::weakly_canonical(dependencies.artifacts->getFullPath(input.local_path));
    if (!cloud::safeRelativePath(path.lexically_relative(base).generic_string())) throw std::invalid_argument("invalid artifact path");
    std::error_code error;
    return std::filesystem::is_regular_file(path, error) && !error ? std::optional(path) : std::nullopt;
}
} // namespace

application::result::ServiceResult<std::string> artifact(const ResourceSnapshotDependencies &dependencies, const model::ResourceArtifactInput &input, const std::shared_ptr<Workspace> &workspace, const model::ResourceTransferJob &job, const model::ResourceTransferContext &context)
{
    using Result = result::ServiceResult<std::string>;
    if (input.artifact_type != "word_json" && input.artifact_type != "enhanced_lrc") return Result::failure(result::ServiceErrorType::InvalidParam, "invalid_artifact_type");
    std::optional<std::filesystem::path> source;
    try {
        source = existing(dependencies, input);
    } catch (const std::invalid_argument &) {
        return Result::failure(result::ServiceErrorType::InvalidParam, "invalid_artifact_path");
    }
    std::optional<model::EncodedLyricArtifact> generated;
    if (!source) {
        if (!dependencies.producer || !dependencies.producer->available()) return Result::failure(result::ServiceErrorType::ExternalApiError, "waiting_input");
        generated = dependencies.producer->cached(input);
        if (!generated) {
            if (!input.source || input.source->resource_kind != "mrc" || input.source->source != "migu" || input.source_checksum.size() != 64) return Result::failure(result::ServiceErrorType::ExternalApiError, "waiting_input");
            auto bounded = job;
            bounded.policy.max_bytes = (std::min<std::uint64_t>)(job.policy.max_bytes, 1024 * 1024);
            const auto downloaded = song(dependencies, bounded, *input.source, workspace, context);
            if (!downloaded.isSuccess()) return Result::failure(downloaded.error(), downloaded.message());
            const auto digest = digestResourceFile(workspace->root / *downloaded.data(), bounded.policy.max_bytes, context);
            if (!digest.isSuccess()) return Result::failure(digest.error(), digest.message());
            const auto rebuilt = dependencies.producer->regenerate(input, Input(workspace->root, *downloaded.data(), *digest.data()), context);
            if (!rebuilt.isSuccess()) return Result::failure(rebuilt.error(), rebuilt.message());
            generated = *rebuilt.data();
            // 本次输入在重新生成后即可回收，临时磁盘峰值仍受本项 max_bytes 限制。
            std::filesystem::remove(workspace->root / *downloaded.data());
        }
        if (generated->content.size() > job.policy.max_bytes) return Result::failure(result::ServiceErrorType::InvalidParam, "snapshot_too_large");
    }
    const auto relative = "artifacts/" + input.artifact_type + (input.artifact_type == "word_json" ? ".json" : ".lrc");
    const auto output = workspace->root / relative;
    std::filesystem::create_directories(output.parent_path());
    std::ofstream to(output, std::ios::binary);
    if (!to) return Result::failure(result::ServiceErrorType::InternalError, "snapshot_open_failed");
    if (source) {
        std::ifstream from(*source, std::ios::binary);
        if (!from) return Result::failure(result::ServiceErrorType::InternalError, "snapshot_open_failed");
        std::array<char, 64 * 1024> chunk {};
        std::uint64_t total = 0;
        while (from) {
            if (context.stopped()) return Result::failure(result::ServiceErrorType::ExternalApiError, "cancelled");
            from.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
            const auto count = from.gcount();
            if (count <= 0) break;
            total += static_cast<std::uint64_t>(count);
            if (total > job.policy.max_bytes) return Result::failure(result::ServiceErrorType::InvalidParam, "snapshot_too_large");
            to.write(chunk.data(), count);
            if (!to) return Result::failure(result::ServiceErrorType::InternalError, "snapshot_write_failed");
        }
        if (from.bad()) return Result::failure(result::ServiceErrorType::InternalError, "snapshot_read_failed");
    } else {
        if (context.stopped()) return Result::failure(result::ServiceErrorType::ExternalApiError, "cancelled");
        to.write(generated->content.data(), static_cast<std::streamsize>(generated->content.size()));
    }
    to.close();
    if (!to) return Result::failure(result::ServiceErrorType::InternalError, "snapshot_write_failed");
    const auto digest = digestResourceFile(output, job.policy.max_bytes, context);
    if (!digest.isSuccess()) return Result::failure(digest.error(), digest.message());
    const auto old = model::parseLegacyResourceChecksum(input.checksum);
    if ((input.size_bytes && input.size_bytes != digest.data()->size_bytes) || (!old.sha256.empty() && old.sha256 != digest.data()->sha256) || (old.algorithm == "md5" && old.value != digest.data()->md5) || (old.algorithm == "sha1" && old.value != digest.data()->sha1)) return Result::failure(result::ServiceErrorType::Conflict, "content_conflict");
    return Result::success(relative);
}
} // namespace orchestrator::modules::crawler::infrastructure::storage::filesystem::snapshot_source
