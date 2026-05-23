#pragma once

#include "crawler/application/ports/parsing/SingerSourceParser.h"

namespace orchestrator::modules::crawler::infrastructure::music::migu::parser {

// Migu 歌手来源 parser，只负责歌手分类来源参数解析。
class MiguSingerSourceParser : public application::ports::SingerSourceParser {
public:
    // 解析可直接入库的歌手平台来源元数据。
    std::vector<application::model::SingerSourceMetadata> parseSingerSources(const std::vector<char> &raw_data, std::string_view tab) const override;
};

} // namespace orchestrator::modules::crawler::infrastructure::music::migu::parser
