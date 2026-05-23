#pragma once

#include "crawler/domain/music/model/Song.h"

#include <vector>

namespace orchestrator::modules::crawler::application::ports {

// 歌曲持久化 port，只暴露 application 可理解的同步批量保存语义。
class SongRepository {
public:
    virtual ~SongRepository() = default;

    // 批量保存歌曲，具体 adapter 可在内部桥接异步数据库实现。
    virtual bool saveBatch(const std::vector<domain::Song> &songs) = 0;
};

} // namespace orchestrator::modules::crawler::application::ports
