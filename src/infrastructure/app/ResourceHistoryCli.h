#pragma once
#include "crawler/infrastructure/music/persistence/mysql/MySqlConnectionPool.h"
#include <map>
#include <memory>
#include <string>
#include <string_view>
namespace orchestrator::modules::crawler::infrastructure::app {
// 维护CLI，不装配网盘、内容来源或后台worker。
int resourceHistoryCommand(std::string_view command, const std::map<std::string, std::string> &args, std::shared_ptr<music::persistence::mysql::MySqlConnectionPool> pool);
} // namespace orchestrator::modules::crawler::infrastructure::app
