#include "crawler/application/model/ResourceHistoryAdoption.h"
#include <algorithm>
#include <stdexcept>
namespace orchestrator::modules::crawler::application::model {
void validateResourceHistoryAdoptionPlan(const ResourceHistoryAdoptionPlan &p)
{
    const auto identifier = [](const std::string &s) { return !s.empty() && s.size() <= 96 && std::all_of(s.begin(), s.end(), [](unsigned char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' || c == '-' || c == '.'; }); };
    const auto hash = [](const std::string &s) { return !s.empty() && resourceDiscoveryHash("sha256", s) == s; };
    if (!identifier(p.run_id) || !identifier(p.discovery_run_id) || !hash(p.manifest_hash) || !hash(p.report_hash) || !hash(p.discovery_definition_hash) || !hash(p.database_identity) || !hash(p.target_fingerprint) || !p.target_revision || resourceOwnerName(p.owner).empty() || (p.destination != ResourceDestination::Baidu && p.destination != ResourceDestination::Gdrive) || (p.report_count && !p.high_water_id)) throw std::invalid_argument("invalid history adoption scope");
}
} // namespace orchestrator::modules::crawler::application::model
