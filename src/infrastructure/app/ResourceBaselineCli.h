#pragma once
#include "crawler/infrastructure/music/persistence/mysql/MySqlConnectionPool.h"
#include <map>
#include <memory>
#include <string>
#include <string_view>
namespace orchestrator::modules::crawler::infrastructure::app {
// 维护 CLI 的基线子命令，不初始化 schema 或发起内容 I/O。
int resourceBaselineCommand(std::string_view command, const std::map<std::string, std::string> &args, std::shared_ptr<music::persistence::mysql::MySqlConnectionPool> pool);
} // namespace orchestrator::modules::crawler::infrastructure::app
