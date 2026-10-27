#pragma once

#include "crawler/application/use_cases/CrawlerServiceDependencies.h"
#include "crawler/application/use_cases/MusicCollectionWorkflow.h"

#include <memory>

namespace orchestrator::modules::crawler::application::use_cases {

// 只装配 application 对象，把实体补齐策略留在专门流程中。
std::shared_ptr<MusicCollectionWorkflow> assembleCollectionWorkflows(const CrawlerServiceDependencies &dependencies, std::shared_ptr<MetadataPersistenceService> persistence, std::shared_ptr<application::ports::DiagnosticsSink> diagnostics);

} // namespace orchestrator::modules::crawler::application::use_cases
