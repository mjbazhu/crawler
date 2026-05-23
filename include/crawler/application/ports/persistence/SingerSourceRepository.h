#pragma once

#include "crawler/application/model/SingerSourceMetadata.h"

#include <vector>

namespace orchestrator::modules::crawler::application::ports {

// 歌手平台来源仓储 port，负责保存公共歌手和平台来源引用。
class SingerSourceRepository {
public:
    virtual ~SingerSourceRepository() = default;

    // 批量保存歌手来源元数据；实现层负责事务、去重和平台引用更新。
    virtual bool saveBatch(const std::vector<application::model::SingerSourceMetadata> &singers) = 0;
};

} // namespace orchestrator::modules::crawler::application::ports
