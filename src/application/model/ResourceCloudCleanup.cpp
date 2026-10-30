#include "crawler/application/model/ResourceCloudCleanup.h"
#include "crawler/application/model/ResourceDiscovery.h"
#include <algorithm>
namespace orchestrator::modules::crawler::application::model {
bool safeResourceCloudCleanupPath(const std::string &path)
{
    if (path.empty() || path.size() > 4096 || path.find("://") != std::string::npos || path.find_first_of("?#@\\") != std::string::npos || std::any_of(path.begin(), path.end(), [](unsigned char c) { return c < 32 || c == 127; })) return false;
    auto start = std::size_t(0);
    do {
        const auto end = path.find('/', start);
        const auto part = path.substr(start, end == std::string::npos ? end : end - start);
        if (part == "." || part == ".." || (part.empty() && start != 0)) return false;
        if (end == std::string::npos) break;
        start = end + 1;
    } while (start < path.size());
    return path.back() != '/';
}
std::string resourceCloudCleanupObjectKey(const std::string &path)
{
    if (!safeResourceCloudCleanupPath(path)) return {};
    constexpr auto marker = "resources/v1/";
    const auto position = path.find(marker);
    if (position == std::string::npos || (position && path[position - 1] != '/') || path.find(marker, position + 1) != std::string::npos) return {};
    auto key = path.substr(position);
    return safeResourceDiscoveryPath(key) ? key : std::string {};
}
std::string classifyResourceCloudCleanup(const ResourceCloudCleanupCandidate &c, const ResourceCloudCleanupFacts &f)
{
    if (f.source_exists) return "source_retained";
    if (!f.target_matches) return "target_changed";
    if (f.active_intent || c.receipt_state == "running" || c.receipt_state == "uncertain") return "active_or_uncertain_intent";
    if (f.shared_reference) return "shared_reference";
    if (f.alias_reference) return "alias_evidence_requires_review";
    if (!safeResourceCloudCleanupPath(c.cloud_path)) return "unsafe_or_unresolved_path";
    if (c.object_key.empty() || c.object_key != resourceCloudCleanupObjectKey(c.cloud_path)) return "unknown_path_layout";
    if (c.verification != "sha256_verified" || resourceDiscoveryHash("sha256", c.checksum) != c.checksum || c.checksum.empty()) return "unverified_evidence";
    if (c.confirmed_content_generation != c.content_generation || c.confirmed_target_revision != c.target_revision) return "confirmation_version_mismatch";
    if (!f.maintenance) return "maintenance_required";
    return "candidate_only";
}
} // namespace orchestrator::modules::crawler::application::model
