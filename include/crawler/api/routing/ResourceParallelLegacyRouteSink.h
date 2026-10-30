#pragma once

#include "crawler/api/routing/RouteSink.h"
#include "crawler/application/ports/persistence/ResourceTransferAccess.h"

#include <memory>
#include <string_view>

namespace orchestrator::modules::crawler::api::routing {

// 已完成迁接的旧治理入口白名单；新增写路由默认拒绝，避免意外绕过版本保护。
bool supportsParallelLegacyGovernance(std::string_view method, std::string_view path);

// 并行模式保留已迁接旧语义，并通过持久准入保护元数据和治理写入。
class ResourceParallelLegacyRouteSink final : public RouteSink {
public:
    // 装饰本次注册，不持有服务或开启后台工作。
    explicit ResourceParallelLegacyRouteSink(RouteSink &sink, bool compatibility = false, std::shared_ptr<application::ports::ResourceTransferAccess> access = {});
    // 已迁接治理入口须配置准入；未知旧写入继续返回固定 503。
    void route(const std::string &method, const std::string &path, RequestHandler handler) override;

private:
    RouteSink &sink_;                                                    // 生命周期由组合根的注册阶段保证
    bool compatibility_ = false;                                         // 只放行已经接入持久兼容映射的入口
    std::shared_ptr<application::ports::ResourceTransferAccess> access_; // 元数据与治理操作的持久闸门
};

} // namespace orchestrator::modules::crawler::api::routing
