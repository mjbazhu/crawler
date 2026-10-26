#pragma once

#include "crawler/application/model/AlbumSourceMetadata.h"
#include "crawler/application/model/SingerSourceMetadata.h"
#include "crawler/application/model/SingerSourceProfile.h"
#include "crawler/application/model/SongSourceMetadata.h"
#include "crawler/application/ports/parsing/PageInspector.h"
#include "crawler/domain/music/model/ParseResult.h"
#include "crawler/infrastructure/music/migu/parser/strategy/IParserStrategy.h"

#include <memory>
#include <string_view>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::music::migu::parser::detail {

// 使用指定策略解析领域结果。
std::vector<domain::ParseResult> parseResultsWithStrategies(
    const std::vector<char> &raw_data,
    const std::vector<std::unique_ptr<strategy::IParserStrategy>> &strategies
);

// 解析 Migu 歌曲来源参数。
std::vector<application::model::SongSourceMetadata> parseSongSourcesRaw(const std::vector<char> &raw_data);

// 解析 Migu 歌手来源参数。
std::vector<application::model::SingerSourceMetadata> parseSingerSourcesRaw(const std::vector<char> &raw_data, std::string_view tab);

// 解析 Migu 歌手主要信息中的平台声明总数。
application::model::SingerSourceProfile parseSingerSourceProfileRaw(const std::vector<char> &raw_data);

// 解析 Migu 专辑来源参数。
std::vector<application::model::AlbumSourceMetadata> parseAlbumSourcesRaw(const std::vector<char> &raw_data);

// 检查 Migu 歌手歌曲分页摘要。
application::ports::SingerSongPageInfo inspectSingerSongPageRaw(const std::vector<char> &raw_data);

// 检查 Migu 歌手专辑分页摘要。
application::ports::SingerAlbumPageInfo inspectSingerAlbumPageRaw(const std::vector<char> &raw_data);

} // namespace orchestrator::modules::crawler::infrastructure::music::migu::parser::detail
