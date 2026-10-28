#pragma once

#include "crawler/application/model/AlbumSourceMetadata.h"

#include <charconv>
#include <optional>
#include <set>
#include <string>
#include <vector>

namespace orchestrator::modules::crawler::application::use_cases {

// Migu 的普通单曲归档由不同歌手共用，只保存当前响应，不自动展开整个集合。
inline bool isSharedReleaseArchive(const application::model::AlbumSourceMetadata &album)
{
    return album.source == "migu" && album.source_album_id == "1"
           && (album.title.empty() || album.title == "单曲发行")
           && album.source_album_kind != 2 && album.source_res_type != "1" && album.source_resource_type != "5"
           && (album.source_material_id.empty() || album.source_material_id == album.source_album_id);
}

// 声明总数必须为非负整数，缺失或非法值保持未知。
inline std::optional<int> declaredAlbumSongCount(const std::string &text)
{
    int count = -1;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), count);
    if (parsed.ec == std::errc {} && parsed.ptr == text.data() + text.size() && count >= 0) return count;
    return std::nullopt;
}

// 只用本次详情及分页中属于该专辑的唯一曲目判断完整性，不查询历史关系总数。
inline application::model::AlbumSongSyncResult albumSongSyncResult(const application::model::AlbumSourceMetadata &album, const std::vector<application::model::SongSourceMetadata> &songs)
{
    std::set<std::string> identities;
    for (const auto &song : songs) {
        if (song.source == album.source && song.source_album_id == album.source_album_id) {
            if (!song.source_content_id.empty())
                identities.insert("content:" + song.source_content_id);
            else if (!song.source_song_id.empty())
                identities.insert("song:" + song.source_song_id);
        }
    }
    application::model::AlbumSongSyncResult result;
    result.received_count = static_cast<int>(identities.size());
    const auto expected = declaredAlbumSongCount(album.total_count);
    if (!expected)
        result.error = "album song completeness unknown: totalCount unavailable";
    else if (result.received_count == *expected)
        result.status = 2;
    else
        result.error = "album songs incomplete: received " + std::to_string(result.received_count) + " of " + std::to_string(*expected);
    return result;
}

} // namespace orchestrator::modules::crawler::application::use_cases
