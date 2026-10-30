#pragma once

#include "crawler/application/model/ResourceTransferJobs.h"
#include "crawler/infrastructure/storage/rclone/RcloneConfig.h"

#include <array>
#include <cstdint>
#include <optional>
#include <string>

namespace orchestrator::modules::crawler::infrastructure::app {

// 单端启动配置；认证始终留在 infrastructure，不通过设置 API 返回。
struct ResourcePipelineTarget {
    bool enabled = false;                                        // 显式启用；离线与关闭不同
    int concurrency = 3;                                         // 本端 worker 上限
    application::model::ResourceTransferJobPolicy policy;        // 新受理任务的冻结预算
    std::string adapter;                                         // filesystem/baidupcs/rclone
    std::string staging_path;                                    // 本端独立来源临时目录
    std::uint64_t staging_max_bytes = 8ULL * 1024 * 1024 * 1024; // 本端临时快照配额
    std::optional<storage::rclone::RcloneConfig> rclone;         // 逐端覆盖，否则继承既有 RC 配置
};

// 三路配置与旧单端开关独立；默认不改变 legacy 执行模式。
struct ResourcePipelineConfig {
    std::string mode = "legacy";                                                                      // legacy/parallel
    std::array<ResourcePipelineTarget, 3> targets;                                                    // 按 Local/Baidu/Gdrive 索引
    int source_concurrency = 6;                                                                       // 三端合计来源配额
    int transfer_concurrency = 9;                                                                     // 三端合计传输配额
    int shutdown_grace_ms = 30000;                                                                    // 停止认领后允许在途完成的时间
    std::string verification_policy = "hash_match";                                                   // hash_match/strict_sha256
    bool producer_enabled = true;                                                                     // parallel MRC 建独立生成计划，缺密钥为 waiting_input
    int producer_concurrency = 1;                                                                     // 每目标独立 producer worker 数
    application::model::ResourceTransferJobPolicy producer_policy {120000, 2, 1024 * 1024, 0, false}; // 独立有界来源/生成预算
    std::string legacy_view_provider;                                                                 // 空表示不启用兼容写；local/baidu/gdrive 显式固定旧视图
};

} // namespace orchestrator::modules::crawler::infrastructure::app
