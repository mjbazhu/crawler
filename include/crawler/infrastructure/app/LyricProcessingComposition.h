#pragma once

#include "crawler/application/use_cases/LyricProcessingWorkflow.h"

#include <filesystem>
#include <memory>
#include <string_view>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {
class MySqlConnectionPool; // 组合根允许持有具体基础设施类型
}

namespace orchestrator::modules::crawler::infrastructure::app {

// 装配在线与离线共用的歌词用例；不在业务层构造具体 adapter。
std::shared_ptr<application::use_cases::LyricProcessingWorkflow> makeMiguLyricProcessingWorkflow(const std::filesystem::path &root, std::string_view key, std::shared_ptr<music::persistence::mysql::MySqlConnectionPool> pool = nullptr);

} // namespace orchestrator::modules::crawler::infrastructure::app
