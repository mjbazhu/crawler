#pragma once

#include "crawler/application/model/AlbumSourceMetadata.h"

#include <cstddef>
#include <string>
#include <vector>

namespace orchestrator::modules::crawler::application::ports {

// 专辑来源同步队列 port，只负责查询和更新详情/歌曲同步状态。
class AlbumSourceSyncQueue {
public:
    virtual ~AlbumSourceSyncQueue() = default;

    // 查询需要补齐详情的专辑来源，用于把列表采集和详情采集分离。
    virtual std::vector<application::model::AlbumSourceMetadata> findPendingDetails(std::string source, std::size_t limit, std::size_t offset) = 0;

    // 更新专辑详情同步状态；状态定义：0 未同步，1 同步中，2 已完成，3 失败，4 部分完成。
    virtual bool updateDetailSyncStatus(std::string source, std::string source_album_id, int status, std::string error) = 0;

    // 查询需要获取歌曲列表的专辑来源；调用方应只处理详情已完成的来源。
    virtual std::vector<application::model::AlbumSourceMetadata> findPendingSongs(std::string source, std::size_t limit, std::size_t offset) = 0;

    // 更新专辑歌曲列表同步状态；状态定义：0 未同步，1 同步中，2 已完成，3 失败，4 部分完成。
    virtual bool updateSongsSyncStatus(std::string source, std::string source_album_id, int status, int count, std::string error) = 0;
};

} // namespace orchestrator::modules::crawler::application::ports
