#pragma once

#include <cstdint>
#include <string>

namespace orchestrator::modules::crawler::application::model {

// 采集时产生的封面保存结果；远端地址仅存在于解析输入中。
struct MetadataResourceFile {
    std::string local_path;       // 模块内相对路径
    std::uint64_t size_bytes = 0; // 实际保存字节数
    std::string checksum;         // 完整文件校验值
    int status = 0;               // 0 未处理，2 已保存，3 失败
    std::string error_category;   // 失败分类
    std::string result_code;      // 稳定结果码
    std::string last_error;       // 脱敏错误说明
};

} // namespace orchestrator::modules::crawler::application::model
