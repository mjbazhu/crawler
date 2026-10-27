#pragma once

#include "crawler/application/model/SongSourceMetadata.h"
#include "crawler/application/model/AlbumSongPage.h"

#include <vector>

namespace orchestrator::modules::crawler::application::ports {

// 歌曲来源 parser port，只解析可直接入库的平台歌曲来源元数据。
class SongSourceParser {
public:
    virtual ~SongSourceParser() = default;

    // 解析平台歌曲来源元数据。
    virtual std::vector<application::model::SongSourceMetadata> parseSongSources(const std::vector<char> &raw_data) const = 0;

    // 解析专辑曲目页及其声明总数；未支持的旧 parser 返回未知结果。
    virtual application::model::AlbumSongPage parseAlbumSongPage(const std::vector<char> &) const
    {
        return {};
    }
};

} // namespace orchestrator::modules::crawler::application::ports
