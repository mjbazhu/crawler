#include "crawler/application/model/ResourceDiscovery.h"
#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <utility>
namespace orchestrator::modules::crawler::application::model {
int resourceDiscoveryPageBudget(const ResourceDiscoveryDefinition &d)
{
    if (d.concurrency < 1 || d.concurrency > 8 || d.page_size < 1 || d.page_size > 200 || d.object_timeout_ms < 100 || d.object_timeout_ms > 10000) throw std::invalid_argument("invalid discovery budget");
    return 60000 + ((d.page_size + d.concurrency - 1) / d.concurrency) * d.object_timeout_ms;
}
void validateResourceDiscoveryDefinition(ResourceDiscoveryDefinition &d)
{
    const auto identifier = [](const std::string &v) { return !v.empty() && v.size() <= 96 && std::all_of(v.begin(), v.end(), [](char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.'; }); };
    if (!identifier(d.run_id) || resourceOwnerName(d.owner).empty() || (d.destination != ResourceDestination::Baidu && d.destination != ResourceDestination::Gdrive) || (d.path_source != "cloud" && d.path_source != "local") || d.page_size < 1 || d.page_size > 200 || d.concurrency < 1 || d.concurrency > 8 || d.object_timeout_ms < 100 || d.object_timeout_ms > 10000 || resourceDiscoveryPageBudget(d) > 600000) throw std::invalid_argument("invalid discovery definition");
    std::replace(d.strip_prefix.begin(), d.strip_prefix.end(), '\\', '/');
    while (d.strip_prefix.size() > 1 && d.strip_prefix.back() == '/') d.strip_prefix.pop_back();
    if (d.strip_prefix.size() > 4096 || d.strip_prefix.find("://") != std::string::npos || d.strip_prefix.find_first_of("?#") != std::string::npos || std::any_of(d.strip_prefix.begin(), d.strip_prefix.end(), [](unsigned char c) { return c < 32 || c == 127; })) throw std::invalid_argument("invalid discovery path mapping");
}
bool safeResourceDiscoveryPath(const std::string &path)
{
    if (path.empty() || path.size() > 4096 || path.front() == '/' || path.back() == '/' || path.find_first_of(":?#\\") != std::string::npos || std::any_of(path.begin(), path.end(), [](unsigned char c) { return c < 32 || c == 127; })) return false;
    std::size_t start = 0;
    do {
        const auto end = path.find('/', start);
        const auto part = path.substr(start, end == std::string::npos ? end : end - start);
        if (part.empty() || part == "." || part == "..") return false;
        if (end == std::string::npos) break;
        start = end + 1;
    } while (start < path.size());
    return true;
}
std::string resourceDiscoveryCandidate(const LegacyResourceSnapshot &s, const ResourceDiscoveryDefinition &d)
{
    if (s.status == 4) return {};
    auto path = d.path_source == "cloud" ? s.cloud_path : s.local_path;
    std::replace(path.begin(), path.end(), '\\', '/');
    if (!d.strip_prefix.empty()) {
        const auto prefix = d.strip_prefix.back() == '/' ? d.strip_prefix : d.strip_prefix + '/';
        if (!path.starts_with(prefix)) return {};
        path.erase(0, prefix.size());
    }
    return safeResourceDiscoveryPath(path) ? path : std::string {};
}
std::string resourceDiscoveryHash(std::string algorithm, std::string value)
{
    const auto length = algorithm == "sha256" ? 64 : algorithm == "md5" ? 32 :
                                                 algorithm == "sha1"    ? 40 :
                                                                          0;
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return length && value.size() == static_cast<std::size_t>(length) && std::all_of(value.begin(), value.end(), [](char c) { return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f'); }) ? value : std::string {};
}
ResourceDiscoveryObservation classifyResourceDiscovery(const LegacyResourceSnapshot &s, std::string path, const ResourceObjectObservation &object)
{
    ResourceDiscoveryObservation value;
    value.resource = s.identity;
    value.generation = s.generation;
    value.source_identity = s.source_identity;
    value.values_digest = s.values_digest;
    value.candidate_path = std::move(path);
    value.expected_size = s.size_bytes;
    const auto checksum = parseLegacyResourceChecksum(s.checksum);
    if (checksum.content_digest) {
        value.expected_algorithm = checksum.algorithm;
        value.expected_hash = resourceDiscoveryHash(checksum.algorithm, checksum.value);
    }
    if (value.candidate_path.empty()) {
        value.result_code = s.status == 4 ? "restricted_resource" : "path_unresolved";
        return value;
    }
    if (!safeResourceDiscoveryPath(value.candidate_path)) throw std::invalid_argument("invalid discovery candidate");
    if (object.state == "missing") {
        value.grade = "missing";
        value.result_code = "object_missing";
        return value;
    }
    if (object.state == "directory") {
        value.grade = "conflict";
        value.result_code = "not_a_file";
        return value;
    }
    if (object.state != "found" || !object.size_bytes) {
        value.grade = "unknown";
        value.result_code = "metadata_unavailable";
        return value;
    }
    value.observed_size = object.size_bytes;
    for (const auto &algorithm : {value.expected_algorithm, std::string("sha256"), std::string("md5"), std::string("sha1")}) {
        const auto found = object.hashes.find(algorithm);
        if (found == object.hashes.end()) continue;
        const auto hash = resourceDiscoveryHash(algorithm, found->second);
        if (hash.empty()) continue;
        value.observed_algorithm = algorithm;
        value.observed_hash = hash;
        break;
    }
    if ((value.expected_size && value.expected_size != *value.observed_size) || (!value.expected_hash.empty() && value.observed_algorithm == value.expected_algorithm && value.observed_hash != value.expected_hash)) {
        value.grade = "conflict";
        value.result_code = "content_mismatch";
    } else if (!value.expected_hash.empty() && value.observed_algorithm == value.expected_algorithm) {
        value.grade = "hash_matched";
        value.result_code = "metadata_hash_matched";
    } else {
        value.grade = "observed";
        value.result_code = "object_observed";
    }
    return value;
}
} // namespace orchestrator::modules::crawler::application::model
