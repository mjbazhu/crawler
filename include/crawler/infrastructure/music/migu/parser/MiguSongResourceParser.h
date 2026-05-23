#pragma once

#include "crawler/application/ports/parsing/SongResourceParser.h"

namespace orchestrator::modules::crawler::infrastructure::music::migu::parser {

// Migu 歌曲资源响应 parser，只解析 listen 接口返回的资源 URL 字段。
class MiguSongResourceParser : public application::ports::SongResourceParser {
public:
    // 解析 Migu listen 响应，返回可下载音频 URL 和歌词资源 URL。
    application::model::SongResourceFetchResult parseSongResource(const std::vector<char> &raw_data) const override;
};

} // namespace orchestrator::modules::crawler::infrastructure::music::migu::parser
