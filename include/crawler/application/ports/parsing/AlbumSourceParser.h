#pragma once

#include "crawler/application/model/AlbumSourceMetadata.h"

#include <vector>

namespace orchestrator::modules::crawler::application::ports {

// 专辑来源 parser port，只解析可直接入库的平台专辑来源元数据。
class AlbumSourceParser {
public:
    virtual ~AlbumSourceParser() = default;

    // 解析平台专辑来源元数据，可同时处理列表和详情响应。
    virtual std::vector<application::model::AlbumSourceMetadata> parseAlbumSources(const std::vector<char> &raw_data) const = 0;
};

} // namespace orchestrator::modules::crawler::application::ports
