#include "crawler/application/model/ResourceStatus.h"

#include <set>
#include <stdexcept>
#include <utility>

namespace orchestrator::modules::crawler::application::model {
void normalizeResourceStatusQuery(ResourceStatusQuery &query)
{
    if (query.resources.empty() || query.resources.size() > 200 || query.destinations.size() > 3) throw std::invalid_argument("invalid resource status scope");
    std::set<std::pair<ResourceOwner, std::uint64_t>> identities;
    for (const auto &identity : query.resources)
        if (!identity.resource_id || resourceOwnerName(identity.owner).empty() || !identities.emplace(identity.owner, identity.resource_id).second) throw std::invalid_argument("invalid resource status identity");
    if (query.destinations.empty()) query.destinations = {ResourceDestination::Local, ResourceDestination::Baidu, ResourceDestination::Gdrive};
    std::set<ResourceDestination> targets;
    for (const auto destination : query.destinations)
        if (resourceDestinationName(destination).empty() || !targets.insert(destination).second) throw std::invalid_argument("invalid resource status targets");
}
} // namespace orchestrator::modules::crawler::application::model
