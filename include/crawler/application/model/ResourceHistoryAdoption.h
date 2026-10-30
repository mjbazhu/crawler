#pragma once
#include "crawler/application/model/ResourceDiscovery.h"
#include <cstdint>
#include <string>
#include <vector>
namespace orchestrator::modules::crawler::application::model {
// 经操作者审阅的单端采信范围；文件SHA不写入自身，避免自引用。
struct ResourceHistoryAdoptionPlan {
    std::string run_id;                                           // 与其他导入run保持不同的业务标识
    std::string manifest_hash;                                    // 已审阅采信清单完整SHA256
    std::string report_hash;                                      // 原始发现报告完整SHA256
    std::string discovery_run_id;                                 // 原持久发现任务
    std::string discovery_definition_hash;                        // 原发现范围摘要
    std::string database_identity;                                // 明确库身份
    ResourceOwner owner = ResourceOwner::Song;                    // 文件归属
    ResourceDestination destination = ResourceDestination::Baidu; // 只改变这一端
    std::uint64_t target_revision = 0;                            // 报告绑定目标版本
    std::string target_fingerprint;                               // 报告绑定真实目标
    std::uint64_t high_water_id = 0;                              // 报告已提交范围，可为部分导出
    std::uint64_t report_count = 0;                               // 报告已提交观察数
    std::uint64_t control_generation = 0;                         // 显式控制的页围栏
    bool operator_attested_destination = false;                   // 操作者显式确认本端决策
    bool writers_stopped = false;                                 // 操作者明确声明已停写排空
};
// 每条均保留原观察，操作方只能改变决策，不能改写观察内容。
struct ResourceHistoryAdoptionRow {
    ResourceDiscoveryObservation observation; // 数据库持久观察的副本
    std::string action = "review";            // adopt / allow_transfer / review
};
// 页按观察ID连续覆盖范围，最多200条，提交原子。
struct ResourceHistoryAdoptionPage {
    std::uint64_t after_id = 0;                   // 上一页结束
    std::uint64_t next_id = 0;                    // 本页结束
    std::vector<ResourceHistoryAdoptionRow> rows; // 按资源ID递增
};
// 单端进度；completed仅表示遍历结束，conflicts仍须处理。
struct ResourceHistoryAdoptionProgress {
    std::string state = "not_started";    // paused/completed进度状态
    std::string control_state = "active"; // active/paused/cancelled控制状态
    std::uint64_t control_generation = 0; // 控制版本
    std::uint64_t cursor = 0;             // 持久页检查点
    std::uint64_t processed = 0;          // 已审阅观察数
    std::uint64_t imported = 0;           // skip或明确normal决策数
    std::uint64_t conflicts = 0;          // 未能采用的观察数
};
// 统一校验纯范围与摘要格式，不将声明当作远端核验。
void validateResourceHistoryAdoptionPlan(const ResourceHistoryAdoptionPlan &plan);
} // namespace orchestrator::modules::crawler::application::model
