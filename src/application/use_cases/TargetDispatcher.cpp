#include "crawler/application/use_cases/TargetDispatcher.h"

#include "crawler/domain/music/model/Singer.h"

#include <optional>
#include <string>

namespace orchestrator::modules::crawler::application::use_cases {

namespace {

// 判断目标是否是 URL。
bool isUrl(std::string_view value)
{
    return value.rfind("http://", 0) == 0 || value.rfind("https://", 0) == 0;
}

// 按前缀解析无符号 ID。
std::optional<std::uint64_t> parseIdTarget(std::string_view value, const std::initializer_list<std::string_view> &prefixes)
{
    std::string_view id_text;
    for (std::string_view prefix : prefixes) {
        if (value.rfind(prefix, 0) == 0) {
            id_text = value.substr(prefix.size());
            break;
        }
    }
    if (id_text.empty()) {
        return std::nullopt;
    }

    try {
        std::size_t parsed = 0;
        const auto id = std::stoull(std::string(id_text), &parsed);
        if (parsed != id_text.size() || id == 0) {
            return std::nullopt;
        }
        return static_cast<std::uint64_t>(id);
    } catch (...) {
        return std::nullopt;
    }
}

// 解析歌手歌曲目标。
std::optional<std::uint64_t> parseSingerTarget(std::string_view value)
{
    return parseIdTarget(value, {"singer:", "singerId:", "singer_id:", "migu:singer:"});
}

// 解析歌手专辑目标。
std::optional<std::uint64_t> parseSingerAlbumsTarget(std::string_view value)
{
    return parseIdTarget(value, {"singer-albums:", "singerAlbums:", "singer_album:", "migu:singer-albums:"});
}

// 解析歌手分类目标。
std::optional<std::string> parseSingerIndexTarget(std::string_view value)
{
    constexpr std::string_view prefixes[] = {
        "singer-index:",
        "singerIndex:",
        "singer-tab:",
        "migu:singer-index:",
    };

    for (std::string_view prefix : prefixes) {
        if (value.rfind(prefix, 0) == 0) {
            std::string tab(value.substr(prefix.size()));
            if (domain::kTabMap.find(tab) != domain::kTabMap.end()) {
                return tab;
            }
            return std::nullopt;
        }
    }
    return std::nullopt;
}

// 按前缀解析来源字符串。
std::optional<std::string> parseSourceTarget(std::string_view value, const std::initializer_list<std::string_view> &prefixes)
{
    for (std::string_view prefix : prefixes) {
        if (value.rfind(prefix, 0) == 0) {
            std::string source(value.substr(prefix.size()));
            if (!source.empty()) {
                return source;
            }
            return std::nullopt;
        }
    }
    return std::nullopt;
}

} // namespace

CrawlTarget TargetDispatcher::dispatch(std::string_view value) const
{
    if (isUrl(value)) {
        return CrawlTarget {CrawlTargetKind::Url, std::string(value), 0};
    }
    if (auto singer_id = parseSingerTarget(value)) {
        return CrawlTarget {CrawlTargetKind::SingerSongs, {}, *singer_id};
    }
    if (auto singer_tab = parseSingerIndexTarget(value)) {
        return CrawlTarget {CrawlTargetKind::SingerIndex, std::move(*singer_tab), 0};
    }
    if (auto singer_albums_id = parseSingerAlbumsTarget(value)) {
        return CrawlTarget {CrawlTargetKind::SingerAlbums, {}, *singer_albums_id};
    }
    if (auto source = parseSourceTarget(value, {"album-details:", "migu:album-details:"})) {
        return CrawlTarget {CrawlTargetKind::AlbumDetails, std::move(*source), 0};
    }
    if (auto source = parseSourceTarget(value, {"album-songs:", "migu:album-songs:"})) {
        return CrawlTarget {CrawlTargetKind::AlbumSongs, std::move(*source), 0};
    }
    return CrawlTarget {CrawlTargetKind::Keyword, std::string(value), 0};
}

} // namespace orchestrator::modules::crawler::application::use_cases
