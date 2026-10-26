#pragma once

#include "crawler/application/model/AlbumSourceMetadata.h"
#include "crawler/application/model/SingerSourceMetadata.h"
#include "crawler/application/model/SongSourceMetadata.h"
#include "crawler/application/ports/diagnostics/DiagnosticsSink.h"
#include "crawler/application/ports/execution/TaskExecutor.h"
#include "crawler/application/ports/parsing/AlbumSourceParser.h"
#include "crawler/application/ports/parsing/PageInspector.h"
#include "crawler/application/ports/parsing/ParseResultParser.h"
#include "crawler/application/ports/parsing/SingerProfileParser.h"
#include "crawler/application/ports/parsing/SingerSourceParser.h"
#include "crawler/application/ports/parsing/SongSourceParser.h"
#include "crawler/application/use_cases/TaskRunner.h"
#include "crawler/domain/music/model/ParseResult.h"

#include <memory>
#include <string>
#include <vector>

namespace orchestrator::modules::crawler::application::use_cases {

// 解析调度器，统一把解析工作投递到解析执行器并屏蔽 parser 空指针。
class ParserDispatch {
public:
    // 保存窄 parser port 和解析执行器。
    ParserDispatch(
        std::shared_ptr<application::ports::ParseResultParser> parse_result_parser,
        std::shared_ptr<application::ports::SongSourceParser> song_source_parser,
        std::shared_ptr<application::ports::SingerSourceParser> singer_source_parser,
        std::shared_ptr<application::ports::AlbumSourceParser> album_source_parser,
        std::shared_ptr<application::ports::SingerProfileParser> singer_profile_parser,
        std::shared_ptr<application::ports::PageInspector> page_inspector,
        std::shared_ptr<application::ports::TaskExecutor> parse_executor,
        std::shared_ptr<application::ports::DiagnosticsSink> diagnostics
    );

    // 解析领域结果。
    std::vector<domain::ParseResult> parse(std::vector<char> body);

    // 解析歌曲来源。
    std::vector<application::model::SongSourceMetadata> parseSongSources(std::vector<char> body);

    // 解析歌手来源。
    std::vector<application::model::SingerSourceMetadata> parseSingerSources(std::vector<char> body, std::string tab);

    // 解析专辑来源。
    std::vector<application::model::AlbumSourceMetadata> parseAlbumSources(std::vector<char> body);

    // 解析歌手来源概览。
    application::model::SingerSourceProfile parseSingerSourceProfile(std::vector<char> body);

    // 检查歌手歌曲分页摘要。
    application::ports::SingerSongPageInfo inspectSingerSongPage(std::vector<char> body);

    // 检查歌手专辑分页摘要。
    application::ports::SingerAlbumPageInfo inspectSingerAlbumPage(std::vector<char> body);

private:
    std::shared_ptr<application::ports::ParseResultParser> parse_result_parser_;   // 领域解析 port
    std::shared_ptr<application::ports::SongSourceParser> song_source_parser_;     // 歌曲来源解析 port
    std::shared_ptr<application::ports::SingerSourceParser> singer_source_parser_; // 歌手来源解析 port
    std::shared_ptr<application::ports::AlbumSourceParser> album_source_parser_;   // 专辑来源解析 port
    std::shared_ptr<application::ports::SingerProfileParser> singer_profile_parser_; // 歌手概览解析 port
    std::shared_ptr<application::ports::PageInspector> page_inspector_;            // 分页摘要 port
    TaskRunner parse_runner_;                                                      // 解析执行器
};

} // namespace orchestrator::modules::crawler::application::use_cases
