#include "MySqlResourceDiscoverySupport.h"
#include <openssl/evp.h>
#include <array>
#include <stdexcept>
namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::discovery {
nlohmann::json definitionJson(const model::ResourceDiscoveryDefinition &d)
{
    return {{"run_id", d.run_id}, {"owner_type", model::resourceOwnerName(d.owner)}, {"destination", model::resourceDestinationName(d.destination)}, {"database_identity", d.database_identity}, {"target_revision", d.target_revision}, {"target_fingerprint", d.target_fingerprint}, {"high_water_id", d.high_water_id}, {"path_source", d.path_source}, {"strip_prefix", d.strip_prefix}, {"page_size", d.page_size}, {"concurrency", d.concurrency}, {"object_timeout_ms", d.object_timeout_ms}};
}
model::ResourceDiscoveryDefinition definitionFromJson(const nlohmann::json &j)
{
    model::ResourceDiscoveryDefinition d;
    d.run_id = j.at("run_id").get<std::string>();
    const auto owner = model::parseResourceOwner(j.at("owner_type").get<std::string>());
    const auto destination = model::parseCloudProvider(j.at("destination").get<std::string>());
    if (!owner || !destination) throw std::logic_error("invalid discovery scope");
    d.owner = *owner;
    d.destination = *destination;
    d.database_identity = j.at("database_identity").get<std::string>();
    d.target_revision = j.at("target_revision").get<std::uint64_t>();
    d.target_fingerprint = j.at("target_fingerprint").get<std::string>();
    d.high_water_id = j.at("high_water_id").get<std::uint64_t>();
    d.path_source = j.at("path_source").get<std::string>();
    d.strip_prefix = j.at("strip_prefix").get<std::string>();
    d.page_size = j.at("page_size").get<int>();
    d.concurrency = j.at("concurrency").get<int>();
    d.object_timeout_ms = j.at("object_timeout_ms").get<int>();
    model::validateResourceDiscoveryDefinition(d);
    return d;
}
nlohmann::json observationJson(const model::ResourceDiscoveryObservation &o)
{
    if (!o.resource.resource_id || !o.generation || !transfer_jobs::hash(o.source_identity) || !transfer_jobs::hash(o.values_digest) || (!o.candidate_path.empty() && !model::safeResourceDiscoveryPath(o.candidate_path)) || !emptyCounts().contains(o.grade) || !transfer_jobs::code(o.result_code) || (!o.expected_hash.empty() && model::resourceDiscoveryHash(o.expected_algorithm, o.expected_hash) != o.expected_hash) || (!o.observed_hash.empty() && model::resourceDiscoveryHash(o.observed_algorithm, o.observed_hash) != o.observed_hash)) throw std::invalid_argument("invalid discovery observation");
    return {{"owner_type", model::resourceOwnerName(o.resource.owner)}, {"resource_id", o.resource.resource_id}, {"content_generation", o.generation}, {"source_identity", o.source_identity}, {"values_digest", o.values_digest}, {"candidate_path", o.candidate_path}, {"grade", o.grade}, {"result_code", o.result_code}, {"expected_size", o.expected_size}, {"expected_algorithm", o.expected_algorithm}, {"expected_hash", o.expected_hash}, {"observed_size", o.observed_size ? nlohmann::json(*o.observed_size) : nlohmann::json(nullptr)}, {"observed_algorithm", o.observed_algorithm}, {"observed_hash", o.observed_hash}, {"observed_at", o.observed_at}};
}
model::ResourceDiscoveryObservation observationFromJson(const nlohmann::json &j)
{
    model::ResourceDiscoveryObservation o;
    const auto owner = model::parseResourceOwner(j.at("owner_type").get<std::string>());
    if (!owner) throw std::logic_error("invalid observation owner");
    o.resource = {*owner, j.at("resource_id").get<std::uint64_t>()};
    o.generation = j.at("content_generation").get<std::uint64_t>();
    o.source_identity = j.at("source_identity").get<std::string>();
    o.values_digest = j.at("values_digest").get<std::string>();
    o.candidate_path = j.at("candidate_path").get<std::string>();
    o.grade = j.at("grade").get<std::string>();
    o.result_code = j.at("result_code").get<std::string>();
    o.expected_size = j.at("expected_size").get<std::uint64_t>();
    o.expected_algorithm = j.at("expected_algorithm").get<std::string>();
    o.expected_hash = j.at("expected_hash").get<std::string>();
    if (!j.at("observed_size").is_null()) o.observed_size = j.at("observed_size").get<std::uint64_t>();
    o.observed_algorithm = j.at("observed_algorithm").get<std::string>();
    o.observed_hash = j.at("observed_hash").get<std::string>();
    o.observed_at = j.at("observed_at").get<std::uint64_t>();
    (void)observationJson(o);
    return o;
}
std::string digest(const nlohmann::json &j)
{
    const auto text = j.dump();
    std::array<unsigned char, 32> hash {};
    unsigned int size = 0;
    if (EVP_Digest(text.data(), text.size(), hash.data(), &size, EVP_sha256(), nullptr) != 1 || size != hash.size()) throw std::runtime_error("discovery digest failed");
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    for (auto b : hash) {
        result += hex[b >> 4];
        result += hex[b & 15];
    }
    return result;
}
std::map<std::string, std::uint64_t> emptyCounts()
{
    return {{"observed", 0}, {"hash_matched", 0}, {"conflict", 0}, {"missing", 0}, {"unknown", 0}, {"unresolved", 0}, {"source_changed", 0}};
}
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::discovery
