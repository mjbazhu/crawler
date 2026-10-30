#include "crawler/infrastructure/storage/filesystem/ResourceFileDigest.h"

#include <openssl/evp.h>

#include <array>
#include <fstream>
#include <memory>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::storage::filesystem {
namespace {
// EVP 上下文的唯一所有权，不共享摘要状态。
using DigestContext = std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)>;
// 最终摘要转为小写十六进制。
std::string finish(EVP_MD_CTX *context)
{
    std::array<unsigned char, EVP_MAX_MD_SIZE> bytes {};
    unsigned int size = 0;
    if (EVP_DigestFinal_ex(context, bytes.data(), &size) != 1) return {};
    static constexpr char hex[] = "0123456789abcdef";
    std::string value;
    for (unsigned int i = 0; i < size; ++i) {
        value.push_back(hex[bytes[i] >> 4]);
        value.push_back(hex[bytes[i] & 15]);
    }
    return value;
}
} // namespace

application::result::ServiceResult<ResourceFileDigest> digestResourceFile(const std::filesystem::path &file, std::uint64_t max_bytes, const application::model::ResourceTransferContext &context)
{
    using Result = application::result::ServiceResult<ResourceFileDigest>;
    using Error = application::result::ServiceErrorType;
    try {
        std::error_code error;
        const auto status = std::filesystem::symlink_status(file, error);
        if (error || !std::filesystem::is_regular_file(status) || std::filesystem::is_symlink(status)) return Result::failure(Error::InvalidParam, "invalid_snapshot_file");
        const auto size = std::filesystem::file_size(file);
        const auto stamp = std::filesystem::last_write_time(file);
        if (size == 0 || size > max_bytes) return Result::failure(Error::InvalidParam, "snapshot_too_large");
        std::ifstream input(file, std::ios::binary);
        if (!input) return Result::failure(Error::InternalError, "snapshot_open_failed");
        DigestContext sha(EVP_MD_CTX_new(), EVP_MD_CTX_free), md5(EVP_MD_CTX_new(), EVP_MD_CTX_free), sha1(EVP_MD_CTX_new(), EVP_MD_CTX_free);
        if (!sha || EVP_DigestInit_ex(sha.get(), EVP_sha256(), nullptr) != 1) return Result::failure(Error::InternalError, "snapshot_hash_failed");
        const bool has_md5 = md5 && EVP_DigestInit_ex(md5.get(), EVP_md5(), nullptr) == 1;
        const bool has_sha1 = sha1 && EVP_DigestInit_ex(sha1.get(), EVP_sha1(), nullptr) == 1;
        std::array<char, 64 * 1024> chunk {};
        ResourceFileDigest value;
        while (input) {
            if (context.stopped()) return Result::failure(Error::ExternalApiError, context.remaining() == 0 ? "timeout" : "cancelled");
            input.read(chunk.data(), static_cast<std::streamsize>(chunk.size()));
            const auto count = input.gcount();
            if (count <= 0) break;
            value.size_bytes += static_cast<std::uint64_t>(count);
            if (value.size_bytes > max_bytes) return Result::failure(Error::InvalidParam, "snapshot_too_large");
            if (EVP_DigestUpdate(sha.get(), chunk.data(), static_cast<std::size_t>(count)) != 1 || (has_md5 && EVP_DigestUpdate(md5.get(), chunk.data(), static_cast<std::size_t>(count)) != 1)) return Result::failure(Error::InternalError, "snapshot_hash_failed");
            if (has_sha1 && EVP_DigestUpdate(sha1.get(), chunk.data(), static_cast<std::size_t>(count)) != 1) return Result::failure(Error::InternalError, "snapshot_hash_failed");
        }
        if (input.bad() || value.size_bytes != size || std::filesystem::file_size(file) != size || std::filesystem::last_write_time(file) != stamp) return Result::failure(Error::Conflict, "snapshot_changed");
        value.sha256 = finish(sha.get());
        if (has_md5) value.md5 = finish(md5.get());
        if (has_sha1) value.sha1 = finish(sha1.get());
        if (value.sha256.size() != 64 || (has_md5 && value.md5.size() != 32) || (has_sha1 && value.sha1.size() != 40)) return Result::failure(Error::InternalError, "snapshot_hash_failed");
        return Result::success(std::move(value));
    } catch (const std::exception &) {
        return Result::failure(Error::InternalError, "snapshot_read_failed");
    }
}

} // namespace orchestrator::modules::crawler::infrastructure::storage::filesystem
