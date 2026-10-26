#include "crawler/application/use_cases/ParserDispatch.h"

#include <utility>

namespace orchestrator::modules::crawler::application::use_cases {

ParserDispatch::ParserDispatch(
    std::shared_ptr<application::ports::ParseResultParser> parse_result_parser,
    std::shared_ptr<application::ports::SongSourceParser> song_source_parser,
    std::shared_ptr<application::ports::SingerSourceParser> singer_source_parser,
    std::shared_ptr<application::ports::AlbumSourceParser> album_source_parser,
    std::shared_ptr<application::ports::SingerProfileParser> singer_profile_parser,
    std::shared_ptr<application::ports::PageInspector> page_inspector,
    std::shared_ptr<application::ports::TaskExecutor> parse_executor,
    std::shared_ptr<application::ports::DiagnosticsSink> diagnostics
) : parse_result_parser_(std::move(parse_result_parser)),
    song_source_parser_(std::move(song_source_parser)),
    singer_source_parser_(std::move(singer_source_parser)),
    album_source_parser_(std::move(album_source_parser)),
    singer_profile_parser_(std::move(singer_profile_parser)),
    page_inspector_(std::move(page_inspector)),
    parse_runner_(std::move(parse_executor), std::move(diagnostics))
{}

std::vector<domain::ParseResult> ParserDispatch::parse(std::vector<char> body)
{
    if (!parse_result_parser_) {
        return {};
    }

    auto parser = parse_result_parser_;
    return parse_runner_.run<std::vector<domain::ParseResult>>(
        [parser, body = std::move(body)]() {
            return parser ? parser->parse(body) : std::vector<domain::ParseResult> {};
        },
        {},
        "parse metadata"
    );
}

std::vector<application::model::SongSourceMetadata> ParserDispatch::parseSongSources(std::vector<char> body)
{
    if (!song_source_parser_) {
        return {};
    }

    auto parser = song_source_parser_;
    return parse_runner_.run<std::vector<application::model::SongSourceMetadata>>(
        [parser, body = std::move(body)]() {
            return parser ? parser->parseSongSources(body) : std::vector<application::model::SongSourceMetadata> {};
        },
        {},
        "parse song sources"
    );
}

std::vector<application::model::SingerSourceMetadata> ParserDispatch::parseSingerSources(std::vector<char> body, std::string tab)
{
    if (!singer_source_parser_) {
        return {};
    }

    auto parser = singer_source_parser_;
    return parse_runner_.run<std::vector<application::model::SingerSourceMetadata>>(
        [parser, body = std::move(body), tab = std::move(tab)]() {
            return parser ? parser->parseSingerSources(body, tab) : std::vector<application::model::SingerSourceMetadata> {};
        },
        {},
        "parse singer sources"
    );
}

std::vector<application::model::AlbumSourceMetadata> ParserDispatch::parseAlbumSources(std::vector<char> body)
{
    if (!album_source_parser_) {
        return {};
    }

    auto parser = album_source_parser_;
    return parse_runner_.run<std::vector<application::model::AlbumSourceMetadata>>(
        [parser, body = std::move(body)]() {
            return parser ? parser->parseAlbumSources(body) : std::vector<application::model::AlbumSourceMetadata> {};
        },
        {},
        "parse album sources"
    );
}

application::model::SingerSourceProfile ParserDispatch::parseSingerSourceProfile(std::vector<char> body)
{
    if (!singer_profile_parser_) {
        return {};
    }

    auto parser = singer_profile_parser_;
    return parse_runner_.run<application::model::SingerSourceProfile>(
        [parser, body = std::move(body)]() {
            return parser ? parser->parseSingerSourceProfile(body) : application::model::SingerSourceProfile {};
        },
        {},
        "parse singer source profile"
    );
}

application::ports::SingerSongPageInfo ParserDispatch::inspectSingerSongPage(std::vector<char> body)
{
    if (!page_inspector_) {
        return {};
    }

    auto inspector = page_inspector_;
    return parse_runner_.run<application::ports::SingerSongPageInfo>(
        [inspector, body = std::move(body)]() {
            return inspector ? inspector->inspectSingerSongPage(body) : application::ports::SingerSongPageInfo {};
        },
        {},
        "inspect singer song page"
    );
}

application::ports::SingerAlbumPageInfo ParserDispatch::inspectSingerAlbumPage(std::vector<char> body)
{
    if (!page_inspector_) {
        return {};
    }

    auto inspector = page_inspector_;
    return parse_runner_.run<application::ports::SingerAlbumPageInfo>(
        [inspector, body = std::move(body)]() {
            return inspector ? inspector->inspectSingerAlbumPage(body) : application::ports::SingerAlbumPageInfo {};
        },
        {},
        "inspect singer album page"
    );
}

} // namespace orchestrator::modules::crawler::application::use_cases
