#pragma once

#include "crawler/application/model/ResourceTransfer.h"

namespace orchestrator::modules::crawler::application::use_cases {

// 认领前的决定；与实际传输结果及版权状态分开。
enum class ResourceWorkDecision { Disabled,
                                  SkipPolicy,
                                  Restricted,
                                  Cancelled,
                                  Blocked,
                                  AlreadyCurrent,
                                  Busy,
                                  WaitingInput,
                                  Unavailable,
                                  Transfer };

// 决策所需快照，不携带 I/O 对象或敏感配置。
struct ResourceTransferPolicyInput {
    model::ResourceTransferVersion version; // 当前内容和目标
    model::ResourceTransferSkip skip;       // 独立持久跳过策略
    bool enabled = true;                    // 本次是否选择目标
    bool cancelled = false;                 // 是否明确取消
    bool dispatch_ready = false;            // schema、基线及维护闸门是否就绪
    bool already_current = false;           // 版本/证据满足当前核验策略
    bool active = false;                    // 是否被其他操作认领
    bool waiting_input = false;             // 派生产物是否等待输入
    bool available = true;                  // 目标是否暂时可用
};

// 纯决策；调用者必须先执行它，再取得内容/配额或执行健康探测。
ResourceWorkDecision evaluateResourceTransfer(const ResourceTransferPolicyInput &input);

} // namespace orchestrator::modules::crawler::application::use_cases
