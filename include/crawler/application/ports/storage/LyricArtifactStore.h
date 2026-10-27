#pragma once

#include "crawler/application/model/LyricArtifacts.h"
#include "crawler/application/result/ServiceResult.h"

#include <cstddef>
#include <string_view>
#include <vector>

namespace orchestrator::modules::crawler::application::ports {

// 歌词来源与派生文件存储 port；实现须线程安全，文件完整提交后才返回产物描述。
class LyricArtifactStore {
public:
    // 经由抽象释放存储实现。
    virtual ~LyricArtifactStore() = default;
    // 限量读取已提交来源；实现负责路径边界和 SHA-256 校验。
    virtual result::ServiceResult<model::LyricSourceContent> readSource(std::string_view path, std::size_t max_bytes) const = 0;
    // 相同内容须幂等；更新失败恢复旧文件，无法恢复时保留备份并返回错误。
    virtual result::ServiceResult<model::LyricArtifactBundle> publish(const model::LyricProcessingRequest &request, std::string_view source_checksum, std::string_view version, int schema_version, const std::vector<model::EncodedLyricArtifact> &artifacts) const = 0;
};

} // namespace orchestrator::modules::crawler::application::ports
