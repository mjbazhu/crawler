#pragma once

#include "crawler/application/ports/parsing/AlbumSourceParser.h"
#include "crawler/application/ports/parsing/PageInspector.h"
#include "crawler/application/ports/parsing/ParseResultParser.h"
#include "crawler/application/ports/parsing/SingerSourceParser.h"
#include "crawler/application/ports/parsing/SongSourceParser.h"

namespace orchestrator::modules::crawler::application::ports {

// 音乐元数据解析聚合 port，用于兼容既有组合根和测试替身。
class MetadataParser : public ParseResultParser,
                       public SongSourceParser,
                       public SingerSourceParser,
                       public AlbumSourceParser,
                       public PageInspector {
public:
    using SingerSongPageInfo = application::ports::SingerSongPageInfo;
    using SingerAlbumPageInfo = application::ports::SingerAlbumPageInfo;

    virtual ~MetadataParser() = default;

    // 检查歌手歌曲分页摘要；默认用解析出的来源数量做保守判断。
    SingerSongPageInfo inspectSingerSongPage(const std::vector<char> &raw_data) const override
    {
        SingerSongPageInfo info;
        info.first_content_song_count = parseSongSources(raw_data).size();
        info.should_stop_paging = info.first_content_song_count < 50;
        return info;
    }

    // 检查歌手专辑分页摘要；默认用解析出的来源数量做保守判断。
    SingerAlbumPageInfo inspectSingerAlbumPage(const std::vector<char> &raw_data) const override
    {
        SingerAlbumPageInfo info;
        info.album_count = parseAlbumSources(raw_data).size();
        info.should_stop_paging = info.album_count < 50;
        return info;
    }
};

} // namespace orchestrator::modules::crawler::application::ports
