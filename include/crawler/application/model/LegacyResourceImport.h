#pragma once

#include "crawler/application/model/ResourceTransfer.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace orchestrator::modules::crawler::application::model {

// 已核实目标身份；导入只绑定版本，不连接目标。
struct LegacyResourceTarget {
    ResourceDestination destination = ResourceDestination::Local; // 目标枚举
    std::uint64_t revision = 1;                                   // 持久目标修订
    std::string fingerprint;                                      // 不含凭据的身份 SHA-256
    std::string dispatch_state;                                   // 导入时必须 maintenance
};

// 只读数据库生成的单个资源快照；不含远端临时 URL 或认证。
struct LegacyResourceSnapshot {
    ResourceIdentity identity;    // 文件资源身份
    std::uint64_t generation = 1; // 当前内容代次
    std::string source_identity;  // 稳定来源及类型的指纹
    std::string values_digest;    // 必要旧字段的指纹
    int status = 0;               // 旧本地状态，不改成核验成功
    std::string cloud_backend;    // 原上传工具
    std::string cloud_path;       // 原单份位置
    std::string local_path;       // 原本地/兼容路径
    std::uint64_t size_bytes = 0; // 原文件大小
    std::string checksum;         // 原算法不明的校验值
    std::string legacy_values;    // 白名单旧字段的序列化证据，只在基础设施处理
};

// 有界页及高水位；预览不改变策略或触发内容 I/O。
struct LegacyResourceImportPage {
    ResourceOwner owner = ResourceOwner::Song;   // 页的资源归属
    std::string database_identity;               // 实例和数据库身份指纹
    std::uint64_t after_id = 0;                  // 上一页已提交游标
    std::uint64_t high_water_id = 0;             // 本轮固定高水位
    std::uint64_t next_id = 0;                   // 本页结束游标
    std::array<LegacyResourceTarget, 3> targets; // local、baidu、gdrive 的配置快照
    std::vector<LegacyResourceSnapshot> rows;    // 包含不满足导入条件的行，避免游标漏项
};

// 已审阅的人工采信计划；不能通过普通 process 构造。
struct LegacyResourceImportPlan {
    std::string run_id;                                        // 清单业务标识，最多 96 字节
    std::string manifest_hash;                                 // 审阅后固定的完整清单 SHA-256
    bool operator_attested_both = false;                       // 明确确认本范围两端已保存
    std::optional<ResourceDestination> legacy_rclone_provider; // 只有明确映射才采用旧 rclone 路径
    bool operator_excluded_all = false;                        // 显式排除本范围，不能与双端声明并用
    std::uint64_t control_generation = 0;                      // 显式暂停/恢复后的页围栏，旧清单默认0
};

// 单页原子导入结果；声明跳过不统计为内容核验成功。
struct LegacyResourceImportResult {
    std::uint64_t next_id = 0;          // 已提交的游标
    std::size_t imported_resources = 0; // 建立/补充跳过策略的资源数
    std::size_t new_receipts = 0;       // 新增云端当前行数
    std::size_t conflicts = 0;          // 已持久化的变化、活动认领或不合格项
    std::size_t excluded_resources = 0; // 原本未完成、未被声明覆盖的资源
    bool already_applied = false;       // 同清单同页重跑未产生变化
};

// 双端共享的已提交进度；完成遍历不等于无冲突或已启用传输。
struct LegacyResourceImportProgress {
    std::uint64_t cursor = 0;              // 双端一致的已提交游标
    std::uint64_t processed_resources = 0; // 本清单已处理资源数
    std::uint64_t imported_resources = 0;  // 已建立策略的资源数
    std::uint64_t conflicts = 0;           // 待复核资源数
    std::string state = "not_started";     // 控制 run 状态
    std::string control_state = "active";  // 独立操作控制，不混淆每页自动paused
    std::uint64_t control_generation = 0;  // 显式控制推进代次
};

} // namespace orchestrator::modules::crawler::application::model
