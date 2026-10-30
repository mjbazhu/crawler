#pragma once

#include "crawler/infrastructure/music/persistence/mysql/MySqlConnectionPool.h"

#include <map>
#include <memory>
#include <string>

namespace orchestrator::modules::crawler::infrastructure::app {
// 维护 CLI 的显式过期认领恢复，不能启动传输或冒充已停止外部 writer。
int resourceRecoveryCommand(const std::map<std::string, std::string> &options, std::shared_ptr<music::persistence::mysql::MySqlConnectionPool> pool);
} // namespace orchestrator::modules::crawler::infrastructure::app
