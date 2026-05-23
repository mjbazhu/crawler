#pragma once

#include "crawler/application/ports/parsing/MetadataParser.h"

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace orchestrator::modules::crawler::tests {

class MockMetadataParser : public application::ports::MetadataParser {
public:
    std::vector<domain::ParseResult> mock_result;                                                                       // 预设解析结果
    std::unordered_map<std::string, std::vector<domain::ParseResult>> mock_results_by_body;                             // 按响应体区分解析结果
    std::unordered_map<std::string, std::vector<application::model::SongSourceMetadata>> mock_song_sources_by_body;     // 按响应体区分歌曲来源解析结果
    std::unordered_map<std::string, std::vector<application::model::SingerSourceMetadata>> mock_singer_sources_by_body; // 按响应体区分歌手来源解析结果
    std::unordered_map<std::string, std::vector<application::model::AlbumSourceMetadata>> mock_album_sources_by_body;   // 按响应体区分专辑来源解析结果

    std::vector<domain::ParseResult> parse(const std::vector<char> &raw_data) const override
    {
        const std::string body(raw_data.begin(), raw_data.end());
        if (auto it = mock_results_by_body.find(body); it != mock_results_by_body.end()) {
            return it->second;
        }
        return mock_result;
    }

    std::vector<application::model::SongSourceMetadata> parseSongSources(const std::vector<char> &raw_data) const override
    {
        const std::string body(raw_data.begin(), raw_data.end());
        if (auto it = mock_song_sources_by_body.find(body); it != mock_song_sources_by_body.end()) {
            return it->second;
        }
        return {};
    }

    std::vector<application::model::SingerSourceMetadata> parseSingerSources(const std::vector<char> &raw_data, std::string_view tab) const override
    {
        (void)tab;
        const std::string body(raw_data.begin(), raw_data.end());
        if (auto it = mock_singer_sources_by_body.find(body); it != mock_singer_sources_by_body.end()) {
            return it->second;
        }
        return {};
    }

    std::vector<application::model::AlbumSourceMetadata> parseAlbumSources(const std::vector<char> &raw_data) const override
    {
        const std::string body(raw_data.begin(), raw_data.end());
        if (auto it = mock_album_sources_by_body.find(body); it != mock_album_sources_by_body.end()) {
            return it->second;
        }
        return {};
    }
};

} // namespace orchestrator::modules::crawler::tests
