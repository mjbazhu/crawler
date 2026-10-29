#include "crawler/infrastructure/storage/cloud/CloudFileSupport.h"

#include <openssl/evp.h>
#include <openssl/rand.h>

#include <algorithm>
#include <array>
#include <cstdint>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::storage::cloud {
namespace {
// 二进制摘要转小写十六进制。
std::string hex(const unsigned char *bytes, std::size_t size)
{
    constexpr char digits[] = "0123456789abcdef";
    std::string value;
    for (std::size_t i = 0; i < size; ++i) {
        value += digits[bytes[i] >> 4];
        value += digits[bytes[i] & 15];
    }
    return value;
}
// EVP 为不同摘要保留相同的完整内容校验实现。
std::string digest(std::string_view bytes, const EVP_MD *algorithm)
{
    std::array<unsigned char, EVP_MAX_MD_SIZE> result {};
    unsigned int size = 0;
    if (EVP_Digest(bytes.data(), bytes.size(), result.data(), &size, algorithm, nullptr) != 1) return {};
    return hex(result.data(), size);
}
} // namespace

bool safeRelativePath(std::string_view path)
{
    if (path.empty() || path.front() == '/' || path.front() == '\\' || path.find_first_of(":\\\r\n\"*?<>|") != std::string_view::npos || path.find('\0') != std::string_view::npos) return false;
    while (!path.empty()) {
        const auto slash = path.find('/');
        const auto part = path.substr(0, slash);
        if (part.empty() || part == "." || part == "..") return false;
        if (slash == std::string_view::npos) return true;
        path.remove_prefix(slash + 1);
        if (path.empty()) return false;
    }
    return false;
}

std::string sha256(std::string_view bytes)
{
    return digest(bytes, EVP_sha256());
}
std::string md5(std::string_view bytes)
{
    return digest(bytes, EVP_md5());
}

std::string uniqueToken()
{
    std::array<unsigned char, 16> bytes {};
    if (RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) != 1) throw std::runtime_error("cloud snapshot identity unavailable");
    return hex(bytes.data(), bytes.size());
}

application::model::CloudFileResult failure(std::string code, std::string category, bool uncertain)
{
    application::model::CloudFileResult result;
    result.uncertain = uncertain;
    result.result_code = std::move(code);
    result.error_category = std::move(category);
    result.message = "cloud file operation was not confirmed";
    return result;
}

bool validUpload(const application::model::CloudFileUploadRequest &request)
{
    return safeRelativePath(request.relative_path) && !request.bytes.empty() && request.bytes.size() <= 8 * 1024 * 1024 && request.checksum.size() == 64 && sha256(request.bytes) == request.checksum;
}

Deadline::Deadline(int timeout_ms, std::function<bool()> cancelled) : end_(std::chrono::steady_clock::now() + std::chrono::milliseconds((std::clamp)(timeout_ms, 1, 600000))), cancelled_(std::move(cancelled))
{}
int Deadline::remaining() const
{
    if (cancelled()) return 0;
    return static_cast<int>((std::max)(std::int64_t {0}, std::chrono::duration_cast<std::chrono::milliseconds>(end_ - std::chrono::steady_clock::now()).count()));
}
bool Deadline::cancelled() const
{
    return cancelled_ && cancelled_();
}

FileSnapshot::FileSnapshot(const std::filesystem::path &root, const std::string &relative_path, std::string_view bytes)
{
    if (!safeRelativePath(relative_path)) throw std::invalid_argument("unsafe cloud snapshot path");
    const auto base = std::filesystem::weakly_canonical(std::filesystem::absolute(root));
    parent_ = std::filesystem::weakly_canonical(base / ".cover-cloud-upload");
    if (!safeRelativePath(parent_.lexically_relative(base).generic_string())) throw std::invalid_argument("cloud snapshot parent escaped storage root");
    std::filesystem::create_directories(parent_);
    root_ = parent_ / uniqueToken();
    if (!std::filesystem::create_directory(root_)) throw std::runtime_error("cloud snapshot directory collision");
    try {
        const auto path = root_ / relative_path;
        std::filesystem::create_directories(path.parent_path());
        std::ofstream output(path, std::ios::binary);
        output.write(bytes.data(), static_cast<std::streamsize>(bytes.size()));
        output.close();
        if (!output) throw std::runtime_error("cloud snapshot could not be saved");
    } catch (...) {
        std::error_code ignored;
        if (root_.parent_path() == parent_) std::filesystem::remove_all(root_, ignored);
        throw;
    }
}

FileSnapshot::~FileSnapshot()
{
    std::error_code ignored;
    if (root_.parent_path() == parent_ && std::filesystem::weakly_canonical(root_.parent_path(), ignored) == parent_) std::filesystem::remove_all(root_, ignored);
}
const std::filesystem::path &FileSnapshot::root() const
{
    return root_;
}

} // namespace orchestrator::modules::crawler::infrastructure::storage::cloud
