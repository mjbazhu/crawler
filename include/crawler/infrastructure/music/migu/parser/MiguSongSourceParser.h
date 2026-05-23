#pragma once

#include "crawler/application/ports/parsing/SongSourceParser.h"

namespace orchestrator::modules::crawler::infrastructure::music::migu::parser {

// Migu 歌曲来源 parser，只负责歌曲入库参数解析。
class MiguSongSourceParser : public application::ports::SongSourceParser {
public:
    // 解析可直接入库的歌曲平台来源元数据。
    std::vector<application::model::SongSourceMetadata> parseSongSources(const std::vector<char> &raw_data) const override;
};

} // namespace orchestrator::modules::crawler::infrastructure::music::migu::parser
