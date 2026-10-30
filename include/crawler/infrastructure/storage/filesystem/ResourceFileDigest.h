#pragma once

#include "crawler/application/model/ResourceTransferExecution.h"
#include "crawler/application/result/ServiceResult.h"

#include <filesystem>
#include <cstdint>
#include <string>

namespace orchestrator::modules::crawler::infrastructure::storage::filesystem {

// 一次有界读取的完整摘要，MD5 仅用于兼容远端算法。
struct ResourceFileDigest {
    std::string sha256;           // 标准内容参考
    std::string md5;              // 远端核对可选算法，缺失时不制造它
    std::string sha1;             // 只用于旧对象兼容，不作为新参考摘要
    std::uint64_t size_bytes = 0; // 实际读取量
};

// 固定 64 KiB 缓冲，拒绝目录、符号链接、超限与读取中变化；不输出物理路径。
application::result::ServiceResult<ResourceFileDigest> digestResourceFile(const std::filesystem::path &file, std::uint64_t max_bytes, const application::model::ResourceTransferContext &context);

} // namespace orchestrator::modules::crawler::infrastructure::storage::filesystem
