#pragma once

#include "crawler/application/result/ServiceResult.h"

#include <filesystem>
#include <string>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::storage::filesystem::detail {

// 一个待发布文件；调用方决定文件名和内容，发布器只处理文件替换。
struct LyricFileContent {
    std::string filename; // 单层文件名，不允许目录或绝对路径
    std::string content;  // 完整文件内容
};

// 按路径组件检查严格子路径；调用方须先完成 canonical 解析。
bool isWithinRoot(const std::filesystem::path &root, const std::filesystem::path &path);

// 顺序发布固定文件，失败恢复旧文件；需外部串行化同一目录的写入。
application::result::ServiceResult<bool> publishLyricFiles(const std::filesystem::path &root, const std::filesystem::path &directory, const std::vector<LyricFileContent> &files);

} // namespace orchestrator::modules::crawler::infrastructure::storage::filesystem::detail
