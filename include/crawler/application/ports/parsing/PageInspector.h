#pragma once

#include <cstddef>
#include <vector>

namespace orchestrator::modules::crawler::application::ports {

// 歌手歌曲分页摘要，用于 application 判断是否继续请求下一页。
struct SingerSongPageInfo {
    std::size_t first_content_song_count = 0;  // data.contents[0] 中有效歌曲数量
    std::size_t second_content_song_count = 0; // data.contents[1] 中有效歌曲数量，不存在时为 0
    bool should_stop_paging = true;            // 前两个内容块有效歌曲数都小于分页大小时为 true
};

// 歌手专辑分页摘要，用于 application 判断是否继续请求下一页。
struct SingerAlbumPageInfo {
    std::size_t album_count = 0;    // 当前响应中有效专辑数量
    bool should_stop_paging = true; // 有效专辑数小于分页大小时为 true
};

// 分页检查 parser port，只返回分页摘要，不负责保存或发起请求。
class PageInspector {
public:
    virtual ~PageInspector() = default;

    // 检查歌手歌曲分页摘要。
    virtual SingerSongPageInfo inspectSingerSongPage(const std::vector<char> &raw_data) const = 0;

    // 检查歌手专辑分页摘要。
    virtual SingerAlbumPageInfo inspectSingerAlbumPage(const std::vector<char> &raw_data) const = 0;
};

} // namespace orchestrator::modules::crawler::application::ports
