#include "crawler/infrastructure/music/migu/parser/MiguParser.h"

#include "crawler/infrastructure/music/migu/parser/MiguAlbumSourceParser.h"
#include "crawler/infrastructure/music/migu/parser/MiguPageInspector.h"
#include "crawler/infrastructure/music/migu/parser/MiguParseResultParser.h"
#include "crawler/infrastructure/music/migu/parser/MiguSingerProfileParser.h"
#include "crawler/infrastructure/music/migu/parser/MiguSingerSourceParser.h"
#include "crawler/infrastructure/music/migu/parser/MiguSongSourceParser.h"

#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::migu::parser {

MiguParser::MiguParser() : parse_result_parser_(std::make_unique<MiguParseResultParser>()),
                           song_source_parser_(std::make_unique<MiguSongSourceParser>()),
                           singer_source_parser_(std::make_unique<MiguSingerSourceParser>()),
                           album_source_parser_(std::make_unique<MiguAlbumSourceParser>()),
                           singer_profile_parser_(std::make_unique<MiguSingerProfileParser>()),
                           page_inspector_(std::make_unique<MiguPageInspector>())
{}

MiguParser::~MiguParser() = default;

std::vector<domain::ParseResult> MiguParser::parse(const std::vector<char> &raw_data) const
{
    return parse_result_parser_ ? parse_result_parser_->parse(raw_data) : std::vector<domain::ParseResult> {};
}

std::vector<application::model::SongSourceMetadata> MiguParser::parseSongSources(const std::vector<char> &raw_data) const
{
    return song_source_parser_ ? song_source_parser_->parseSongSources(raw_data) : std::vector<application::model::SongSourceMetadata> {};
}

std::vector<application::model::SingerSourceMetadata> MiguParser::parseSingerSources(const std::vector<char> &raw_data, std::string_view tab) const
{
    return singer_source_parser_ ? singer_source_parser_->parseSingerSources(raw_data, tab) : std::vector<application::model::SingerSourceMetadata> {};
}

std::vector<application::model::AlbumSourceMetadata> MiguParser::parseAlbumSources(const std::vector<char> &raw_data) const
{
    return album_source_parser_ ? album_source_parser_->parseAlbumSources(raw_data) : std::vector<application::model::AlbumSourceMetadata> {};
}

application::model::SingerSourceProfile MiguParser::parseSingerSourceProfile(const std::vector<char> &raw_data) const
{
    return singer_profile_parser_ ? singer_profile_parser_->parseSingerSourceProfile(raw_data) : application::model::SingerSourceProfile {};
}

application::ports::MetadataParser::SingerSongPageInfo MiguParser::inspectSingerSongPage(const std::vector<char> &raw_data) const
{
    return page_inspector_ ? page_inspector_->inspectSingerSongPage(raw_data) : SingerSongPageInfo {};
}

application::ports::MetadataParser::SingerAlbumPageInfo MiguParser::inspectSingerAlbumPage(const std::vector<char> &raw_data) const
{
    return page_inspector_ ? page_inspector_->inspectSingerAlbumPage(raw_data) : SingerAlbumPageInfo {};
}

} // namespace orchestrator::modules::crawler::infrastructure::music::migu::parser
