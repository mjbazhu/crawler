#pragma once

#include "crawler/application/ports/parsing/AlbumSourceParser.h"

namespace orchestrator::modules::crawler::infrastructure::music::migu::parser {

// Migu 专辑来源 parser，只负责专辑列表和详情来源参数解析。
class MiguAlbumSourceParser : public application::ports::AlbumSourceParser {
public:
    // 解析可直接入库的专辑平台来源元数据。
    std::vector<application::model::AlbumSourceMetadata> parseAlbumSources(const std::vector<char> &raw_data) const override;
};

} // namespace orchestrator::modules::crawler::infrastructure::music::migu::parser
