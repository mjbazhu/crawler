#pragma once

#include "crawler/application/model/ResourceTransfer.h"
#include "crawler/application/result/ServiceResult.h"

#include <string>

namespace orchestrator::modules::crawler::application::ports {

// 当前数据库身份与策略快照；查阅本身不得获取内容或探测存储。
struct ResourceTransferAdmission {
    model::ResourceTransferVersion version;            // 当前内容代次和目标修订
    model::ResourceTransferSkip skip;                  // 与核验证据分开的持久策略
    bool dispatch_ready = false;                       // 本目标已封存且允许执行
    bool restricted = false;                           // 资源具有永久业务限制
    std::string blocked_code = "resource_maintenance"; // 不可执行的稳定原因
};

// 线程安全的执行准入 port；每次读取持久状态，不缓存维护或跳过结论。
class ResourceTransferAccess {
public:
    // 释放准入适配器。
    virtual ~ResourceTransferAccess() = default;
    // 查阅指定文件资源和目标；缺失或数据库异常必须失败关闭。
    virtual result::ServiceResult<ResourceTransferAdmission> inspect(model::ResourceIdentity identity, model::ResourceDestination destination) = 0;
    // 桥接期间源数据写入总闸门；maintenance 阻断，paused 的未启用目标不参与执行。
    virtual result::ServiceResult<bool> writesAllowed() = 0;
    // 目标暂停与全局维护分开；旧替身默认沿用总闸门。
    virtual result::ServiceResult<bool> destinationReady(model::ResourceDestination)
    {
        return writesAllowed();
    }
};

} // namespace orchestrator::modules::crawler::application::ports
