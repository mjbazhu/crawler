#pragma once

#include "crawler/application/ports/storage/LyricArtifactEncoder.h"

#include <string_view>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::music::lyrics {

// 平台无关的 JSON 与增强 LRC 文件格式 adapter；无状态且线程安全。
class TimedLyricsEncoder final : public application::ports::LyricArtifactEncoder {
public:
    // 标识 JSON schema 与增强 LRC 导出约定。
    std::string_view version() const override;
    // JSON 文档、文件 manifest 与数据库使用同一结构版本。
    int schemaVersion() const override;
    // JSON 保留完整时长；增强 LRC 为播放器兼容导出，间隙通过空时间片段表达。
    application::result::ServiceResult<std::vector<application::model::EncodedLyricArtifact>> encode(const domain::TimedLyrics &lyrics) const override;
};

} // namespace orchestrator::modules::crawler::infrastructure::music::lyrics
