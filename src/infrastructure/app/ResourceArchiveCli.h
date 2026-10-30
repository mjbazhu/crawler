#pragma once
#include "crawler/infrastructure/music/persistence/mysql/MySqlConnectionPool.h"
#include <map>
#include <memory>
#include <string>
#include <string_view>
namespace orchestrator::modules::crawler::infrastructure::app {
// 显式数据库维护入口；历史运行命令才创建只读网盘探测器。
int resourceArchiveCommand(std::string_view command, const std::map<std::string, std::string> &args, std::shared_ptr<music::persistence::mysql::MySqlConnectionPool> pool);
} // namespace orchestrator::modules::crawler::infrastructure::app
