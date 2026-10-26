#pragma once

#include "crawler/application/ports/persistence/MetadataBatchWriter.h"
#include "crawler/application/ports/storage/MetadataCoverStore.h"
#include "crawler/application/ports/diagnostics/DiagnosticsSink.h"

#include <memory>

namespace orchestrator::modules::crawler::application::use_cases {

// 元数据入库依赖组；生产装配使用事务 writer 和封面保存器。
struct MetadataIngestionDependencies {
    std::shared_ptr<application::ports::MetadataBatchWriter> writer;  // 原子元数据写入 port
    std::shared_ptr<application::ports::MetadataCoverStore> covers;   // 封面完整保存与回滚 port
    std::shared_ptr<application::ports::DiagnosticsSink> diagnostics; // 结构化写入失败诊断
};

} // namespace orchestrator::modules::crawler::application::use_cases
