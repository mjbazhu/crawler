#pragma once

#include <map>
#include <string>
#include <string_view>

namespace orchestrator::modules::crawler::infrastructure::app {
// 独立schema维护，不启动HTTP、资源工厂、认领恢复或后台worker。
int resourceSchemaCommand(std::string_view command, const std::map<std::string, std::string> &options);
} // namespace orchestrator::modules::crawler::infrastructure::app
