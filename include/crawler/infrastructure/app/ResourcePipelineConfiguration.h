#pragma once

#include "crawler/infrastructure/app/ServerApplication.h"

namespace orchestrator::modules::crawler::infrastructure::app {

// 在既有 RC 配置读取后解析新增环境；格式非法时拒绝，不静默使用默认值。
void readResourcePipelineEnvironment(ServerApplication::Config &config);
// 验证三路结构和全局预算；单端目标可用性由实际身份核验返回。
void validateResourcePipelineConfiguration(const ServerApplication::Config &config);

} // namespace orchestrator::modules::crawler::infrastructure::app
