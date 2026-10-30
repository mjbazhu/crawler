#pragma once

#include "crawler/application/model/ResourceTransferQuery.h"
#include <cstdint>
#include <string>
#include <vector>

namespace orchestrator::modules::crawler::application::model {
// 批量附加资源列表事实；身份按请求顺序返回，不含任意路径。
struct ResourceStatusQuery {
    std::vector<ResourceIdentity> resources;       // 1..200个不同文件身份
    std::vector<ResourceDestination> destinations; // 聚合目标；省略时全部三端
};
// 本地最后确认事实；current不承诺文件此刻仍在线。
struct ResourceLocalReceiptView {
    int status = 0;                                 // 原生状态，不能替代版本与摘要校验
    ResourceTransferVersion confirmed {0, 0};       // 最后确认版本
    ResourceTransferVersion current_version {0, 0}; // 当前内容与本地目标版本
    std::uint64_t size_bytes = 0;                   // 已保存文件大小
    std::string checksum;                           // 原样保留兼容校验值，不冒充SHA256
    bool path_resolved = false;                     // 不公开实际路径
    bool current = false;                           // 当前代次、目标和SHA256参考一致
    bool transfer_skipped = false;                  // 有效历史策略，与current分开
};
// 未找到的身份也保留占位，避免列表位置错配。
struct ResourceStatusView {
    ResourceIdentity resource;                            // 完整文件身份
    bool found = false;                                   // 资源行是否存在
    ResourceLocalReceiptView local;                       // 本地事实
    std::vector<ResourceCloudReceiptView> cloud_receipts; // 已有云端回执；缺失由协议返回null
    bool all_requested_targets_current = false;           // 只有所选端均有有效核验事实才成立
};
// 只读批量结果；没有操作ID，不触发任何任务。
struct ResourceStatusBatch {
    std::vector<ResourceDestination> requested_targets; // 本次聚合范围，不从运行配置猜测
    std::vector<ResourceStatusView> items;              // 同一数据库快照，按请求顺序
};
// 校验有界身份并展开默认目标；不改变资源顺序。
void normalizeResourceStatusQuery(ResourceStatusQuery &query);
} // namespace orchestrator::modules::crawler::application::model
