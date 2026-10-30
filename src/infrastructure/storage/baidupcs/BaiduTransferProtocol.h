#pragma once

#include "crawler/infrastructure/storage/baidupcs/BaiduResourceTransfer.h"

#include <atomic>
#include <functional>
#include <string>
#include <vector>
#include <filesystem>
#include <memory>
#include <cstdint>

namespace orchestrator::modules::crawler::infrastructure::storage::baidupcs::transfer {
namespace model = application::model;
namespace result = application::result;
namespace process = execution::process;

// 限制同时回读的预留量，目录仅在核对到一个正确大小的文件后创建。
struct ReadbackQuota {
    const std::uint64_t limit;               // 本目标配置上限
    std::atomic<std::uint64_t> reserved {0}; // 活跃回读独占预留
    // 不创建文件系统状态。
    explicit ReadbackQuota(std::uint64_t maximum) : limit(maximum)
    {}
};
// 元信息必须同时确认精确路径、类型和完整整数大小。
enum class MetaState { Unknown,
                       Missing,
                       File,
                       Directory,
                       Authentication };
// 元数据只用于有界回读，不把分片 MD5 当作文件强证据。
struct Meta {
    MetaState state = MetaState::Unknown; // 未识别或失败保持未知
    std::uint64_t size = 0;               // File 时的精确大小
};

// 共用根规范化，不访问用户账号文件。
BaiduPcsGoConfig normalize(BaiduPcsGoConfig config);
// UTF-8 路径进入进程参数，不能经过本地 ANSI 编码。
std::string pathUtf8(const std::filesystem::path &path);
// 固定参数使用原生命令引用；remaining 为 0 时不创建子进程。
process::ProcessResult run(const BaiduPcsGoConfig &config, const process::CommandRunner &runner, const std::vector<std::string> &arguments, const model::ResourceTransferContext &context, std::function<bool()> extra_stop = {});
// 只识别明确错误码和当前版本约定的表格字段；不以退出码/子串数字猜缺失。
Meta meta(const BaiduPcsGoConfig &config, const process::CommandRunner &runner, const std::string &path, const model::ResourceTransferContext &context);
// 明确文件才创建有界私有回读；旧未决尝试即使查不到也不盲目放行重传。
result::ServiceResult<model::ResourceAttemptInspection> verify(const BaiduPcsGoConfig &config, const process::CommandRunner &runner, std::shared_ptr<ReadbackQuota> quota, const model::ResourceTransferAttempt &attempt, const model::ResourceTransferContext &context, bool allow_absent);
// 原生客户端直接消费租约文件，完成后仍需实际回读核对。
result::ServiceResult<model::ResourceTransferReceipt> publish(const BaiduPcsGoConfig &config, const process::CommandRunner &runner, std::shared_ptr<ReadbackQuota> quota, const model::ResourceTransferAttempt &attempt, const model::ResourceContentSnapshot &snapshot, const model::ResourceTransferContext &context);

} // namespace orchestrator::modules::crawler::infrastructure::storage::baidupcs::transfer
