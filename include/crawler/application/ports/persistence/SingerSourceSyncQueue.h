#pragma once

#include "crawler/application/model/SingerSourceSync.h"

#include <string>
#include <vector>

namespace orchestrator::modules::crawler::application::ports {

// 歌手来源同步队列 port，只负责查询、认领和更新歌曲/专辑列表同步状态。
class SingerSourceSyncQueue {
public:
    virtual ~SingerSourceSyncQueue() = default;

    // 认领需要同步歌曲列表的歌手来源，并把状态置为同步中。
    virtual std::vector<application::model::SingerSourceSyncTarget> claimPendingSongs(application::model::SingerSourceSyncQuery query) = 0;

    // 认领需要同步专辑列表的歌手来源，并把状态置为同步中。
    virtual std::vector<application::model::SingerSourceSyncTarget> claimPendingAlbums(application::model::SingerSourceSyncQuery query) = 0;

    // 预览需要同步歌曲列表的歌手来源；不修改同步状态。
    virtual application::model::SingerSourceSyncPreviewResult previewPendingSongs(application::model::SingerSourceSyncQuery query) = 0;

    // 预览需要同步专辑列表的歌手来源；不修改同步状态。
    virtual application::model::SingerSourceSyncPreviewResult previewPendingAlbums(application::model::SingerSourceSyncQuery query) = 0;

    // 更新歌手歌曲列表同步状态；状态定义：0 未同步，1 同步中，2 已完成，3 失败，4 部分完成。
    virtual bool updateSongsSyncStatus(std::string source, std::string source_singer_id, int status, int count, std::string error) = 0;

    // 更新歌手专辑列表同步状态；状态定义：0 未同步，1 同步中，2 已完成，3 失败，4 部分完成。
    virtual bool updateAlbumsSyncStatus(std::string source, std::string source_singer_id, int status, int count, std::string error) = 0;
};

} // namespace orchestrator::modules::crawler::application::ports
