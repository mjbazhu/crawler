#include "crawler/infrastructure/storage/filesystem/LocalLyricArtifactStore.h"

#include "crawler/infrastructure/storage/filesystem/detail/LyricFilePublication.h"

#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <fstream>
#include <optional>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::storage::filesystem {
namespace {

namespace fs = std::filesystem;
using Error = application::result::ServiceErrorType;

// 来源与目标均禁止绝对路径和上跳；canonical 同时解析符号链接与 junction。
std::optional<fs::path> resolve(const fs::path &root, std::string_view relative)
{
    const fs::path input {relative};
    if (input.empty() || input.is_absolute() || input.has_root_name()) return std::nullopt;
    for (const auto &part : input)
        if (part == "..") return std::nullopt;
    const auto canonical = fs::weakly_canonical(root / input);
    if (!detail::isWithinRoot(root, canonical)) return std::nullopt;
    return canonical;
}

// 版本和摘要仅交给数据库索引，不允许控制字符或路径分隔符。
bool safeMetadataToken(std::string_view value)
{
    return !value.empty() && value != "." && value != ".." && std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return (ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.';
    });
}

// 将摘要转为稳定的小写十六进制，交给数据库索引。
std::string hex(const unsigned char *data, std::size_t size)
{
    constexpr char digits[] = "0123456789abcdef";
    std::string output;
    output.reserve(size * 2);
    for (std::size_t index = 0; index < size; ++index) {
        output += digits[data[index] >> 4];
        output += digits[data[index] & 15];
    }
    return output;
}

// 一次性计算受限歌词文件的 SHA-256，不使用已弃用的 OpenSSL SHA 上下文 API。
std::optional<std::string> checksum(std::string_view content)
{
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest {};
    unsigned int length = 0;
    if (EVP_Digest(content.data(), content.size(), digest.data(), &length, EVP_sha256(), nullptr) != 1) return std::nullopt;
    return hex(digest.data(), length);
}

} // namespace

LocalLyricArtifactStore::LocalLyricArtifactStore(fs::path root) : root_(fs::weakly_canonical(fs::absolute(std::move(root))))
{}

application::result::ServiceResult<application::model::LyricSourceContent> LocalLyricArtifactStore::readSource(std::string_view path, std::size_t max_bytes) const
{
    using Result = application::result::ServiceResult<application::model::LyricSourceContent>;
    try {
        const auto full = resolve(root_, path);
        if (!full) return Result::failure(Error::InvalidParam, "lyric_unsafe_source_path");
        if (!fs::is_regular_file(*full)) return Result::failure(Error::NotFound, "lyric_source_not_found");
        const auto size = fs::file_size(*full);
        if (size == 0 || size > max_bytes) return Result::failure(Error::InvalidParam, "lyric_source_size_invalid");
        std::ifstream stream(*full, std::ios::binary);
        std::string content(static_cast<std::size_t>(size), '\0');
        stream.read(content.data(), static_cast<std::streamsize>(size));
        if (!stream || stream.peek() != std::char_traits<char>::eof()) return Result::failure(Error::Conflict, "lyric_source_read_failed");
        const auto hash = checksum(content);
        if (!hash) return Result::failure(Error::InternalError, "lyric_checksum_failed");
        return Result::success({std::move(content), *hash});
    } catch (const fs::filesystem_error &) {
        return Result::failure(Error::InternalError, "lyric_source_io_failed");
    }
}

application::result::ServiceResult<application::model::LyricArtifactBundle> LocalLyricArtifactStore::publish(const application::model::LyricProcessingRequest &request, std::string_view source_checksum, std::string_view version, int schema_version, const std::vector<application::model::EncodedLyricArtifact> &artifacts) const
{
    using Result = application::result::ServiceResult<application::model::LyricArtifactBundle>;
    if (source_checksum.size() != 64 || !safeMetadataToken(source_checksum) || !safeMetadataToken(version) || version.size() > 96 || schema_version < 1 || artifacts.size() != 2) return Result::failure(Error::InvalidParam, "lyric_invalid_artifacts");
    try {
        const auto source = resolve(root_, request.local_path);
        if (!source) return Result::failure(Error::InvalidParam, "lyric_unsafe_source_path");
        if (source->filename() == "words.manifest.json") return Result::failure(Error::InvalidParam, "lyric_invalid_artifacts");
        const auto relative = source->parent_path().lexically_relative(root_);
        application::model::LyricArtifactBundle bundle {request.source_resource_id, std::string(source_checksum), std::string(version), schema_version, {}};
        std::vector<detail::LyricFileContent> contents;
        bool has_json = false;
        bool has_lrc = false;
        for (const auto &artifact : artifacts) {
            const bool json = artifact.kind == "word_json" && artifact.filename == "words.json";
            const bool lrc = artifact.kind == "enhanced_lrc" && artifact.filename == "words.enhanced.lrc";
            if (artifact.filename == source->filename().string() || (!json && !lrc) || (json && has_json) || (lrc && has_lrc) || artifact.content.empty()) return Result::failure(Error::InvalidParam, "lyric_invalid_artifacts");
            has_json = has_json || json;
            has_lrc = has_lrc || lrc;
            const auto hash = checksum(artifact.content);
            if (!hash) return Result::failure(Error::InternalError, "lyric_checksum_failed");
            bundle.files.push_back({artifact.kind, (relative / artifact.filename).lexically_normal().generic_string(), artifact.content.size(), *hash});
            contents.push_back({artifact.filename, artifact.content});
        }
        std::lock_guard lock(publication_mutex_);
        // 固定路径更新前再次核对来源，避免旧解码结果覆盖已经替换的原始文件。
        const auto current = readSource(request.local_path, 1024 * 1024);
        if (!current.isSuccess()) return Result::failure(current.error(), current.message());
        if (current.data()->checksum != source_checksum) return Result::failure(Error::Conflict, "lyric_source_changed");
        // 只移除当前歌曲旧版本生成的清单，不扫描或清理其他目录。
        const auto legacy_manifest = source->parent_path() / "words.manifest.json";
        if (fs::exists(legacy_manifest)) {
            if (!fs::is_regular_file(fs::symlink_status(legacy_manifest))) return Result::failure(Error::Conflict, "lyric_artifact_conflict");
            fs::remove(legacy_manifest);
        }
        const auto published = detail::publishLyricFiles(root_, source->parent_path(), contents);
        if (!published.isSuccess()) return Result::failure(published.error(), published.message());
        return Result::success(std::move(bundle));
    } catch (const fs::filesystem_error &) {
        return Result::failure(Error::InternalError, "lyric_artifact_io_failed");
    }
}

} // namespace orchestrator::modules::crawler::infrastructure::storage::filesystem
