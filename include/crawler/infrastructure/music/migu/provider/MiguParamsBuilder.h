#pragma once

#include <string>

#include "crawler/infrastructure/music/migu/provider/MiguParams.h"

namespace orchestrator::modules::crawler::infrastructure::music::migu::provider {

// qstring
std::string toQueryString(const Params &p);

} // namespace orchestrator::modules::crawler::infrastructure::music::migu::provider
