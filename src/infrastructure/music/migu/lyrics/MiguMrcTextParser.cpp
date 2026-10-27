#include "crawler/infrastructure/music/migu/lyrics/MiguMrcTextParser.h"

#include <algorithm>
#include <charconv>
#include <optional>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::migu::lyrics {
namespace {

// 解析非负毫秒值，保留 JSON 客户端可精确表达的整数范围。
std::optional<std::int64_t> parseTime(std::string_view value)
{
    std::int64_t number = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), number);
    if (parsed.ec != std::errc {} || parsed.ptr != value.data() + value.size() || number < 0 || number > 9007199254740991LL) return std::nullopt;
    return number;
}

// 一对时间必须都存在，且结束时间仍可无损地序列化给 JSON 客户端。
bool parsePair(std::string_view value, std::int64_t &start, std::int64_t &duration)
{
    const auto comma = value.find(',');
    if (comma == std::string_view::npos) return false;
    const auto first = parseTime(value.substr(0, comma));
    const auto second = parseTime(value.substr(comma + 1));
    if (!first || !second || *first > 9007199254740991LL - *second) return false;
    start = *first;
    duration = *second;
    return true;
}

// 时间元组位于文字后方；非时间括号作为歌词文字保留。
bool parseWords(std::string_view payload, domain::LyricLine &line)
{
    std::size_t text_start = 0;
    std::size_t search = 0;
    while (search < payload.size()) {
        const auto left = payload.find('(', search);
        if (left == std::string_view::npos) break;
        const auto right = payload.find(')', left + 1);
        if (right == std::string_view::npos) return false;
        domain::LyricWord word;
        if (!parsePair(payload.substr(left + 1, right - left - 1), word.start_ms, word.duration_ms)) {
            search = left + 1;
            continue;
        }
        word.text = std::string(payload.substr(text_start, left - text_start));
        if (word.text.empty() || (!line.words.empty() && word.start_ms < line.words.back().start_ms)) return false;
        line.words.push_back(std::move(word));
        text_start = right + 1;
        search = text_start;
    }
    // 零时长署名可能只给前缀标时；尾部沿用行级零时间，不能据此推测正常歌词的时间。
    if (text_start < payload.size() && line.start_ms == 0 && line.duration_ms == 0 && !line.words.empty()
        && std::all_of(line.words.begin(), line.words.end(), [](const domain::LyricWord &word) { return word.start_ms == 0 && word.duration_ms == 0; })) {
        line.words.push_back({std::string(payload.substr(text_start)), 0, 0});
        text_start = payload.size();
    }
    return text_start == payload.size() && !line.words.empty();
}

} // namespace

application::result::ServiceResult<domain::TimedLyrics> MiguMrcTextParser::parse(std::string_view content) const
{
    using Result = application::result::ServiceResult<domain::TimedLyrics>;
    using Error = application::result::ServiceErrorType;
    if (content.size() > 1024 * 1024) return Result::failure(Error::InvalidParam, "mrc_plaintext_too_large");
    domain::TimedLyrics lyrics;
    while (!content.empty()) {
        const auto end = content.find('\n');
        auto line = content.substr(0, end);
        content = end == std::string_view::npos ? std::string_view {} : content.substr(end + 1);
        if (!line.empty() && line.back() == '\r') line.remove_suffix(1);
        if (line.find_first_not_of(" \t") == std::string_view::npos) continue;
        const auto closing = line.find(']');
        if (!line.starts_with('[') || closing == std::string_view::npos) return Result::failure(Error::InvalidParam, "mrc_invalid_line");
        const auto header = line.substr(1, closing - 1);
        const auto colon = header.find(':');
        if (colon != std::string_view::npos && colon > 0 && closing + 1 == line.size()) {
            lyrics.tags.push_back({std::string(header.substr(0, colon)), std::string(header.substr(colon + 1))});
            continue;
        }
        domain::LyricLine parsed;
        if (!parsePair(header, parsed.start_ms, parsed.duration_ms) || !parseWords(line.substr(closing + 1), parsed)) return Result::failure(Error::InvalidParam, "mrc_invalid_timing");
        lyrics.lines.push_back(std::move(parsed));
    }
    if (lyrics.lines.empty()) return Result::failure(Error::InvalidParam, "mrc_missing_lyrics");
    return Result::success(std::move(lyrics));
}

} // namespace orchestrator::modules::crawler::infrastructure::music::migu::lyrics
