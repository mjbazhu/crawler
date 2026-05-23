#include "crawler/infrastructure/music/migu/provider/MiguUrlResolver.h"

#include "crawler/domain/music/model/Album.h"
#include "crawler/infrastructure/music/migu/provider/MiguRequestFactory.h"
#include "crawler/infrastructure/observability/logging/Logger.h"

#include <cstdint>
#include <string>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::migu::provider {

namespace {

// 从页面 URL 末尾解析连续数字 ID。
std::optional<std::uint64_t> parseTrailingId(const std::string &url, std::string_view context)
{
    auto pos = url.find_last_of('/');
    if (pos == std::string::npos) {
        return std::nullopt;
    }

    std::string id_text = url.substr(pos + 1);
    const std::size_t param_pos = id_text.find_first_not_of("0123456789");
    if (param_pos != std::string::npos) {
        id_text = id_text.substr(0, param_pos);
    }

    try {
        return static_cast<std::uint64_t>(std::stoull(id_text));
    } catch (...) {
        LOG_ERROR("[MiguUrlResolver] Failed to parse " + std::string(context) + " ID from URL: " + url);
        return std::nullopt;
    }
}

} // namespace

std::optional<std::string> MiguUrlResolver::resolve(std::string url)
{
    std::string target_url(std::move(url));

    if (target_url.find("/album/") != std::string::npos) {
        auto album_id = parseTrailingId(target_url, "album");
        if (!album_id) {
            return std::nullopt;
        }
        return MiguRequestFactory::albumDetailUrl(*album_id, "0");
    }

    if (target_url.find("/song/") != std::string::npos) {
        auto song_id = parseTrailingId(target_url, "song");
        if (!song_id) {
            return std::nullopt;
        }
        domain::Album pseudo_album;
        pseudo_album.album_id = *song_id;
        pseudo_album.type = 2;
        return MiguRequestFactory::albumSongsUrl(pseudo_album);
    }

    return target_url;
}

} // namespace orchestrator::modules::crawler::infrastructure::music::migu::provider
