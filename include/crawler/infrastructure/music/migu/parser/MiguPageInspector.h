#pragma once

#include "crawler/application/ports/parsing/PageInspector.h"

namespace orchestrator::modules::crawler::infrastructure::music::migu::parser {

// Migu 分页检查 parser，只负责分页摘要判断。
class MiguPageInspector : public application::ports::PageInspector {
public:
    // 检查歌手歌曲分页是否结束。
    application::ports::SingerSongPageInfo inspectSingerSongPage(const std::vector<char> &raw_data) const override;

    // 检查歌手专辑分页是否结束。
    application::ports::SingerAlbumPageInfo inspectSingerAlbumPage(const std::vector<char> &raw_data) const override;
};

} // namespace orchestrator::modules::crawler::infrastructure::music::migu::parser
