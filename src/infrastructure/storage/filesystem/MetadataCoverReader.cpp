#include "crawler/infrastructure/storage/filesystem/MetadataCoverStore.h"

#include "FileWriteSupport.h"
#include "MetadataCoverIo.h"

#include <array>
#include <filesystem>
#include <fstream>
#include <system_error>

namespace orchestrator::modules::crawler::infrastructure::storage::filesystem {
namespace {

// 解析实际路径后再次检查根边界，避免数据库旧路径或符号链接逃逸。
std::optional<std::filesystem::path> safeCoverPath(const FileStorage &files, std::string_view path)
{
    if (!file_detail::isSafeRelativePath(path)) return std::nullopt;
    std::error_code error;
    const auto root = std::filesystem::weakly_canonical(files.getFullPath(""), error);
    if (error) return std::nullopt;
    const auto full = std::filesystem::weakly_canonical(files.getFullPath(path), error);
    if (error || !file_detail::isSafeRelativePath(full.lexically_relative(root).generic_string())) return std::nullopt;
    if (!std::filesystem::is_regular_file(full, error) || error) return std::nullopt;
    return full;
}

} // namespace

bool MetadataCoverStore::available(const application::model::MetadataResourceFile &file) const
{
    if (!files_ || file.local_path.empty() || file.size_bytes == 0 || file.size_bytes > cover_io::maxCoverBytes || file.checksum.empty()) return false;
    const auto path = safeCoverPath(*files_, file.local_path);
    if (!path) return false;
    std::error_code error;
    const auto size = std::filesystem::file_size(*path, error);
    return !error && size == file.size_bytes;
}

std::optional<application::model::MetadataCoverContent> MetadataCoverStore::read(const application::model::MetadataResourceFile &file) const
{
    if (!available(file)) return std::nullopt;
    const auto path = safeCoverPath(*files_, file.local_path);
    if (!path) return std::nullopt;
    std::ifstream input(*path, std::ios::binary);
    std::vector<char> bytes(static_cast<std::size_t>(file.size_bytes));
    input.read(bytes.data(), static_cast<std::streamsize>(bytes.size()));
    if (!input || input.peek() != std::char_traits<char>::eof()) return std::nullopt;
    const auto extension = cover_io::imageExtension(bytes);
    if (extension.empty()) return std::nullopt;
    std::array<unsigned char, SHA256_DIGEST_LENGTH> digest {};
    if (!SHA256(reinterpret_cast<const unsigned char *>(bytes.data()), bytes.size(), digest.data())) return std::nullopt;
    if (file_detail::sha256DigestHex(digest) != file.checksum) return std::nullopt;
    return application::model::MetadataCoverContent {std::string(bytes.begin(), bytes.end()), cover_io::imageMime(extension), file.checksum};
}

} // namespace orchestrator::modules::crawler::infrastructure::storage::filesystem
