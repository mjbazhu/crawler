#pragma once

#include "crawler/application/model/AlbumSourceMetadata.h"

#include <vector>

namespace orchestrator::modules::crawler::application::ports {

// 专辑来源写入 port，只负责保存可重建平台资源请求的专辑参数。
class AlbumSourceWriter {
public:
    virtual ~AlbumSourceWriter() = default;

    // 批量保存专辑来源；实现需保证同一平台 album 来源幂等更新。
    virtual bool saveBatch(const std::vector<application::model::AlbumSourceMetadata> &albums) = 0;
};

} // namespace orchestrator::modules::crawler::application::ports
