#include "crawler/infrastructure/music/lyrics/TimedLyricsEncoder.h"

#include <nlohmann/json.hpp>

#include <iomanip>
#include <locale>
#include <sstream>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::lyrics {
namespace {

// 使用三位毫秒精度，避免逐字时间在导出时被舍入。
std::string timestamp(std::int64_t milliseconds)
{
    std::ostringstream stream;
    stream.imbue(std::locale::classic());
    stream << std::setfill('0') << std::setw(2) << milliseconds / 60000 << ':' << std::setw(2) << milliseconds / 1000 % 60 << '.' << std::setw(3) << milliseconds % 1000;
    return stream.str();
}

// 标记字符会破坏 LRC 语法；JSON 保留原文，LRC 使用外观相近的全角字符。
std::string lrcText(std::string_view text)
{
    std::string escaped;
    for (const char value : text) {
        switch (value) {
        case '<': escaped += "＜"; break;
        case '>': escaped += "＞"; break;
        case '[': escaped += "［"; break;
        case ']': escaped += "］"; break;
        case '\r':
        case '\n': escaped += ' '; break;
        default: escaped += value; break;
        }
    }
    return escaped;
}

} // namespace

std::string_view TimedLyricsEncoder::version() const
{
    return "exports-v2";
}

int TimedLyricsEncoder::schemaVersion() const
{
    return 1;
}

application::result::ServiceResult<std::vector<application::model::EncodedLyricArtifact>> TimedLyricsEncoder::encode(const domain::TimedLyrics &lyrics) const
{
    using Result = application::result::ServiceResult<std::vector<application::model::EncodedLyricArtifact>>;
    using Error = application::result::ServiceErrorType;
    if (lyrics.lines.empty()) return Result::failure(Error::InvalidParam, "lyric_empty_document");
    nlohmann::json document = {{"schema_version", schemaVersion()}, {"time_unit", "ms"}, {"time_base", "absolute"}, {"metadata", nlohmann::json::array()}, {"lines", nlohmann::json::array()}};
    std::string enhanced;
    for (const auto &tag : lyrics.tags) {
        document["metadata"].push_back({{"name", tag.name}, {"value", tag.value}});
        enhanced += '[' + lrcText(tag.name) + ':' + lrcText(tag.value) + "]\n";
    }
    for (const auto &line : lyrics.lines) {
        if (line.start_ms < 0 || line.duration_ms < 0 || line.start_ms > 9007199254740991LL - line.duration_ms || line.words.empty()) return Result::failure(Error::InvalidParam, "lyric_invalid_timing");
        nlohmann::json words = nlohmann::json::array();
        enhanced += '[' + timestamp(line.start_ms) + ']';
        for (std::size_t index = 0; index < line.words.size(); ++index) {
            const auto &word = line.words[index];
            if (word.start_ms < 0 || word.duration_ms < 0 || word.start_ms > 9007199254740991LL - word.duration_ms) return Result::failure(Error::InvalidParam, "lyric_invalid_timing");
            words.push_back({{"text", word.text}, {"start_ms", word.start_ms}, {"duration_ms", word.duration_ms}});
            if (line.duration_ms != 0) enhanced += '<' + timestamp(word.start_ms) + '>';
            enhanced += lrcText(word.text);
            const auto end = word.start_ms + word.duration_ms;
            if (line.duration_ms != 0 && (index + 1 == line.words.size() || end < line.words[index + 1].start_ms)) enhanced += '<' + timestamp(end) + '>';
        }
        document["lines"].push_back({{"start_ms", line.start_ms}, {"duration_ms", line.duration_ms}, {"words", std::move(words)}});
        enhanced += '\n';
    }
    std::string json;
    try {
        json = document.dump() + '\n';
    } catch (const nlohmann::json::exception &) {
        return Result::failure(Error::InvalidParam, "lyric_invalid_utf8");
    }
    std::vector<application::model::EncodedLyricArtifact> files;
    files.push_back({"word_json", "words.json", "application/json", std::move(json)});
    files.push_back({"enhanced_lrc", "words.enhanced.lrc", "text/plain; charset=utf-8", std::move(enhanced)});
    return Result::success(std::move(files));
}

} // namespace orchestrator::modules::crawler::infrastructure::music::lyrics
