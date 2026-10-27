#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace orchestrator::modules::crawler::domain {

// 歌词标签，按来源顺序保留重复标签，不依赖平台或序列化格式。
struct LyricTag {
    std::string name;  // 标签名称
    std::string value; // 标签原值
};

// 一个字或词片段；时间均为相对整首歌曲起点的绝对毫秒。
struct LyricWord {
    std::string text;             // UTF-8 文本，保留空格与标点
    std::int64_t start_ms = 0;    // 片段开始时间
    std::int64_t duration_ms = 0; // 片段持续时间，0 表示来源未提供有效时长
};

// 一行逐字歌词；允许不同歌词行的时间区间重叠。
struct LyricLine {
    std::int64_t start_ms = 0;    // 行开始时间
    std::int64_t duration_ms = 0; // 行持续时间
    std::vector<LyricWord> words; // 有序文本片段
};

// 与音乐平台、JSON、数据库及文件系统无关的逐字歌词值模型。
struct TimedLyrics {
    std::vector<LyricTag> tags;   // 来源标签
    std::vector<LyricLine> lines; // 来源顺序的歌词行，保留零时长署名行
};

} // namespace orchestrator::modules::crawler::domain
