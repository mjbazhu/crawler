#pragma once

#include "crawler/application/model/LyricArtifacts.h"
#include "crawler/application/result/ServiceResult.h"
#include "crawler/domain/music/TimedLyrics.h"

#include <vector>
#include <string_view>

namespace orchestrator::modules::crawler::application::ports {

// 歌词文件导出 port；线程安全，不执行 I/O，JSON 库类型不得穿过接口。
class LyricArtifactEncoder {
public:
    // 经由抽象释放导出实现。
    virtual ~LyricArtifactEncoder() = default;
    // 导出协议变化时升级该版本，application 无需了解具体文件格式。
    virtual std::string_view version() const = 0;
    // 文档结构版本由格式实现定义，存储和用例只转交该值。
    virtual int schemaVersion() const = 0;
    // 从同一模型生成完整 JSON 和增强 LRC，避免重复解析平台语法。
    virtual result::ServiceResult<std::vector<model::EncodedLyricArtifact>> encode(const domain::TimedLyrics &lyrics) const = 0;
};

} // namespace orchestrator::modules::crawler::application::ports
