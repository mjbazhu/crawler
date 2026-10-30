#pragma once

#include "crawler/infrastructure/storage/cloud/CloudFileSupport.h"
#include "crawler/infrastructure/storage/filesystem/ResourceFileDigest.h"
#include "crawler/application/ports/storage/ResourceTransferDestination.h"
#include "crawler/infrastructure/http/client/HttpClient.h"

#include <array>
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <string>

namespace orchestrator::modules::crawler::tests::cloud_transfer {
namespace model = application::model;
namespace ports = application::ports;
namespace result = application::result;
namespace cloud = infrastructure::storage::cloud;
namespace files = infrastructure::storage::filesystem;
namespace http = infrastructure::http::client;

// 独立测试断言，不输出远端或认证正文。
inline void require(bool value, const char *message)
{
    if (!value) throw std::runtime_error(message);
}
// 物理路径只在测试内部转为 UTF-8。
inline std::string utf8(const std::filesystem::path &path)
{
    const auto value = path.generic_u8string();
    return {value.begin(), value.end()};
}
// 每个用例的独占临时根。
struct Root {
    std::filesystem::path path = std::filesystem::temp_directory_path() / ("crawler_stream_cloud_" + cloud::uniqueToken()); // 唯一根
    Root()
    {
        std::filesystem::create_directories(path);
    } // 创建自己的目录
    ~Root()
    {
        std::error_code ignored;
        std::filesystem::remove_all(path, ignored);
    } // 清理 fixture
};
// 有足够预算的单路上下文。
inline model::ResourceTransferContext context(int milliseconds = 10000)
{
    return {std::chrono::steady_clock::now() + std::chrono::milliseconds(milliseconds), {}};
}
// 用固定缓冲生成超过旧 8 MiB 限制的文件。
inline void writeFile(const std::filesystem::path &path, std::uint64_t bytes, char value = 's')
{
    std::filesystem::create_directories(path.parent_path());
    std::ofstream output(path, std::ios::binary);
    std::array<char, 64 * 1024> chunk {};
    chunk.fill(value);
    while (bytes) {
        const auto count = static_cast<std::size_t>((std::min<std::uint64_t>)(bytes, chunk.size()));
        output.write(chunk.data(), static_cast<std::streamsize>(count));
        bytes -= count;
    }
    output.close();
    require(bool(output), "fixture file write failed");
}
// 测试内完整文件租约，不聚合正文；Root 决定其最终清理。
class Snapshot final : public model::ResourceContentSnapshot {
public:
    explicit Snapshot(const std::filesystem::path &path) : file_(utf8(path))
    {
        const auto hash = files::digestResourceFile(path, 512 * 1024 * 1024, context());
        require(hash.isSuccess(), "fixture snapshot hash failed");
        digest = *hash.data();
    }
    const std::string &filePath() const override
    {
        return file_;
    } // 私有文件
    const std::string &relativePath() const override
    {
        return relative_;
    } // 已规划资源名
    const std::string &checksum() const override
    {
        return digest.sha256;
    } // 实测 SHA-256
    std::uint64_t sizeBytes() const override
    {
        return digest.size_bytes;
    } // 实际大小
    files::ResourceFileDigest digest; // 验证端保存的完整证据
private:
    std::string file_;                                 // 不暴露给 API
    std::string relative_ = "songs/fixture/audio.bin"; // 固定受控名
};
// 不依赖数据库的已认领任务模型。
inline model::ResourceTransferJob job(model::ResourceDestination destination, model::ResourceOwner owner = model::ResourceOwner::Song)
{
    model::ResourceTransferJob value;
    value.resource = {owner, 11};
    value.task_id = 101;
    value.claim_generation = 1;
    value.native_claim_generation = 1;
    value.destination = destination;
    value.adapter = destination == model::ResourceDestination::Local ? "filesystem" : destination == model::ResourceDestination::Baidu ? "baidupcs" :
                                                                                                                                         "rclone";
    value.version = {3, 1};
    value.state = "running";
    return value;
}
// 只复用已经持久登记的精确意图，模拟新 repair 任务没有原快照。
inline model::ResourceTransferJob repair(model::ResourceTransferJob value, const model::ResourceTransferAttempt &attempt)
{
    value.task_id += 100;
    value.action = model::ResourceTransferAction::Repair;
    value.attempt_path = attempt.path;
    value.attempt_checksum = attempt.checksum;
    value.attempt_size_bytes = attempt.size_bytes;
    return value;
}
} // namespace orchestrator::modules::crawler::tests::cloud_transfer
