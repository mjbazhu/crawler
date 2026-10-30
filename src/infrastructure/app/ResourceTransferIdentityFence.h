#pragma once

#include "crawler/application/ports/provider/ResourceTransferSource.h"
#include "crawler/application/ports/storage/ResourceTransferDestination.h"
#include "crawler/infrastructure/app/ResourceTargetVerifier.h"

#include <functional>
#include <memory>

namespace orchestrator::modules::crawler::infrastructure::app {

// 只在来源/准备/发布边界核验，不在每个正文分片回调里发网络请求。
using ResourceTargetProbe = std::function<ResourceTargetVerification(const application::model::ResourceTransferContext &)>;
// 把实际目标核验附加到现有窄 port，业务 worker 不依赖厂商协议。
std::shared_ptr<application::ports::ResourceTransferSource> fenceResourceSource(std::shared_ptr<application::ports::ResourceTransferSource> source, ResourceTargetProbe probe);
std::shared_ptr<application::ports::ResourceTransferDestination> fenceResourceDestination(std::shared_ptr<application::ports::ResourceTransferDestination> destination, ResourceTargetProbe probe);

} // namespace orchestrator::modules::crawler::infrastructure::app
