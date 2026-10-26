#pragma once

#include "crawler/application/ports/parsing/SingerProfileParser.h"

namespace orchestrator::modules::crawler::infrastructure::music::migu::parser {

// Migu 歌手概览 parser，只解析歌手详情页中的平台声明总数。
class MiguSingerProfileParser : public application::ports::SingerProfileParser {
public:
    // 解析 Migu 歌手主要信息响应。
    application::model::SingerSourceProfile parseSingerSourceProfile(const std::vector<char> &raw_data) const override;
};

} // namespace orchestrator::modules::crawler::infrastructure::music::migu::parser
