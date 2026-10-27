#pragma once

#include "crawler/application/model/LyricArtifacts.h"
#include "crawler/application/result/ServiceResult.h"

#include <string_view>

namespace orchestrator::modules::crawler::application::ports {

// 歌词派生产物索引 port；线程安全，正文仍由文件存储持有。
class LyricArtifactRepository {
public:
    // 经由抽象释放 repository。
    virtual ~LyricArtifactRepository() = default;
    // 原子登记整个集合；须拒绝已被新来源校验值替换的旧处理结果。
    virtual result::ServiceResult<bool> record(const model::LyricArtifactBundle &bundle) = 0;
    // 单独记录派生失败，不改变原始资源下载状态。
    virtual result::ServiceResult<bool> recordFailure(const model::LyricProcessingRequest &request, std::string_view version, int schema_version, std::string_view code) = 0;
};

} // namespace orchestrator::modules::crawler::application::ports
