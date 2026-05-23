#pragma once

#include "crawler/application/ports/persistence/AlbumSourceRepository.h"

#include <algorithm>
#include <cstddef>
#include <string>
#include <vector>

namespace orchestrator::modules::crawler::tests {

class MockAlbumSourceRepository : public application::ports::AlbumSourceRepository {
public:
    std::vector<application::model::AlbumSourceMetadata> saved_albums; // 所有保存过的专辑来源
    std::vector<application::model::AlbumSourceMetadata> pending_details; // 预设待补齐详情的专辑来源
    std::vector<application::model::AlbumSourceMetadata> pending_songs;   // 预设待获取歌曲的专辑来源
    int save_calls = 0;                                                // 保存调用次数
    int update_detail_calls = 0;                                       // 更新详情同步状态调用次数
    int update_songs_calls = 0;                                        // 更新歌曲同步状态调用次数
    int last_detail_status = 0;                                        // 最近一次详情同步状态
    int last_songs_status = 0;                                         // 最近一次歌曲同步状态
    int last_songs_count = 0;                                          // 最近一次歌曲同步数量

    bool saveBatch(const std::vector<application::model::AlbumSourceMetadata> &albums) override
    {
        ++save_calls;
        saved_albums.insert(saved_albums.end(), albums.begin(), albums.end());
        return !albums.empty();
    }

    std::vector<application::model::AlbumSourceMetadata> findPendingDetails(std::string source, std::size_t limit, std::size_t offset) override
    {
        (void)source;
        if (offset >= pending_details.size()) {
            return {};
        }
        const auto end = (std::min)(pending_details.size(), offset + limit);
        return {pending_details.begin() + static_cast<std::ptrdiff_t>(offset), pending_details.begin() + static_cast<std::ptrdiff_t>(end)};
    }

    bool updateDetailSyncStatus(std::string source, std::string source_album_id, int status, std::string error) override
    {
        (void)source;
        (void)source_album_id;
        (void)error;
        ++update_detail_calls;
        last_detail_status = status;
        return true;
    }

    std::vector<application::model::AlbumSourceMetadata> findPendingSongs(std::string source, std::size_t limit, std::size_t offset) override
    {
        (void)source;
        if (offset >= pending_songs.size()) {
            return {};
        }
        const auto end = (std::min)(pending_songs.size(), offset + limit);
        return {pending_songs.begin() + static_cast<std::ptrdiff_t>(offset), pending_songs.begin() + static_cast<std::ptrdiff_t>(end)};
    }

    bool updateSongsSyncStatus(std::string source, std::string source_album_id, int status, int count, std::string error) override
    {
        (void)source;
        (void)source_album_id;
        (void)error;
        ++update_songs_calls;
        last_songs_status = status;
        last_songs_count = count;
        return true;
    }
};

} // namespace orchestrator::modules::crawler::tests
