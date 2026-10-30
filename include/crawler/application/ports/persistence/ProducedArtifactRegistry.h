#pragma once

#include "crawler/application/model/LyricArtifacts.h"
#include "crawler/application/model/ResourceTransferExecution.h"
#include "crawler/application/result/ServiceResult.h"

#include <cstdint>
#include <vector>

namespace orchestrator::modules::crawler::application::ports {

// 只登记派生产物身份和实测参考，不把 producer 临时文件冒充正式本地回执。
class ProducedArtifactRegistry {
public:
    virtual ~ProducedArtifactRegistry() = default;
    // 短事务核对仍有效的父任务并绑定 MRC 参考；bundle.files 的 local_path 必须为空。
    virtual result::ServiceResult<std::vector<model::ResourceIdentity>> record(const model::ResourceTransferJob &parent, const model::LyricArtifactBundle &bundle, std::uint64_t source_size, const model::ResourceTransferContext &context) = 0;
};

} // namespace orchestrator::modules::crawler::application::ports
