#pragma once

#include <string_view>

namespace orchestrator::modules::crawler::application::model {
// 复用既有永久限制码；关联种类发现不使音频限制失效。
inline bool isPermanentSongResourceSkip(std::string_view code)
{
    return code == "210001" || code == "440000" || code == "440015" || code == "440016";
}
} // namespace orchestrator::modules::crawler::application::model
