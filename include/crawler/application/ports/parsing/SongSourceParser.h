#pragma once

#include "crawler/application/model/SongSourceMetadata.h"

#include <vector>

namespace orchestrator::modules::crawler::application::ports {

// 歌曲来源 parser port，只解析可直接入库的平台歌曲来源元数据。
class SongSourceParser {
public:
    virtual ~SongSourceParser() = default;

    // 解析平台歌曲来源元数据。
    virtual std::vector<application::model::SongSourceMetadata> parseSongSources(const std::vector<char> &raw_data) const = 0;
};

} // namespace orchestrator::modules::crawler::application::ports
