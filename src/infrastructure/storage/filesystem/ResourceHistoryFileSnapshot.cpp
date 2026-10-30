#include "crawler/infrastructure/storage/filesystem/ResourceHistoryManifest.h"
#include "crawler/infrastructure/storage/filesystem/ResourceCloudImportManifest.h"
#include <chrono>
#include <random>
#include <stdexcept>
#include <utility>
namespace orchestrator::modules::crawler::infrastructure::storage::filesystem {
ResourceHistoryFileSnapshot::ResourceHistoryFileSnapshot(const std::filesystem::path &source, std::string expected_sha256)
{
    expected_sha256 = application::model::resourceDiscoveryHash("sha256", std::move(expected_sha256));
    if (expected_sha256.empty() || !std::filesystem::is_regular_file(source)) throw std::invalid_argument("history input or SHA256 is invalid");
    path_ = source;
    path_ += ".review." + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()) + '.' + std::to_string(std::random_device {}());
    owned_ = std::filesystem::copy_file(source, path_);
    if (!owned_) throw std::runtime_error("history snapshot not created");
    try {
        hash_ = resourceCloudManifestSha256(path_);
        if (hash_ != expected_sha256) throw std::invalid_argument("reviewed history SHA256 does not match");
    } catch (...) {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
        owned_ = false;
        throw;
    }
}
ResourceHistoryFileSnapshot::~ResourceHistoryFileSnapshot()
{
    if (owned_) {
        std::error_code ignored;
        std::filesystem::remove(path_, ignored);
    }
}
const std::filesystem::path &ResourceHistoryFileSnapshot::path() const
{
    return path_;
}
const std::string &ResourceHistoryFileSnapshot::sha256() const
{
    return hash_;
}
} // namespace orchestrator::modules::crawler::infrastructure::storage::filesystem
