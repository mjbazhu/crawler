#include "crawler/infrastructure/storage/filesystem/ResourceHistoryManifest.h"
#include <nlohmann/json.hpp>
#include <map>
#include <stdexcept>
#include <utility>
namespace orchestrator::modules::crawler::infrastructure::storage::filesystem {
namespace {
namespace model = application::model;
using Json = nlohmann::json;
// 限制单行内存；不向上层回显原始JSON或路径。
std::optional<Json> line(std::istream &input)
{
    std::string text;
    char ch;
    while (input.get(ch)) {
        if (ch == '\n') break;
        if (text.size() >= 65536) throw std::invalid_argument("history line too large");
        text += ch;
    }
    if (!input && !input.eof()) throw std::runtime_error("history input read failed");
    if (text.empty() && input.eof()) return {};
    auto value = Json::parse(text, nullptr, false);
    if (!value.is_object()) throw std::invalid_argument("invalid history JSON line");
    return value;
}
// 报告只接受固定字段，输出时重新序列化白名单，不复制任意附加文本。
model::ResourceDiscoveryObservation observation(const Json &j)
{
    model::ResourceDiscoveryObservation o;
    const auto owner = model::parseResourceOwner(j.at("owner_type").get<std::string>());
    if (!owner) throw std::invalid_argument("invalid history owner");
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
    const auto hash = [](const std::string &s) { return !s.empty() && model::resourceDiscoveryHash("sha256", s) == s; };
    const bool grade = o.grade == "observed" || o.grade == "hash_matched" || o.grade == "unknown" || o.grade == "missing" || o.grade == "conflict" || o.grade == "unresolved" || o.grade == "source_changed";
    if (!o.resource.resource_id || !o.generation || !hash(o.source_identity) || !hash(o.values_digest) || !grade || (!o.candidate_path.empty() && !model::safeResourceDiscoveryPath(o.candidate_path)) || o.result_code.size() > 64 || (!o.expected_hash.empty() && model::resourceDiscoveryHash(o.expected_algorithm, o.expected_hash) != o.expected_hash) || (!o.observed_hash.empty() && model::resourceDiscoveryHash(o.observed_algorithm, o.observed_hash) != o.observed_hash)) throw std::invalid_argument("invalid history observation");
    return o;
}
// 与发现持久观察相同的白名单数据，不把MD5变成SHA256。
Json json(const model::ResourceDiscoveryObservation &o)
{
    return {{"owner_type", model::resourceOwnerName(o.resource.owner)}, {"resource_id", o.resource.resource_id}, {"content_generation", o.generation}, {"source_identity", o.source_identity}, {"values_digest", o.values_digest}, {"candidate_path", o.candidate_path}, {"grade", o.grade}, {"result_code", o.result_code}, {"expected_size", o.expected_size}, {"expected_algorithm", o.expected_algorithm}, {"expected_hash", o.expected_hash}, {"observed_size", o.observed_size ? Json(*o.observed_size) : Json(nullptr)}, {"observed_algorithm", o.observed_algorithm}, {"observed_hash", o.observed_hash}, {"observed_at", o.observed_at}};
}
// 固定单端清单头，清单自身SHA由调用者另行绑定。
Json header(const model::ResourceHistoryAdoptionPlan &p)
{
    return {{"format", "crawler.resource-history-adoption.v1"}, {"run_id", p.run_id}, {"report_sha256", p.report_hash}, {"discovery_run_id", p.discovery_run_id}, {"discovery_definition_hash", p.discovery_definition_hash}, {"database_identity", p.database_identity}, {"owner_type", model::resourceOwnerName(p.owner)}, {"destination", model::resourceDestinationName(p.destination)}, {"target_revision", p.target_revision}, {"target_fingerprint", p.target_fingerprint}, {"high_water_id", p.high_water_id}, {"report_count", p.report_count}, {"review_required", true}};
}
} // namespace
application::model::ResourceHistoryAdoptionPlan prepareResourceHistoryManifest(std::istream &report, std::string report_hash, std::string run_id, std::ostream &manifest)
{
    const auto input = line(report);
    if (!input || input->value("format", std::string {}) != "crawler.resource-cloud-discovery.v1") throw std::invalid_argument("expected discovery report");
    const auto &d = input->at("definition");
    model::ResourceHistoryAdoptionPlan p;
    p.run_id = std::move(run_id);
    p.manifest_hash = report_hash;
    p.report_hash = std::move(report_hash);
    p.discovery_run_id = d.at("run_id").get<std::string>();
    p.discovery_definition_hash = input->at("definition_hash").get<std::string>();
    p.database_identity = d.at("database_identity").get<std::string>();
    const auto owner = model::parseResourceOwner(d.at("owner_type").get<std::string>());
    const auto destination = model::parseCloudProvider(d.at("destination").get<std::string>());
    if (!owner || !destination) throw std::invalid_argument("invalid discovery report scope");
    p.owner = *owner;
    p.destination = *destination;
    p.target_revision = d.at("target_revision").get<std::uint64_t>();
    p.target_fingerprint = d.at("target_fingerprint").get<std::string>();
    p.high_water_id = input->at("cursor").get<std::uint64_t>();
    p.report_count = input->at("processed").get<std::uint64_t>();
    model::validateResourceHistoryAdoptionPlan(p);
    manifest << header(p).dump() << '\n';
    std::uint64_t previous = 0, count = 0;
    std::map<std::string, std::uint64_t> grades;
    while (const auto row = line(report)) {
        const auto o = observation(*row);
        if (o.resource.owner != p.owner || o.resource.resource_id <= previous || o.resource.resource_id > p.high_water_id) throw std::invalid_argument("discovery report order changed");
        previous = o.resource.resource_id;
        ++count;
        ++grades[o.grade];
        manifest << Json {{"action", o.grade == "observed" || o.grade == "hash_matched" ? "adopt" : "review"}, {"observation", json(o)}}.dump() << '\n';
        if (!manifest) throw std::runtime_error("history manifest write failed");
    }
    if (count != p.report_count) throw std::invalid_argument("discovery report count mismatch");
    for (const auto &[grade, value] : input->at("counts").items())
        if (value.get<std::uint64_t>() != grades[grade]) throw std::invalid_argument("discovery report grades mismatch");
    // 草案尚未审阅，报告摘要不能充当将来清单自身的摘要。
    p.manifest_hash.clear();
    return p;
}
application::model::ResourceHistoryAdoptionPlan readResourceHistoryManifest(std::istream &manifest, std::string manifest_hash)
{
    const auto input = line(manifest);
    if (!input || input->value("format", std::string {}) != "crawler.resource-history-adoption.v1") throw std::invalid_argument("expected reviewed history manifest");
    const auto &j = *input;
    model::ResourceHistoryAdoptionPlan p;
    p.manifest_hash = std::move(manifest_hash);
    p.run_id = j.at("run_id").get<std::string>();
    p.report_hash = j.at("report_sha256").get<std::string>();
    p.discovery_run_id = j.at("discovery_run_id").get<std::string>();
    p.discovery_definition_hash = j.at("discovery_definition_hash").get<std::string>();
    p.database_identity = j.at("database_identity").get<std::string>();
    const auto owner = model::parseResourceOwner(j.at("owner_type").get<std::string>());
    const auto destination = model::parseCloudProvider(j.at("destination").get<std::string>());
    if (!owner || !destination) throw std::invalid_argument("invalid history manifest scope");
    p.owner = *owner;
    p.destination = *destination;
    p.target_revision = j.at("target_revision").get<std::uint64_t>();
    p.target_fingerprint = j.at("target_fingerprint").get<std::string>();
    p.high_water_id = j.at("high_water_id").get<std::uint64_t>();
    p.report_count = j.at("report_count").get<std::uint64_t>();
    model::validateResourceHistoryAdoptionPlan(p);
    return p;
}
std::optional<application::model::ResourceHistoryAdoptionPage> readResourceHistoryPage(std::istream &manifest, const application::model::ResourceHistoryAdoptionPlan &p, std::uint64_t after)
{
    if (after > p.high_water_id) throw std::invalid_argument("invalid history cursor");
    if (manifest.peek() == std::char_traits<char>::eof() && after == p.high_water_id) return {};
    model::ResourceHistoryAdoptionPage page;
    page.after_id = after;
    auto previous = after;
    for (int i = 0; i < 200; ++i) {
        const auto input = line(manifest);
        if (!input) break;
        model::ResourceHistoryAdoptionRow row;
        row.observation = observation(input->at("observation"));
        row.action = input->at("action").get<std::string>();
        if (row.observation.resource.owner != p.owner || row.observation.resource.resource_id <= previous || row.observation.resource.resource_id > p.high_water_id || (row.action != "adopt" && row.action != "review" && row.action != "allow_transfer")) throw std::invalid_argument("invalid history row decision or order");
        previous = row.observation.resource.resource_id;
        page.rows.push_back(std::move(row));
    }
    page.next_id = manifest.peek() == std::char_traits<char>::eof() ? p.high_water_id : previous;
    if (p.high_water_id && page.next_id <= after) throw std::invalid_argument("history cursor did not advance");
    return page;
}
void validateResourceHistoryManifest(std::istream &manifest, const application::model::ResourceHistoryAdoptionPlan &p)
{
    std::uint64_t after = 0, count = 0;
    while (const auto page = readResourceHistoryPage(manifest, p, after)) {
        after = page->next_id;
        count += page->rows.size();
    }
    if (after != p.high_water_id || count != p.report_count) throw std::invalid_argument("history manifest truncated or count changed");
}
} // namespace orchestrator::modules::crawler::infrastructure::storage::filesystem
