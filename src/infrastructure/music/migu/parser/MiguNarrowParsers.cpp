#include "crawler/infrastructure/music/migu/parser/MiguAlbumSourceParser.h"
#include "crawler/infrastructure/music/migu/parser/MiguPageInspector.h"
#include "crawler/infrastructure/music/migu/parser/MiguParseResultParser.h"
#include "crawler/infrastructure/music/migu/parser/MiguParserSupport.h"
#include "crawler/infrastructure/music/migu/parser/MiguSingerProfileParser.h"
#include "crawler/infrastructure/music/migu/parser/MiguSingerSourceParser.h"
#include "crawler/infrastructure/music/migu/parser/strategy/MiguStrategies.h"

namespace orchestrator::modules::crawler::infrastructure::music::migu::parser {

MiguParseResultParser::MiguParseResultParser()
{
    strategies_.push_back(std::make_unique<strategy::MiguSongListStrategy>());
    strategies_.push_back(std::make_unique<strategy::MiguSingerSongStrategy>());
    strategies_.push_back(std::make_unique<strategy::MiguAlbumDetailStrategy>());
    strategies_.push_back(std::make_unique<strategy::MiguSearchStrategy>());
    strategies_.push_back(std::make_unique<strategy::MiguSingerListStrategy>());
    strategies_.push_back(std::make_unique<strategy::MiguAlbumListStrategy>());
    strategies_.push_back(std::make_unique<strategy::MiguAlbumListSearchStrategy>());
}

std::vector<domain::ParseResult> MiguParseResultParser::parse(const std::vector<char> &raw_data) const
{
    return detail::parseResultsWithStrategies(raw_data, strategies_);
}

std::vector<application::model::SingerSourceMetadata> MiguSingerSourceParser::parseSingerSources(const std::vector<char> &raw_data, std::string_view tab) const
{
    return detail::parseSingerSourcesRaw(raw_data, tab);
}

std::vector<application::model::AlbumSourceMetadata> MiguAlbumSourceParser::parseAlbumSources(const std::vector<char> &raw_data) const
{
    return detail::parseAlbumSourcesRaw(raw_data);
}

application::model::SingerSourceProfile MiguSingerProfileParser::parseSingerSourceProfile(const std::vector<char> &raw_data) const
{
    return detail::parseSingerSourceProfileRaw(raw_data);
}

application::ports::SingerSongPageInfo MiguPageInspector::inspectSingerSongPage(const std::vector<char> &raw_data) const
{
    return detail::inspectSingerSongPageRaw(raw_data);
}

application::ports::SingerAlbumPageInfo MiguPageInspector::inspectSingerAlbumPage(const std::vector<char> &raw_data) const
{
    return detail::inspectSingerAlbumPageRaw(raw_data);
}

} // namespace orchestrator::modules::crawler::infrastructure::music::migu::parser
