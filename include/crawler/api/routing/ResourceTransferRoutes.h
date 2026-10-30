#pragma once

#include "crawler/api/routing/RouteSink.h"
#include "crawler/application/use_cases/ResourceTransferService.h"

#include <memory>

namespace orchestrator::modules::crawler::api::routing {

// 独立 v2 传输路由；旧协议的同步完成语义不复用为后台受理。
class ResourceTransferRoutes {
public:
    // 持有业务服务；缺依赖时仍注册端点并返回 503，处理器可安全跨请求调用。
    explicit ResourceTransferRoutes(std::shared_ptr<application::use_cases::ResourceTransferService> service);
    // 在 HTTP adapter 注册固定端点，维护检查由业务服务执行。
    void registerRoutes(RouteSink &sink) const;

private:
    // 注册批量只读状态，不经过执行准入闸门。
    void registerStatusRoutes(RouteSink &sink) const;
    void registerProducerRoutes(RouteSink &sink) const;
    std::shared_ptr<application::use_cases::ResourceTransferService> service_; // 线程安全业务服务
};

} // namespace orchestrator::modules::crawler::api::routing
