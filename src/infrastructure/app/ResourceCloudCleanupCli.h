#pragma once
#include "crawler/infrastructure/music/persistence/mysql/MySqlConnectionPool.h"
#include <map>
#include <memory>
#include <string>
#include <string_view>
namespace orchestrator::modules::crawler::infrastructure::app {
// 显式库维护CLI，只捕获删除前证据和查询预演，没有远端执行能力。
int resourceCloudCleanupCommand(std::string_view command, const std::map<std::string, std::string> &args, std::shared_ptr<music::persistence::mysql::MySqlConnectionPool> pool);
} // namespace orchestrator::modules::crawler::infrastructure::app
