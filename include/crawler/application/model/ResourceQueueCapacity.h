#pragma once
#include "crawler/application/model/ResourceTransfer.h"
#include <cstdint>
namespace orchestrator::modules::crawler::application::model {
// 全实例共享的容量，计入未决和等待输入，不计历史终态。
struct ResourceQueueCapacity {
    ResourceDestination destination = ResourceDestination::Local; // 独立目标
    std::uint64_t transfer_limit = 10000;                         // 在队文件任务上限
    std::uint64_t producer_limit = 10000;                         // 在队生成计划上限
    std::uint64_t version = 1;                                    // 并发配置修订
    std::uint64_t transfers = 0;                                  // 有界读取的任务数量下界
    std::uint64_t producers = 0;                                  // 有界读取的生成数量下界
    bool transfers_exact = true;                                  // 达到limit+1时不能证明精确数量
    bool producers_exact = true;                                  // 达到limit+1时不能证明精确数量
};
} // namespace orchestrator::modules::crawler::application::model
