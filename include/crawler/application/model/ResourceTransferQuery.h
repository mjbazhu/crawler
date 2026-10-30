#pragma once

#include "crawler/application/model/ResourceTransfer.h"

#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace orchestrator::modules::crawler::application::model {

// 基于主键游标的有界查询，不从执行配置推断历史平台。
struct ResourceCloudReceiptQuery {
    ResourceOwner owner = ResourceOwner::Song;   // 必须指定所属资源类型
    std::optional<std::uint64_t> resource_id;    // 可选精确文件资源 ID
    std::optional<ResourceDestination> provider; // 可查询当前停用平台
    std::string task_state;                      // 可选精确任务状态
    std::string verification;                    // 可选精确核验级别
    std::uint64_t after_id = 0;                  // 主键游标，不使用递增 OFFSET
    int limit = 100;                             // 1..200
};

// 只暴露资源事实，不包含账号、URL、环境配置或未决对象路径。
struct ResourceCloudReceiptView {
    std::uint64_t id = 0;                                      // 稳定回执主键
    ResourceIdentity resource;                                 // 文件身份
    ResourceDestination provider = ResourceDestination::Baidu; // 实际平台
    std::string adapter;                                       // 历史确认使用的工具
    std::string task_state;                                    // 当前执行状态
    ResourceTransferVersion confirmed;                         // 最后确认的内容/目标版本
    ResourceTransferVersion current_version;                   // 当前资源和目标版本
    std::uint64_t size_bytes = 0;                              // 最后确认的大小
    std::string checksum;                                      // 最后确认的完整 SHA-256，未知为空
    std::string verification;                                  // 保留声明与实际核验的区别
    std::string verification_method;                           // 最后实际核验方法
    std::optional<std::uint64_t> uploaded_at;                  // 没有真实证据时为空
    std::optional<std::uint64_t> verified_at;                  // 实际核验时间
    std::string result_code;                                   // 固定脱敏任务结果
    bool path_resolved = false;                                // 不向协议暴露内部对象路径
    bool current = false;                                      // 版本及强内容参考匹配，不等于远端在线
    std::optional<bool> matches_local;                         // 缺少当前本地摘要时保持未知
    bool transfer_skipped = false;                             // 当前版本的有效持久跳过策略
    bool assumed_stored = false;                               // 人工采信，不能冒充实际核验
};

// 一页至多 200 条，后续 cursor 仅在确有下一页时给出。
struct ResourceCloudReceiptPage {
    std::vector<ResourceCloudReceiptView> items; // 同一查询快照
    std::optional<std::uint64_t> next_after_id;  // 下一页游标
};

} // namespace orchestrator::modules::crawler::application::model
