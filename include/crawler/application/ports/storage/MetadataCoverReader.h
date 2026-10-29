#pragma once

#include "crawler/application/model/MetadataCover.h"
#include "crawler/application/model/MetadataResourceFile.h"

#include <optional>

namespace orchestrator::modules::crawler::application::ports {

// 本地图片读取 port；仅接受数据库回执，不接受任意文件请求。
class MetadataCoverReader {
public:
    // 释放读取器。
    virtual ~MetadataCoverReader() = default;
    // 检查回执文件存在性与大小，允许刷新期间继续使用旧文件。
    virtual bool available(const model::MetadataResourceFile &file) const = 0;
    // 有界读取并核对签名、大小和 checksum，损坏或缺失返回空。
    virtual std::optional<model::MetadataCoverContent> read(const model::MetadataResourceFile &file) const = 0;
};

} // namespace orchestrator::modules::crawler::application::ports
