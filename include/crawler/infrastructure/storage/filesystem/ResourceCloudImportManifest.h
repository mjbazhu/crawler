#pragma once

#include "crawler/application/model/LegacyResourceImport.h"

#include <filesystem>
#include <istream>
#include <optional>
#include <ostream>
#include <string>

namespace orchestrator::modules::crawler::infrastructure::storage::filesystem {

// 清单固定范围；manifest_hash 来自已经校验的完整文件，不写入文件形成自引用。
struct ResourceCloudManifestHeader {
    application::model::LegacyResourceImportPlan plan;  // 人工声明及运行标识
    application::model::LegacyResourceImportPage scope; // 固定数据库、目标和高水位，不含行
};

// 顺序写入单行格式头，不访问数据库或内容资源。
void writeResourceCloudManifestHeader(std::ostream &output, const ResourceCloudManifestHeader &header);
// 写入一个有界资源页，文件行过大时拒绝。
void writeResourceCloudManifestPage(std::ostream &output, const application::model::LegacyResourceImportPage &page);
// 读取格式头并绑定调用方已核对的完整文件摘要。
ResourceCloudManifestHeader readResourceCloudManifestHeader(std::istream &input, std::string verified_sha256);
// 顺序读取下一页，EOF 返回空，格式错误不输出正文。
std::optional<application::model::LegacyResourceImportPage> readResourceCloudManifestPage(std::istream &input, const ResourceCloudManifestHeader &header);
// 流式计算清单文件 SHA-256，不将整个清单读入内存。
std::string resourceCloudManifestSha256(const std::filesystem::path &path);

} // namespace orchestrator::modules::crawler::infrastructure::storage::filesystem
