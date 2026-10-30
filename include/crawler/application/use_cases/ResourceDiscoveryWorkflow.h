#pragma once
#include "crawler/application/ports/persistence/LegacyResourceImportRepository.h"
#include "crawler/application/ports/persistence/ResourceDiscoveryRepository.h"
#include "crawler/application/ports/storage/ResourceDiscoveryProbe.h"
#include <string>

namespace orchestrator::modules::crawler::application::use_cases {
// 无共享可变状态；多个进程通过持久页租约串行推进同一任务。
class ResourceDiscoveryWorkflow {
public:
    // 依赖窄只读探测端口，不拥有正文下载或传输准入能力。
    ResourceDiscoveryWorkflow(ports::ResourceDiscoveryRepository &runs, ports::LegacyResourceImportRepository &sources, ports::ResourceDiscoveryProbe &probe);
    // 最多推进一页；没有可认领页时返回当前快照。
    result::ServiceResult<model::ResourceDiscoveryRun> step(std::string id);

private:
    ports::ResourceDiscoveryRepository &runs_;       // 线程安全持久检查点
    ports::LegacyResourceImportRepository &sources_; // 只读来源快照
    ports::ResourceDiscoveryProbe &probe_;           // 线程安全单目标元数据
};
} // namespace orchestrator::modules::crawler::application::use_cases
