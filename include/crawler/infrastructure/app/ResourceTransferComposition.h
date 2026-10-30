#pragma once

#include "crawler/application/ports/persistence/ResourceTransferJobRepository.h"
#include "crawler/application/ports/provider/MiguPacmtokenStore.h"
#include "crawler/infrastructure/app/ResourceTransferRuntime.h"
#include "crawler/infrastructure/app/ServerApplication.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlConnectionPool.h"

#include <memory>

namespace orchestrator::modules::crawler::infrastructure::app {

// 完成依赖装配并只读核验目标；返回的 runtime 尚未启动。
std::shared_ptr<ResourceTransferRuntime> makeResourceTransferRuntime(const ServerApplication::Config &config, std::shared_ptr<music::persistence::mysql::MySqlConnectionPool> pool, std::shared_ptr<application::ports::ResourceTransferJobRepository> jobs, std::shared_ptr<application::ports::MiguPacmtokenStore> tokens);

} // namespace orchestrator::modules::crawler::infrastructure::app
