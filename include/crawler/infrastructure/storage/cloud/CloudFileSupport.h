#pragma once

#include "crawler/application/model/CloudFile.h"

#include <chrono>
#include <filesystem>
#include <functional>
#include <string>
#include <string_view>

namespace orchestrator::modules::crawler::infrastructure::storage::cloud {

// 下载和上传共用安全相对路径边界，拒绝路径逃逸及 header 分隔字符。
bool safeRelativePath(std::string_view path);
// 计算完整内容 SHA-256。
std::string sha256(std::string_view bytes);
// 计算完整内容 MD5，仅用于与后端提供的完整 MD5 比对。
std::string md5(std::string_view bytes);
// 生成本次独占快照或 multipart 边界的随机标识。
std::string uniqueToken();
// 返回固定脱敏错误，不转发远端响应或命令正文。
application::model::CloudFileResult failure(std::string code, std::string category = "persistence", bool uncertain = false);
// 核对上传的不可变快照，不信任调用方声明的摘要。
bool validUpload(const application::model::CloudFileUploadRequest &request);

// 单项上传及验证共享同一截止时间，线程间不共享可变状态。
class Deadline {
public:
    // 保存总超时和协作取消检查。
    Deadline(int timeout_ms, std::function<bool()> cancelled);
    // 剩余毫秒数，0 表示超时或取消。
    int remaining() const;
    // 检查协作取消。
    bool cancelled() const;

private:
    std::chrono::steady_clock::time_point end_; // 固定截止时间
    std::function<bool()> cancelled_;           // 调用方取消状态
};

// BaiduPCS-Go 使用独占磁盘快照，输入删除或刷新不影响在途上传。
class FileSnapshot {
public:
    // 在给定根内创建独占目录并保存已验证字节。
    FileSnapshot(const std::filesystem::path &root, const std::string &relative_path, std::string_view bytes);
    // 只清理本次创建且仍在固定父目录内的快照。
    ~FileSnapshot();
    FileSnapshot(const FileSnapshot &) = delete;
    FileSnapshot &operator=(const FileSnapshot &) = delete;
    // 返回本次独占存储根。
    const std::filesystem::path &root() const;

private:
    std::filesystem::path parent_; // 已规范化的快照父目录
    std::filesystem::path root_;   // 本次独占目录
};

} // namespace orchestrator::modules::crawler::infrastructure::storage::cloud
