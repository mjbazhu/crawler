#include "MiguSongFieldParsing.h"
#include "MiguParserSupportInternal.h"

#include "crawler/infrastructure/json/JsonUtils.h"

#include <charconv>
#include <limits>
#include <set>
#include <string>
#include <string_view>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::migu::parser::detail {
namespace {

// 数字可能以 JSON 字符串返回，拒绝负数和溢出值。
std::uint64_t unsignedField(const nlohmann::json &item, const char *key)
{
    const auto text = json::JsonUtils::getString(item, key);
    std::uint64_t value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    return parsed.ec == std::errc {} && parsed.ptr == text.data() + text.size() ? value : 0;
}

// 两种响应仅在字段名不同，不改变平台返回的格式代码。
std::string stringAlias(const nlohmann::json &item, const char *first, const char *second, const char *fallback)
{
    auto value = json::JsonUtils::getString(item, first);
    if (value.empty()) value = json::JsonUtils::getString(item, second);
    if (value.empty()) value = json::JsonUtils::getString(item, fallback);
    return value;
}

// 保留空片段以维持 ID 和姓名的对应位置，只去掉字段两端的空白。
std::vector<std::string> splitArtistField(const std::string &value)
{
    std::vector<std::string> parts;
    if (value.empty()) return parts;
    std::size_t start = 0;
    while (start <= value.size()) {
        const auto end = value.find('|', start);
        auto part = value.substr(start, end == std::string::npos ? end : end - start);
        const auto first = part.find_first_not_of(" \t\r\n");
        if (first == std::string::npos)
            part.clear();
        else
            part = part.substr(first, part.find_last_not_of(" \t\r\n") - first + 1);
        parts.push_back(std::move(part));
        if (end == std::string::npos) break;
        start = end + 1;
    }
    return parts;
}

// 数组结构优先于旧的分隔字段；数组元素必须代表一个独立歌手身份。
void appendArtistArray(const nlohmann::json *artists, std::vector<application::model::SongSourceArtistRef> &result)
{
    if (!artists) return;
    for (const auto &artist : *artists) {
        application::model::SongSourceArtistRef ref;
        ref.source_artist_id = stringAlias(artist, "id", "singerId", "artistId");
        if (ref.source_artist_id.empty() || ref.source_artist_id.find('|') != std::string::npos) continue;
        ref.source_artist_name = json::JsonUtils::getString(artist, "name");
        ref.source_artist_name_pinyin = json::JsonUtils::getString(artist, "nameSpelling");
        ref.source_artist_image = normalizeMiguResourceUrl(json::JsonUtils::getString(artist, "img"));
        ref.artist_order = static_cast<std::uint32_t>(result.size()) + 1;
        ref.is_primary = result.empty();
        result.push_back(std::move(ref));
    }
}

// singerImgs 是同一歌手的多种分辨率；只有唯一且明确匹配的身份才能使用。
void fillSingleArtistImage(const nlohmann::json &item, std::vector<application::model::SongSourceArtistRef> &artists)
{
    if (artists.size() != 1 || !artists.front().source_artist_image.empty()) return;
    const auto identities = splitArtistField(json::JsonUtils::getString(item, "singerId"));
    if (identities.size() != 1 || identities.front() != artists.front().source_artist_id) return;
    artists.front().source_artist_image = normalizeMiguResourceUrl(selectImageUrl(json::JsonUtils::getArray(item, "singerImgs")));
}

} // namespace

std::uint32_t parseSongDuration(const nlohmann::json &song)
{
    const auto duration = unsignedField(song, "duration");
    if (duration > 0 && duration <= (std::numeric_limits<std::uint32_t>::max)()) return static_cast<std::uint32_t>(duration);
    const std::string length = json::JsonUtils::getString(song, "length");
    std::string_view remaining = length;
    std::uint64_t seconds = 0;
    int segments = 0;
    while (!remaining.empty() && segments < 3) {
        const auto separator = remaining.find(':');
        const auto part = remaining.substr(0, separator);
        std::uint64_t value = 0;
        const auto parsed = std::from_chars(part.data(), part.data() + part.size(), value);
        if (parsed.ec != std::errc {} || parsed.ptr != part.data() + part.size() || value > 86400) return 0;
        if (segments > 0 && value >= 60) return 0;
        seconds = seconds * 60 + value;
        ++segments;
        if (separator == std::string_view::npos) return seconds <= (std::numeric_limits<std::uint32_t>::max)() ? static_cast<std::uint32_t>(seconds) : 0;
        remaining.remove_prefix(separator + 1);
    }
    return 0;
}

std::vector<application::model::SongSourceArtistRef> parseSourceArtists(const nlohmann::json &item)
{
    std::vector<application::model::SongSourceArtistRef> result;
    appendArtistArray(json::JsonUtils::getArray(item, "singerList"), result);
    if (result.empty()) appendArtistArray(json::JsonUtils::getArray(item, "artists"), result);
    if (!result.empty()) {
        fillSingleArtistImage(item, result);
        return result;
    }

    const auto ids = splitArtistField(json::JsonUtils::getString(item, "singerId"));
    const auto names = splitArtistField(json::JsonUtils::getString(item, "singer"));
    std::set<std::string> seen;
    for (std::size_t index = 0; index < ids.size(); ++index) {
        if (ids[index].empty() || !seen.insert(ids[index]).second) continue;
        application::model::SongSourceArtistRef ref;
        ref.source_artist_id = ids[index];
        // 数量不匹配时只保留身份，不能把合唱显示名冒充任意单个歌手。
        if (names.size() == ids.size()) ref.source_artist_name = names[index];
        ref.artist_order = static_cast<std::uint32_t>(result.size()) + 1;
        ref.is_primary = result.empty();
        result.push_back(std::move(ref));
    }
    fillSingleArtistImage(item, result);
    return result;
}

std::vector<application::model::SongSourceAudioFormat> parseSourceAudioFormats(const nlohmann::json &song)
{
    std::set<std::string> seen;
    std::vector<application::model::SongSourceAudioFormat> result;
    // 新字段优先；旧字段只能补充没有对应音质的条目。
    for (const auto *key : {"audioFormats", "newRateFormats", "rateFormats"}) {
        const auto *items = json::JsonUtils::getArray(song, key);
        if (!items) continue;
        for (const auto &item : *items) {
            application::model::SongSourceAudioFormat format;
            format.format_type = json::JsonUtils::getString(item, "formatType");
            if (format.format_type == "ZQ") format.format_type = "ZQ24";
            if (format.format_type.empty() || seen.contains(format.format_type)) continue;
            format.resource_type = json::JsonUtils::getString(item, "resourceType");
            format.iformat = stringAlias(item, "iformat", "iosFormat", "format");
            format.aformat = stringAlias(item, "aformat", "androidFormat", "format");
            format.isize = unsignedField(item, "isize");
            if (format.isize == 0) format.isize = unsignedField(item, "iosSize");
            if (format.isize == 0) format.isize = unsignedField(item, "size");
            format.asize = unsignedField(item, "asize");
            if (format.asize == 0) format.asize = unsignedField(item, "androidSize");
            if (format.asize == 0) format.asize = unsignedField(item, "size");
            seen.insert(format.format_type);
            result.push_back(std::move(format));
        }
    }
    return result;
}

} // namespace orchestrator::modules::crawler::infrastructure::music::migu::parser::detail
