#pragma once
#include "crawler/application/model/ResourceTransfer.h"
#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>
namespace orchestrator::modules::crawler::application::model {
// 删除前已确认的对象证据；仅为预演，不代表远端删除授权。
struct ResourceCloudCleanupCandidate {
    std::uint64_t id = 0;                                      // 持久证据ID
    std::uint64_t receipt_id = 0;                              // 原回执ID，资源删除后仍保留
    ResourceIdentity resource;                                 // 原文件身份
    ResourceDestination provider = ResourceDestination::Baidu; // 单一云平台
    std::uint64_t content_generation = 0;                      // 捕获时原生内容代次
    std::uint64_t target_revision = 0;                         // 捕获时目标修订
    std::string target_fingerprint;                            // 捕获时真实目标摘要
    std::string cloud_path;                                    // 安全时才公开的精确原路径
    std::string path_hash;                                     // 原始路径SHA256，包括不可公开路径
    std::string object_key;                                    // 受控resources/v1/对象后缀
    std::uint64_t size_bytes = 0;                              // 捕获大小
    std::string checksum;                                      // 原内容SHA256，不升级弱证据
    std::string verification;                                  // 原核验等级
    std::uint64_t confirmed_content_generation = 0;            // 原确认内容代次
    std::uint64_t confirmed_target_revision = 0;               // 原确认目标修订
    std::string receipt_state;                                 // 捕获时任务状态
    std::uint64_t captured_at = 0;                             // 删除前捕获时刻
    std::string result_code;                                   // 当前阻断原因或candidate_only
};
// 一页只读预演，未捕获的历史对象不能从空结果推断不存在。
struct ResourceCloudCleanupPage {
    std::vector<ResourceCloudCleanupCandidate> items; // 最多200项
    std::uint64_t next_after_id = 0;                  // 下页游标
    bool may_have_more = false;                       // 页满时保守提示
};
// 一次捕获返回稳定ID，同一证据重放不重复新增。
struct ResourceCloudCleanupCapture {
    std::vector<std::uint64_t> candidate_ids;   // 此次范围关联的证据ID
    std::size_t inserted = 0;                   // 新增快照数
    std::size_t resources_missing = 0;          // 缺少原生身份，证据可能已丢失
    std::size_t resources_without_receipts = 0; // 没有可捕获云回执，不证明无远端对象
};
// 同一数据库快照中的保守围栏，任何一项不满足都不能列为候选。
struct ResourceCloudCleanupFacts {
    bool source_exists = false;    // 原资源尚在或已重建
    bool target_matches = false;   // 当前目标与捕获身份一致
    bool maintenance = false;      // 全端维护闸门已关闭准入
    bool shared_reference = false; // 精确路径或同受控对象后缀仍被引用
    bool active_intent = false;    // 同资源/同对象存在活动、未决或生成意图
    bool alias_reference = false;  // 别名归档仍有待人工处理的证据
};
// 精确路径须无URL/凭据/穿越/控制字符；未知布局不能成为可清理候选。
bool safeResourceCloudCleanupPath(const std::string &path);
// 提取不可猜测的受控对象键，remote别名不影响同对象引用判断。
std::string resourceCloudCleanupObjectKey(const std::string &path);
// 仅给出预演分级；candidate_only仍须另行线上核对与删除授权。
std::string classifyResourceCloudCleanup(const ResourceCloudCleanupCandidate &candidate, const ResourceCloudCleanupFacts &facts);
} // namespace orchestrator::modules::crawler::application::model
