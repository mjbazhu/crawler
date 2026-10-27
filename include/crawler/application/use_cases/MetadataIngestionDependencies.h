#pragma once

#include "crawler/application/ports/persistence/MetadataBatchWriter.h"
#include "crawler/application/ports/diagnostics/DiagnosticsSink.h"

#include <memory>

namespace orchestrator::modules::crawler::application::use_cases {

// 元数据入库依赖组；只保存实体、关系和资源地址，不触发二进制下载。
struct MetadataIngestionDependencies {
    std::shared_ptr<application::ports::MetadataBatchWriter> writer;  // 原子元数据写入 port
    std::shared_ptr<application::ports::DiagnosticsSink> diagnostics; // 结构化写入失败诊断
};

} // namespace orchestrator::modules::crawler::application::use_cases
