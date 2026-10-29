#include "crawler/infrastructure/storage/filesystem/FileStorage.h"

#include "FileWriteSupport.h"

#include <fstream>
#include <system_error>

namespace orchestrator::modules::crawler::infrastructure::storage::filesystem {

std::size_t FileStorage::recoverResourceWrites(const std::function<std::optional<application::model::ResourceWriteDisposition>(std::uint64_t, std::uint64_t, const std::string &)> &inspect)
{
    return recoverOwnedResourceWrites([&](const std::string &owner, std::uint64_t resource, std::uint64_t generation, const std::string &path) {
        return owner == "song" ? inspect(resource, generation, path) : std::nullopt;
    });
}

std::size_t FileStorage::recoverOwnedResourceWrites(const std::function<std::optional<application::model::ResourceWriteDisposition>(const std::string &, std::uint64_t, std::uint64_t, const std::string &)> &inspect)
{
    const auto directory = root_dir_ / ".resource-writes";
    std::error_code error;
    if (!std::filesystem::exists(directory, error) || error) return 0;
    std::size_t recovered = 0;
    for (const auto &entry : std::filesystem::directory_iterator(directory)) {
        if (entry.path().extension() != ".pending") continue;
        std::ifstream record(entry.path(), std::ios::binary);
        std::uint64_t resource = 0;
        std::uint64_t generation = 0;
        std::string local_path;
        std::string temporary;
        std::string previous;
        if (!(record >> resource >> generation)) continue;
        record.ignore();
        std::getline(record, local_path);
        std::getline(record, temporary);
        std::getline(record, previous);
        std::string owner;
        if (!std::getline(record, owner) || owner.empty()) owner = "song";
        if (owner != "song" && owner != "singer" && owner != "album") continue;
        record.close();
        // 日志也作为外部数据检查，禁止损坏日志逃逸存储根目录。
        if (!file_detail::isSafeRelativePath(local_path) || !file_detail::isSafeRelativePath(temporary)) continue;
        const auto disposition = inspect(owner, resource, generation, local_path);
        if (!disposition || *disposition == application::model::ResourceWriteDisposition::Active) continue;
        if (*disposition == application::model::ResourceWriteDisposition::Discard) {
            error.clear();
            std::filesystem::remove(root_dir_ / local_path, error);
            if (error) continue;
        }
        if (*disposition == application::model::ResourceWriteDisposition::Committed && !previous.empty() && previous != local_path && file_detail::isSafeRelativePath(previous)) {
            error.clear();
            std::filesystem::remove(root_dir_ / previous, error);
            if (error) continue;
        }
        error.clear();
        std::filesystem::remove(root_dir_ / temporary, error);
        if (error) continue;
        error.clear();
        std::filesystem::remove(entry.path(), error);
        if (!error) ++recovered;
    }
    return recovered;
}
} // namespace orchestrator::modules::crawler::infrastructure::storage::filesystem
