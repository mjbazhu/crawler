#pragma once

#include <cstdint>
#include <functional>
#include <string>

namespace orchestrator::modules::crawler::application::model {

// 后端无关的已确认云端文件回执，checksum 对应已核验内容的本地 SHA-256。
struct CloudFileReceipt {
    std::string backend;          // 上传后端
    std::string path;             // 后端稳定路径
    std::uint64_t size_bytes = 0; // 实际内容大小
    std::string checksum;         // 已匹配内容的 SHA-256
};

// 上传不可变字节快照，避免本地刷新删除在途上传文件。
struct CloudFileUploadRequest {
    std::string relative_path;       // 数据库中的安全相对路径
    std::string bytes;               // 已提交且验证过的内容
    std::string checksum;            // 内容 SHA-256
    int timeout_ms = 120000;         // 上传及核验总超时
    std::function<bool()> cancelled; // 协作取消
};

// 存储操作结果；未知表示远端可能已经完成，下一次先核对对象。
struct CloudFileResult {
    bool ok = false;            // 对象内容已核实
    bool reused = false;        // 已有对象被复用
    bool uncertain = false;     // 远端结果待核对
    CloudFileReceipt receipt;   // 成功回执
    std::string error_category; // 脱敏错误分类
    std::string result_code;    // 稳定结果码
    std::string message;        // 脱敏诊断
};

} // namespace orchestrator::modules::crawler::application::model
