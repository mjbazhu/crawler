#pragma once

#include "crawler/application/model/ResourceTransfer.h"

#include "crawler/application/ports/diagnostics/DiagnosticsSink.h"
#include "crawler/application/ports/execution/TaskExecutor.h"
#include "crawler/application/ports/persistence/BatchTaskRepository.h"
#include "crawler/application/ports/persistence/ResourceTransferAccess.h"
#include "crawler/application/ports/storage/CloudFileStore.h"
#include "crawler/application/use_cases/MetadataCoverService.h"
#include "crawler/infrastructure/http/client/HttpClient.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlConnectionPool.h"
#include "crawler/infrastructure/storage/filesystem/FileStorage.h"

#include <chrono>
#include <memory>
#include <optional>

namespace orchestrator::modules::crawler::infrastructure::app {

// 组合独立本地封面用例；SQLite 兼容装配返回空值。
std::shared_ptr<application::use_cases::MetadataCoverService> makeMetadataCoverService(
    std::shared_ptr<music::persistence::mysql::MySqlConnectionPool> pool,
    std::shared_ptr<storage::filesystem::FileStorage> files,
    std::shared_ptr<http::client::HttpClient> client,
    std::shared_ptr<application::ports::TaskExecutor> executor,
    std::shared_ptr<application::ports::DiagnosticsSink> diagnostics,
    std::shared_ptr<application::ports::BatchTaskRepository> batches,
    std::chrono::milliseconds timeout,
    std::shared_ptr<application::ports::CloudFileStore> cloud = {},
    int cloud_timeout_ms = 120000,
    std::shared_ptr<application::ports::ResourceTransferAccess> access = {},
    std::optional<application::model::ResourceDestination> legacy_provider = std::nullopt
);

} // namespace orchestrator::modules::crawler::infrastructure::app
