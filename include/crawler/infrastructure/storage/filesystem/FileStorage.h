#pragma once

#include "crawler/application/ports/storage/FileStore.h"
#include "crawler/application/ports/storage/SongResourceBlobStore.h"

#include <filesystem>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::storage::filesystem {

// 本地文件系统存储 adapter，支持旧 FileStore 和歌曲资源二进制存储 port。
class FileStorage : public application::ports::FileStore, public application::ports::SongResourceBlobStore {
public:
    explicit FileStorage(std::filesystem::path root_dir);
    ~FileStorage() override = default;

    // 原子写入文件
    // 策略：先写入临时文件，写入完成后 rename，防止部分写入
    bool save(std::string_view relative_path, const std::vector<char> &data) override;

    // 删除文件（用于回滚）
    bool remove(std::string_view relative_path);

    // 检查文件是否存在
    bool exists(std::string_view relative_path) const;

    // 保存歌曲资源字节，并返回相对路径、大小和 SHA-256。
    application::model::SongResourceBlobSaveResult saveSongResourceBlob(application::model::SongResourceBlobSaveRequest request) override;

    // 打开歌曲资源流式写入会话，提交前只写入临时文件。
    std::unique_ptr<application::ports::SongResourceBlobWriteSession> beginSongResourceBlobWrite(application::model::SongResourceBlobStreamSaveRequest request) override;

    // 获取完整路径
    std::filesystem::path getFullPath(std::string_view relative_path) const;

private:
    std::filesystem::path root_dir_; // 本地存储根目录
};

} // namespace orchestrator::modules::crawler::infrastructure::storage::filesystem
