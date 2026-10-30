#pragma once

#include "crawler/application/model/LyricArtifacts.h"
#include "crawler/application/model/ResourceTransferExecution.h"
#include "crawler/application/result/ServiceResult.h"

namespace orchestrator::modules::crawler::application::ports {

// 从已受控获取的私有输入生成实际内容参考；不登记数据库或正式 local。
class ResourceArtifactGenerator {
public:
    virtual ~ResourceArtifactGenerator() = default;
    virtual result::ServiceResult<model::LyricArtifactBundle> generate(const model::ResourceTransferJob &source_job, const model::ResourceContentSnapshot &source, const model::ResourceTransferContext &context) = 0;
};

} // namespace orchestrator::modules::crawler::application::ports
