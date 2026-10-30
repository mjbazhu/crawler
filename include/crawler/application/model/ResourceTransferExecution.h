#pragma once

#include "crawler/application/model/ResourceTransferJobs.h"

#include <chrono>
#include <cstdint>
#include <functional>
#include <string>

namespace orchestrator::modules::crawler::application::model {

// 单次认领的独立截止时间和协作停止，排队不消耗传输超时。
struct ResourceTransferContext {
    std::chrono::steady_clock::time_point deadline;       // 本路固定截止时间
    std::function<bool()> cancelled;                      // 本任务取消/版本检查，不操作兄弟任务
    std::function<void(std::uint8_t)> related_discovered; // 失败音频仅回传种类，不携带URL或正文
    // 返回剩余毫秒，不因暂停等待重新计时。
    int remaining() const;
    // 异常也按停止处理，避免状态库不可用时继续副作用。
    bool stopped() const;
};

// 完整快照的所有权由具体 lease 托管，析构只清理自己的临时文件。
class ResourceContentSnapshot {
public:
    // 所有消费者释放后回收本次独占临时区。
    virtual ~ResourceContentSnapshot() = default;
    // 已校验的不可变文件；API 不接收或暴露此路径。
    virtual const std::string &filePath() const = 0;
    // 资源路径规划结果，独立于正式本地是否保存。
    virtual const std::string &relativePath() const = 0;
    // 完整实际内容 SHA-256。
    virtual const std::string &checksum() const = 0;
    // 实际完整字节数。
    virtual std::uint64_t sizeBytes() const = 0;
    // 仅旧音频关联计划使用；既有快照默认没有动态发现事实。
    virtual std::uint8_t relatedMask() const
    {
        return 0;
    }
};

// 修复先核对旧尝试，明确不存在才允许再次取得来源。
enum class ResourceAttemptObservation { Matched,
                                        Absent,
                                        Unknown,
                                        Conflict };

// 核对结果不把远端未知当作确定缺失。
struct ResourceAttemptInspection {
    ResourceAttemptObservation observation = ResourceAttemptObservation::Unknown; // 实际对象观察
    ResourceTransferReceipt receipt;                                              // Matched 时的实测证据
};

// 文件发布后的确认策略与物理写入分开。
enum class ResourceTransferSettlement { Accept,
                                        Defer,
                                        Abort };

} // namespace orchestrator::modules::crawler::application::model
