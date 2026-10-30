#pragma once

#include "crawler/application/model/ResourceTransfer.h"

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace orchestrator::modules::crawler::application::model {

// 重试不隐式刷新内容；修复核对未决尝试；显式生成保留原始 MRC 的跳过事实。
enum class ResourceTransferAction { Process,
                                    Retry,
                                    Repair,
                                    RefreshContent,
                                    ProduceArtifacts };

// 合法来源登录态刷新规则；不承载token或HTTP配置。
struct ResourceTransferSourceOptions {
    int pacmtoken_refresh_interval_seconds = 300; // 周期刷新间隔，0仅保留批次初次/明确拒绝刷新
    bool auto_refresh_pacmtoken = true;           // 关闭后不自动请求刷新
    bool refresh_pacmtoken_on_440013 = true;      // 既有一次拒绝后的有限刷新策略
};

// 冻结到每个目标任务的有效策略，不包含认证或路径配置。
struct ResourceTransferJobPolicy {
    int timeout_ms = 120000;                     // 单次执行预算，不包含排队时间
    int retries = 1;                             // 暂态错误的额外尝试次数
    std::uint64_t max_bytes = 512 * 1024 * 1024; // 内容与临时空间上界
    std::uint64_t bytes_per_second = 0;          // 该目标额外限速，0 表示无额外限制
    bool strict_sha256 = false;                  // 首次受理冻结的强核验要求，配置降级不放宽旧任务
};

// 操作者明确请求的资源集合；空 destinations 使用首次受理时的配置。
struct ResourceTransferRequest {
    std::vector<ResourceIdentity> resources;                         // 最多 200 个文件资源身份
    std::vector<ResourceDestination> destinations;                   // 请求目标，不表示依赖顺序
    std::string idempotency_key;                                     // 同键同请求返回原操作
    ResourceTransferAction action = ResourceTransferAction::Process; // 明确的处理类型
    std::string parent_operation_id;                                 // 内部产物登记所属操作，HTTP 不接受
    std::uint64_t parent_task_id = 0;                                // 内部产物登记所属 MRC 任务
    std::uint64_t parent_claim_generation = 0;                       // 防止旧 producer 在重认领后晚登记
    std::string child_kind;                                          // 内部 related/artifact 来源，HTTP 不接受
    bool require_write_admission = false;                            // API 新受理在登记事务内复核维护闸门
    bool process_related_resources = false;                          // 内部旧合同要求音频成功后处理关联文件，HTTP v2不接受
    std::optional<ResourceTransferSourceOptions> source_options;     // 内部兼容入口冻结的来源规则；缺省兼容旧幂等摘要
};

// 组合根已核实的目标配置；指纹与持久目标状态比较后才允许取得内容。
struct ResourceTransferTargetProfile {
    ResourceDestination destination = ResourceDestination::Local; // 目标身份
    std::string fingerprint;                                      // 不含凭据的目标身份摘要
    ResourceTransferJobPolicy policy;                             // 首次受理冻结的配置
    bool available = true;                                        // 单端配置/身份核对是否可用；历史 skip 不依赖它
    std::string adapter;                                          // 已选择的传输工具，与平台身份独立
    bool produces_artifacts = false;                              // 已装配受控 MRC producer，缺 local 仍可生成
    std::string producer_version;                                 // 显式生成计划的冻结处理器版本
    int producer_schema_version = 1;                              // 冻结导出协议
    ResourceTransferJobPolicy producer_policy;                    // 与父资源传输无关的预算
    bool producer_key_available = false;                          // 仅表明当前配置可运行，不返回密钥
};

// 当前任务及其持久版本，不包含临时 URL、正文或认证。
struct ResourceTransferJob {
    std::uint64_t task_id = 0;                                       // 持久任务主键
    std::string operation_id;                                        // 真实父操作
    ResourceIdentity resource;                                       // 文件资源归属
    ResourceDestination destination = ResourceDestination::Local;    // 独立目标
    ResourceTransferVersion version;                                 // 内容代次与目标修订
    std::uint64_t source_revision = 0;                               // 来源描述版本
    std::string source_identity;                                     // 冻结来源身份，拒绝未捕获的来源变化
    std::string target_fingerprint;                                  // 冻结目标身份
    std::string adapter;                                             // 首次受理冻结的工具，旧记录缺失时保持未知
    std::uint64_t claim_generation = 0;                              // 拒绝旧 worker 的回写
    std::uint64_t native_claim_generation = 0;                       // 对应原资源/云回执的认领代次
    std::uint64_t claimed_at = 0;                                    // Unix 秒，用于显式租约恢复
    int attempt_count = 0;                                           // 已开始的尝试数
    ResourceTransferJobPolicy policy;                                // 冻结执行预算
    ResourceTransferAction action = ResourceTransferAction::Process; // 本次动作
    std::string state;                                               // pending/running/终态
    std::string result_code;                                         // 稳定结果码
    bool cancel_requested = false;                                   // 只影响本任务
    std::uint64_t busy_task_id = 0;                                  // 被其他操作占用时的可查询引用
    std::string attempt_path;                                        // 结果未决时先核对此独占对象，禁止直接重传
    std::string attempt_checksum;                                    // 已登记尝试的 SHA-256
    std::uint64_t attempt_size_bytes = 0;                            // 已登记尝试的完整大小
    bool process_related_resources = false;                          // 首次受理冻结，重试不扩大关联范围
    ResourceTransferSourceOptions source_options;                    // 认领读取首次来源规则，不能随运行设置改变
};

// 本次尝试在副作用前固定的对象身份；同一代次内也使用独占路径。
struct ResourceTransferAttempt {
    std::string path;              // 安全相对/云端对象路径，不是下载 URL
    std::string checksum;          // 本次完整快照 SHA-256
    std::uint64_t size_bytes = 0;  // 本次完整快照大小
    std::uint8_t related_mask = 0; // 合法listen发现的cover/lyric/mrc/trc位集，不保存临时URL
};

// 可以确认的完整对象；local 和云端均按同一内容参考校验。
struct ResourceTransferReceipt {
    std::string path;                                                     // 实际完整对象路径
    std::string checksum;                                                 // 已核实的 SHA-256
    std::uint64_t size_bytes = 0;                                         // 实际完整大小
    std::string adapter;                                                  // 实际上传工具；local 为 filesystem
    std::string verification_method;                                      // 实际核验方式
    ResourceVerification verification = ResourceVerification::Unverified; // 远端 MD5 不提升为远端 SHA-256
    std::string remote_hash_algorithm;                                    // 原始远端 hash 算法
    std::string remote_hash_value;                                        // 原始远端 hash
    bool uploaded = false;                                                // 本次确实上传；复用或核对不制造上传时间
};

// 失败不擦除上次已确认对象；未决任务须先核对再决定是否重新获取来源。
struct ResourceTransferJobFailure {
    std::string result_code;                  // 固定脱敏结果码
    std::string error_category;               // 稳定错误类别
    bool uncertain = false;                   // 已有副作用但无法确认
    bool retryable = false;                   // 尚未发生不可核对副作用的暂态失败
    bool source_restricted = false;           // 永久来源限制，阻止新的源站请求
    std::optional<std::uint8_t> related_mask; // 既有合同允许永久限制响应中合法关联资源，未知则空
};

// 父操作的持久快照；完成不等于所有副本均已经过实测核验。
struct ResourceTransferOperation {
    std::string operation_id;                     // 真实父 ID
    std::string state;                            // running/cancelling/completed/partial/cancelled
    std::uint64_t logical_resource_count = 0;     // 逻辑资源数
    std::vector<ResourceTransferJob> tasks;       // 最多 600 个目标项
    bool replayed = false;                        // 是否返回已有幂等操作
    std::string parent_operation_id;              // 产物操作所属的原始资源操作
    std::uint64_t parent_task_id = 0;             // 产物操作所属的目标任务
    std::vector<std::string> child_operation_ids; // 原始资源操作产生的单层产物操作
};

// 稳定动作名只用于受控持久化及协议转换。
std::string_view resourceTransferActionName(ResourceTransferAction action);
// 只接受已定义动作，不把任意字符串拼入 SQL。
std::optional<ResourceTransferAction> parseResourceTransferAction(std::string_view name);

} // namespace orchestrator::modules::crawler::application::model
