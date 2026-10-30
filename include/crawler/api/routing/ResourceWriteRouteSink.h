#pragma once

#include "crawler/api/routing/RouteSink.h"
#include "crawler/application/ports/persistence/ResourceTransferAccess.h"

#include <memory>
#include <string>
#include <string_view>

namespace orchestrator::modules::crawler::api::routing {

// 方法使用注册时的大写形式；路由和 OpenAPI 复用同一保护范围。
bool requiresResourceWriteAdmission(std::string_view method, std::string_view path);

// 桥接入口装饰器；只读查询和停止操作保持可用，所有写入默认经过持久闸门。
class ResourceWriteRouteSink final : public RouteSink {
public:
    // 注册时注入后端 sink 与实时准入；请求处理器独立持有 access 生命周期。
    ResourceWriteRouteSink(RouteSink &sink, std::shared_ptr<application::ports::ResourceTransferAccess> access);
    // 精确保留方法/路径，包裹需要写入准入的 handler。
    void route(const std::string &method, const std::string &path, RequestHandler handler) override;

private:
    RouteSink &sink_;                                                    // 仅在注册期间借用
    std::shared_ptr<application::ports::ResourceTransferAccess> access_; // 线程安全的查询 port
};
} // namespace orchestrator::modules::crawler::api::routing
