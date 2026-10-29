#pragma once

#include <cstdint>
#include <string>

namespace orchestrator::modules::crawler::application::model {

// 独立封面下载或旧兼容装配产生的文件回执与错误，不携带远端地址。
struct MetadataResourceFile {
    std::string local_path;       // 模块内相对路径
    std::uint64_t size_bytes = 0; // 实际保存字节数
    std::string checksum;         // 完整文件校验值
    int status = 0;               // 0 未处理，1 获取中，2 已保存，3 失败
    std::string error_category;   // 失败分类
    std::string result_code;      // 稳定结果码
    std::string last_error;       // 脱敏错误说明
};

} // namespace orchestrator::modules::crawler::application::model
