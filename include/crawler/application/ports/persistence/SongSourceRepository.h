#pragma once

#include "crawler/application/model/SongSourceMetadata.h"

#include <vector>

namespace orchestrator::modules::crawler::application::ports {

// 歌曲平台来源持久化 port，负责保存歌曲公共信息、平台引用、音质参数和资源任务。
class SongSourceRepository {
public:
    virtual ~SongSourceRepository() = default;

    // 批量保存歌曲来源元数据，写入后应保留后续获取资源所需参数。
    virtual bool saveBatch(const std::vector<application::model::SongSourceMetadata> &songs) = 0;
};

} // namespace orchestrator::modules::crawler::application::ports
