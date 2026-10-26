#pragma once

#include <optional>
#include <string>

namespace orchestrator::modules::crawler::application::model {

// 歌手歌曲或专辑采集结果，用于同步状态和批次 item 写入数量。
struct SingerCollectionResult {
    bool success = false;        // 是否完整成功
    bool partial = false;        // 是否部分成功
    int written_count = 0;       // 本次写入或解析出的记录数量
    std::string error;           // 失败或部分完成原因
    std::optional<int> source_song_total;  // 平台声明歌曲总数，空表示未知
    std::optional<int> source_album_total; // 平台声明专辑总数，空表示未知
};

} // namespace orchestrator::modules::crawler::application::model
