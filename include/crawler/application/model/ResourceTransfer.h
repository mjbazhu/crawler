#pragma once

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace orchestrator::modules::crawler::application::model {

// 文件资源归属；主键只在相应表内唯一。
enum class ResourceOwner { Song,
                           Singer,
                           Album,
                           SongArtifact };

// 调度目标；Local 不属于云端平台。
enum class ResourceDestination { Local,
                                 Baidu,
                                 Gdrive };

// 文件资源的完整身份。
struct ResourceIdentity {
    ResourceOwner owner = ResourceOwner::Song; // 资源表归属
    std::uint64_t resource_id = 0;             // 资源行主键
    bool operator==(const ResourceIdentity &) const = default;
};

// 认领、跳过和回写共同使用的版本边界。
struct ResourceTransferVersion {
    std::uint64_t content_generation = 1; // 内容代次
    std::uint64_t target_revision = 1;    // 目标配置修订
    bool operator==(const ResourceTransferVersion &) const = default;
};

// 持久跳过策略与核验结果分开，失败核验不解除策略。
struct ResourceTransferSkip {
    ResourceTransferVersion version; // 声明绑定的内容与目标
    std::string baseline_id;         // 已审阅的清单标识
    std::string reason;              // 固定、可审计的跳过原因
    bool enabled = false;            // 是否启用该条策略
};

// 已核验程度；不能由路径、大小或旧 backend 推导成功。
enum class ResourceVerification { Unverified,
                                  OperatorAttested,
                                  ObjectObserved,
                                  HashMatched,
                                  Sha256Verified,
                                  Conflict };

// 旧校验值的解析结果；解析格式不代表完成了实际核验。
struct LegacyResourceChecksum {
    std::string original;        // 不改写的旧值
    std::string algorithm;       // sha256、md5、sha1、size 或 unknown
    std::string value;           // 经格式校验的摘要或大小文本
    std::string sha256;          // 只有合法 SHA-256 格式才填写
    bool content_digest = false; // 是否为内容摘要，仍需确认对象和证据
};

// 返回固定所有者名，用于身份、事件及协议映射。
std::string_view resourceOwnerName(ResourceOwner owner);
// 解析固定所有者枚举，不接受任意表名。
std::optional<ResourceOwner> parseResourceOwner(std::string_view name);
// 返回稳定调度目标名。
std::string_view resourceDestinationName(ResourceDestination destination);
// 解析固定调度目标。
std::optional<ResourceDestination> parseResourceDestination(std::string_view name);
// 只接受两种云端平台，local 返回空。
std::optional<ResourceDestination> parseCloudProvider(std::string_view name);
// 工具能力与平台身份分开；rclone 可用于已明确归属的任一受支持云端。
bool supportsResourceTransferAdapter(ResourceDestination destination, std::string_view adapter);
// 识别旧校验算法和格式，不读文件、不访问云端、不制造核验时间。
LegacyResourceChecksum parseLegacyResourceChecksum(std::string_view value);

} // namespace orchestrator::modules::crawler::application::model
