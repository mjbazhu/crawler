#pragma once

#include "crawler/application/model/ResourceTransfer.h"
#include "crawler/application/ports/persistence/ResourceTransferAccess.h"
#include "crawler/application/result/ServiceResult.h"
#include "crawler/application/use_cases/ResourceTransferPolicy.h"

#include <string>

namespace orchestrator::modules::crawler::application::use_cases {

// 执行前判断；Transfer 是唯一允许后续内容 I/O 的结果。
struct ResourceTransferPreflight {
    ResourceWorkDecision decision = ResourceWorkDecision::Blocked; // 目标是否可以执行
    model::ResourceTransferVersion version;                        // 冻结的内容/目标边界
    std::string result_code;                                       // 不包含基础设施细节的稳定结果
};

// 无准入 port 仅供旧兼容装配；正式 MySQL 装配必须注入实时查询。
result::ServiceResult<ResourceTransferPreflight> preflightResourceTransfer(ports::ResourceTransferAccess *access, model::ResourceIdentity identity, model::ResourceDestination destination, bool cancelled = false);

} // namespace orchestrator::modules::crawler::application::use_cases
