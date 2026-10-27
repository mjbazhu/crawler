#pragma once

#include "crawler/application/ports/storage/LyricArtifactStore.h"

#include <filesystem>
#include <cstddef>
#include <mutex>
#include <string_view>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::storage::filesystem {

// 本地歌词文件 adapter；同一实例线程安全，共享目录的多实例写入须由调用方串行化。
class LocalLyricArtifactStore final : public application::ports::LyricArtifactStore {
public:
    // root 必须是资源存储根目录，不能是任意 URL 或外部对象存储地址。
    explicit LocalLyricArtifactStore(std::filesystem::path root);
    // 限制读取大小并返回 SHA-256，不修改来源。
    application::result::ServiceResult<application::model::LyricSourceContent> readSource(std::string_view path, std::size_t max_bytes) const override;
    // 在来源同目录发布两个固定文件；替换失败恢复旧文件，元信息交由数据库保存。
    application::result::ServiceResult<application::model::LyricArtifactBundle> publish(const application::model::LyricProcessingRequest &request, std::string_view source_checksum, std::string_view version, int schema_version, const std::vector<application::model::EncodedLyricArtifact> &artifacts) const override;

private:
    std::filesystem::path root_;           // 初始化后不变的存储根目录
    mutable std::mutex publication_mutex_; // 串行化当前实例的文件集合替换
};

} // namespace orchestrator::modules::crawler::infrastructure::storage::filesystem
