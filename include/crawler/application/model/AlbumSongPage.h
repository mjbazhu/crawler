#pragma once

#include "crawler/application/model/SongSourceMetadata.h"

#include <cstddef>
#include <optional>
#include <vector>

namespace orchestrator::modules::crawler::application::model {

// 专辑曲目分页的已解析结果，不泄漏平台 JSON 或不可靠的 hasNext 标记。
struct AlbumSongPage {
    std::vector<SongSourceMetadata> songs; // 当前页有效歌曲，保留页内位置
    std::optional<int> source_song_total;  // 当前响应声明总数，缺失时无法确认完整性
    std::size_t returned_count = 0;        // 原始页条数，用于判断截断和停止条件
};

} // namespace orchestrator::modules::crawler::application::model
