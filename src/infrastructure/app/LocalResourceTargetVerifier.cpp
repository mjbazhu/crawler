#include "ResourceTargetVerificationSupport.h"

#include <openssl/evp.h>
#include <array>
#include <stdexcept>
#include <system_error>
#ifdef _WIN32
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace orchestrator::modules::crawler::infrastructure::app {
namespace model = application::model;
namespace target_verification {
std::string fingerprint(const nlohmann::json &identity)
{
    const auto canonical = identity.dump();
    std::array<unsigned char, 32> digest {};
    unsigned int size = 0;
    if (EVP_Digest(canonical.data(), canonical.size(), digest.data(), &size, EVP_sha256(), nullptr) != 1 || size != digest.size()) throw std::runtime_error("target fingerprint failed");
    constexpr char hex[] = "0123456789abcdef";
    std::string value;
    for (const auto byte : digest) {
        value += hex[byte >> 4];
        value += hex[byte & 15];
    }
    return value;
}
ResourceTargetVerification unavailable(model::ResourceDestination destination, std::string adapter, std::string code)
{
    ResourceTargetVerification value;
    value.profile.destination = destination;
    value.profile.adapter = std::move(adapter);
    value.profile.available = false;
    value.result_code = std::move(code);
    return value;
}
} // namespace target_verification

ResourceTargetVerification verifyLocalResourceTarget(const std::filesystem::path &root, const model::ResourceTransferContext &context)
{
    auto value = target_verification::unavailable(model::ResourceDestination::Local, "filesystem", "local_identity_unavailable");
    if (context.stopped()) {
        value.result_code = "target_probe_stopped";
        return value;
    }
    try {
        if (root.empty() || !std::filesystem::is_directory(root)) return value;
        const auto canonical = std::filesystem::canonical(root);
        const auto path = canonical.generic_u8string();
        nlohmann::json identity {{"provider", "local"}, {"root", std::string(path.begin(), path.end())}, {"layout", "resource-data/v1"}};
#ifdef _WIN32
        // 目录句柄的 RAII 生命周期覆盖身份读取，不申请写权限。
        struct DirectoryHandle {
            HANDLE value;
            ~DirectoryHandle()
            {
                if (value != INVALID_HANDLE_VALUE) CloseHandle(value);
            }
        } handle {CreateFileW(canonical.c_str(), 0, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr, OPEN_EXISTING, FILE_FLAG_BACKUP_SEMANTICS, nullptr)};
        BY_HANDLE_FILE_INFORMATION info {};
        if (handle.value == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(handle.value, &info)) return value;
        identity["volume"] = info.dwVolumeSerialNumber;
        identity["file_id"] = (std::uint64_t(info.nFileIndexHigh) << 32) | info.nFileIndexLow;
        identity["created"] = (std::uint64_t(info.ftCreationTime.dwHighDateTime) << 32) | info.ftCreationTime.dwLowDateTime;
#else
        struct stat info {};
        if (::stat(canonical.c_str(), &info) != 0) return value;
        identity["volume"] = info.st_dev;
        identity["file_id"] = info.st_ino;
#endif
        if (context.stopped()) {
            value.result_code = "target_probe_stopped";
            return value;
        }
        value.profile.fingerprint = target_verification::fingerprint(identity);
        value.profile.available = true;
        value.identity_scope = "filesystem_object";
        value.result_code.clear();
    } catch (const std::exception &) {}
    return value;
}
} // namespace orchestrator::modules::crawler::infrastructure::app
