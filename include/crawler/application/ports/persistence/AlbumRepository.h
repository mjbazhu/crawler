#pragma once

#include "crawler/domain/music/model/Album.h"

#include <vector>

namespace orchestrator::modules::crawler::application::ports {

// 专辑持久化 port，只暴露 application 可理解的同步批量保存语义。
class AlbumRepository {
public:
    virtual ~AlbumRepository() = default;

    // 批量保存专辑，具体 adapter 可在内部桥接异步数据库实现。
    virtual bool saveBatch(const std::vector<domain::Album> &albums) = 0;
};

} // namespace orchestrator::modules::crawler::application::ports
