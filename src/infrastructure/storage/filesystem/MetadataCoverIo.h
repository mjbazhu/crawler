#pragma once

#include "crawler/application/model/MetadataResourceFile.h"
#include "crawler/infrastructure/http/client/HttpClient.h"

#include <memory>
#include <string>
#include <vector>

#include <cstddef>
#include <functional>

namespace orchestrator::modules::crawler::infrastructure::storage::filesystem::cover_io {

// 单张封面上限；下载和读取保持一致。
inline constexpr std::size_t maxCoverBytes = 8 * 1024 * 1024;

// 按签名和必要尾部识别格式，拒绝只有图片前缀的明显截断内容。
std::string imageExtension(const std::vector<char> &bytes);
// 按已识别的扩展名返回实际 MIME。
std::string imageMime(const std::string &extension);

// 有界 HTTP 图片获取结果，错误不保留 URL 或原始响应。
struct FetchResult {
    std::vector<char> bytes;                        // 完整响应字节
    std::string extension;                          // 内容识别出的格式
    application::model::MetadataResourceFile error; // 失败分类与稳定码
};

// 支持最多三次跳转和整次超时，传输中断时不返回可保存字节。
FetchResult fetch(http::client::HttpClient &client, std::string url, std::vector<std::string> headers, int timeout_ms, const std::function<bool()> &cancelled, std::size_t max_bytes = maxCoverBytes);

} // namespace orchestrator::modules::crawler::infrastructure::storage::filesystem::cover_io
