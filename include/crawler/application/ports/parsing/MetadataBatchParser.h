#pragma once
#include "crawler/application/model/MetadataWriteBatch.h"
#include <vector>

namespace orchestrator::modules::crawler::application::ports {

// 解析一个采集响应的实体输入；一次调用不包含网络、保存或分页策略。
class MetadataBatchParser {
public:
    // 多态 parser 生命周期由装配方管理。
    virtual ~MetadataBatchParser() = default;
    // 平台实现只解码一次响应，分流歌曲、专辑和歌手上下文。
    virtual application::model::MetadataWriteBatch parseMetadataBatch(const std::vector<char> &body) const = 0;
};
} // namespace orchestrator::modules::crawler::application::ports
