#pragma once

#include "crawler/application/model/AlbumSongPage.h"
#include "crawler/application/result/ServiceResult.h"

#include "crawler/application/ports/parsing/MetadataParser.h"

#include "crawler/application/ports/parsing/MetadataBatchParser.h"

#include <memory>
#include <string_view>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::music::migu::parser {

class MiguAlbumSourceParser;
class MiguPageInspector;
class MiguParseResultParser;
class MiguSingerProfileParser;
class MiguSingerSourceParser;
class MiguSongSourceParser;

// Migu 解析兼容门面，只委托给按能力拆分的窄 parser。
class MiguParser : public application::ports::MetadataParser, public application::ports::MetadataBatchParser {
public:
    // 创建所有 Migu 窄 parser。
    MiguParser();
    ~MiguParser() override;

    // 一次解码响应，供生产入库路径分流实体。
    application::model::MetadataWriteBatch parseMetadataBatch(const std::vector<char> &body) const override;
    // 委托批次 parser 保留平台明确拒绝的结果。
    application::result::ServiceResult<application::model::MetadataWriteBatch> parseMetadataResponse(const std::vector<char> &body) const override;

    // 解析 Migu 平台响应为领域结果。
    std::vector<domain::ParseResult> parse(const std::vector<char> &raw_data) const override;

    // 解析可直接入库的歌曲平台来源元数据。
    std::vector<application::model::SongSourceMetadata> parseSongSources(const std::vector<char> &raw_data) const override;

    // 委托歌曲来源 parser 处理专辑分页结构。
    application::model::AlbumSongPage parseAlbumSongPage(const std::vector<char> &raw_data) const override;

    // 解析可直接入库的歌手平台来源元数据。
    std::vector<application::model::SingerSourceMetadata> parseSingerSources(const std::vector<char> &raw_data, std::string_view tab) const override;

    // 解析可直接入库的专辑平台来源元数据。
    std::vector<application::model::AlbumSourceMetadata> parseAlbumSources(const std::vector<char> &raw_data) const override;

    // 解析歌手主要信息中的平台声明总数。
    application::model::SingerSourceProfile parseSingerSourceProfile(const std::vector<char> &raw_data) const override;

    // 检查歌手歌曲分页是否结束。
    SingerSongPageInfo inspectSingerSongPage(const std::vector<char> &raw_data) const override;

    // 检查歌手专辑分页是否结束。
    SingerAlbumPageInfo inspectSingerAlbumPage(const std::vector<char> &raw_data) const override;

private:
    std::unique_ptr<MiguParseResultParser> parse_result_parser_;     // 领域结果 parser
    std::unique_ptr<MiguSongSourceParser> song_source_parser_;       // 歌曲来源 parser
    std::unique_ptr<MiguSingerSourceParser> singer_source_parser_;   // 歌手来源 parser
    std::unique_ptr<MiguAlbumSourceParser> album_source_parser_;     // 专辑来源 parser
    std::unique_ptr<MiguSingerProfileParser> singer_profile_parser_; // 歌手概览 parser
    std::unique_ptr<MiguPageInspector> page_inspector_;              // 分页检查 parser
};

} // namespace orchestrator::modules::crawler::infrastructure::music::migu::parser
