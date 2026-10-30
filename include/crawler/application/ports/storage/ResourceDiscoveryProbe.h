#pragma once
#include "crawler/application/model/ResourceDiscovery.h"
#include "crawler/application/model/ResourceTransferExecution.h"
#include "crawler/application/result/ServiceResult.h"
#include <string>
namespace orchestrator::modules::crawler::application::ports {
// 线程安全的单目标只读探测；不提供正文回读或写入能力。
class ResourceDiscoveryProbe {
public:
    // 释放适配器。
    virtual ~ResourceDiscoveryProbe() = default;
    // 与运行时相同的真实身份核对，不依据remote名猜平台。
    virtual result::ServiceResult<std::string> fingerprint(const model::ResourceTransferContext &context) = 0;
    // 只查询精确相对对象元信息，未知绝不当成缺失。
    virtual result::ServiceResult<model::ResourceObjectObservation> observe(std::string path, const model::ResourceTransferContext &context) = 0;
};
} // namespace orchestrator::modules::crawler::application::ports
