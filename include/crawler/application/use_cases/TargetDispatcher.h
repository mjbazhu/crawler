#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace orchestrator::modules::crawler::application::use_cases {

// 采集目标类型，表示用户输入应进入哪个用例分支。
enum class CrawlTargetKind {
    Keyword,
    Url,
    SingerSongs,
    SingerIndex,
    SingerAlbums,
    AlbumDetails,
    AlbumSongs
};

// 采集目标解析结果，按 kind 读取对应字段。
struct CrawlTarget {
    CrawlTargetKind kind = CrawlTargetKind::Keyword; // 目标分支
    std::string text;                                // URL、关键词或 tab
    std::uint64_t singer_id = 0;                     // 歌手 ID
};

// 目标分发器，只负责把输入字符串识别为稳定的 application 目标。
class TargetDispatcher {
public:
    // 解析输入目标；无法识别特殊前缀时按普通关键词处理。
    CrawlTarget dispatch(std::string_view value) const;
};

} // namespace orchestrator::modules::crawler::application::use_cases
