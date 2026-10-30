#pragma once
#include "crawler/application/model/LegacyResourceImport.h"
#include <cstdint>
#include <map>
#include <optional>
#include <string>
#include <vector>
namespace orchestrator::modules::crawler::application::model {
// 受控只读盘点定义，不保存RC认证、源站URL或媒体正文。
struct ResourceDiscoveryDefinition {
    std::string run_id;                                           // 不可复用到不同范围的业务ID
    ResourceOwner owner = ResourceOwner::Song;                    // 四类文件身份之一
    ResourceDestination destination = ResourceDestination::Baidu; // 本次单一平台
    std::string database_identity;                                // 捕获库身份摘要
    std::uint64_t target_revision = 0;                            // 捕获目标修订
    std::string target_fingerprint;                               // 捕获真实目标身份摘要
    std::uint64_t high_water_id = 0;                              // 固定遍历高水位
    std::string path_source = "cloud";                            // 从旧cloud/local路径生成候选
    std::string strip_prefix;                                     // 操作方明确给定的旧路径根映射
    int page_size = 50;                                           // 每页1..200
    int concurrency = 4;                                          // 每目标一次仅一个活动run，内部1..8并发
    int object_timeout_ms = 5000;                                 // 单对象100..10000ms
};
// 持久页租约与计数，暂停/取消推进代次阻止迟到提交。
struct ResourceDiscoveryRun {
    ResourceDiscoveryDefinition definition;      // 不变范围
    std::string definition_hash;                 // 规范定义的SHA256
    std::string state;                           // running/paused/completed/cancelled
    std::uint64_t cursor = 0;                    // 已提交资源ID
    std::uint64_t claim_generation = 0;          // 当前页认领代次
    std::uint64_t lease_until = 0;               // 最晚页提交时刻
    std::uint64_t processed = 0;                 // 已持久化观察数
    std::map<std::string, std::uint64_t> counts; // 各证据等级计数
    std::string result_code;                     // 固定脱敏原因
};
// 网盘元信息响应；没有正文回读入口。
struct ResourceObjectObservation {
    std::string state = "unknown";             // found/missing/directory/unknown
    std::optional<std::uint64_t> size_bytes;   // 明确文件大小
    std::map<std::string, std::string> hashes; // 仅规范sha256/md5/sha1
};
// 一条观察同时固定输入来源和对象证据，不会直接变成传输策略。
struct ResourceDiscoveryObservation {
    ResourceIdentity resource;                  // 文件身份
    std::uint64_t generation = 0;               // 来源内容代次
    std::string source_identity;                // 来源身份摘要
    std::string values_digest;                  // 旧事实摘要
    std::string candidate_path;                 // 明确相对路径，未定位为空
    std::string grade = "unresolved";           // observed/hash_matched/conflict/missing/unknown/unresolved/source_changed
    std::string result_code;                    // 固定诊断
    std::uint64_t expected_size = 0;            // 库内大小，0为未知
    std::string expected_algorithm;             // 可识别的旧摘要算法
    std::string expected_hash;                  // 合法旧摘要，不保存任意文本
    std::optional<std::uint64_t> observed_size; // 远端元信息大小
    std::string observed_algorithm;             // 实际比较或保留的摘要算法
    std::string observed_hash;                  // 合法远端摘要
    std::uint64_t observed_at = 0;              // 本地记录元信息完成时间，不采用远端上传时间
};
// 校验范围、标识、路径映射和有界执行预算。
void validateResourceDiscoveryDefinition(ResourceDiscoveryDefinition &definition);
// 页预算含数据库/身份核对收尾，不超过600秒。
int resourceDiscoveryPageBudget(const ResourceDiscoveryDefinition &definition);
// 安全相对对象路径；禁止URL、穿越、驱动器和控制字符。
bool safeResourceDiscoveryPath(const std::string &path);
// 从明确的旧路径根映射生成候选，不猜测basename或另一平台路径。
std::string resourceDiscoveryCandidate(const LegacyResourceSnapshot &snapshot, const ResourceDiscoveryDefinition &definition);
// 仅接受已知算法的合法十六进制摘要，其他内容返回空。
std::string resourceDiscoveryHash(std::string algorithm, std::string value);
// 对元信息分级，缺失/未知/冲突不可互相转换。
ResourceDiscoveryObservation classifyResourceDiscovery(const LegacyResourceSnapshot &snapshot, std::string candidate, const ResourceObjectObservation &object);
} // namespace orchestrator::modules::crawler::application::model
