#pragma once

#include "crawler/application/ports/persistence/ResourceTransferAccess.h"

#include <atomic>
#include <map>
#include <stdexcept>

namespace orchestrator::modules::crawler::tests {

// 只在运行前配置的准入替身；调用计数可跨线程读取。
class TransferAccess final : public application::ports::ResourceTransferAccess {
public:
    application::ports::ResourceTransferAdmission value;                                                      // 固定策略，任务运行时不修改
    std::map<application::model::ResourceDestination, application::ports::ResourceTransferAdmission> targets; // 可分别覆盖目标
    bool allowed = true;                                                                                      // 总闸门
    bool ready = true;                                                                                        // 特定目标是否可执行
    bool fault = false;                                                                                       // 模拟数据库不可用
    bool throws = false;                                                                                      // 模拟未捕获的依赖异常
    std::atomic<int> inspections = 0;                                                                         // 逐目标查询次数
    std::atomic<int> write_checks = 0;                                                                        // 写入检查次数
    // 默认是有效的历史跳过，维护仍关闭。
    TransferAccess()
    {
        value.skip = {{1, 1}, "reviewed-fixture", "legacy_both_clouds_present", true};
    }
    // 不执行任何文件、网络或数据库 I/O。
    application::result::ServiceResult<application::ports::ResourceTransferAdmission> inspect(application::model::ResourceIdentity, application::model::ResourceDestination destination) override
    {
        ++inspections;
        using Result = application::result::ServiceResult<application::ports::ResourceTransferAdmission>;
        const auto found = targets.find(destination);
        return fault ? Result::failure(application::result::ServiceErrorType::InternalError, "fixture unavailable") : Result::success(found == targets.end() ? value : found->second);
    }
    // 错误与关闭分别模拟，两者均不能放行。
    application::result::ServiceResult<bool> writesAllowed() override
    {
        ++write_checks;
        if (throws) throw std::runtime_error("private dependency diagnostic");
        using Result = application::result::ServiceResult<bool>;
        return fault ? Result::failure(application::result::ServiceErrorType::InternalError, "fixture unavailable") : Result::success(allowed);
    }
    // 不允许另一目标的活动状态替代当前目标。
    application::result::ServiceResult<bool> destinationReady(application::model::ResourceDestination) override
    {
        return application::result::ServiceResult<bool>::success(ready);
    }
};
} // namespace orchestrator::modules::crawler::tests
