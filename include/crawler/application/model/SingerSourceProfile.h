#pragma once

#include "crawler/application/model/SingerSourceMetadata.h"

#include <optional>
#include <string>

namespace orchestrator::modules::crawler::application::model {

// 歌手平台来源概览，保存平台声明的歌曲、专辑等聚合数量。
struct SingerSourceProfile {
    std::string source = "migu";                  // 音乐平台标识
    std::string source_singer_id;                 // 平台歌手 ID
    std::optional<int> source_song_total;         // 平台声明歌曲总数，空表示未知
    std::optional<int> source_album_total;        // 平台声明专辑总数，空表示未知
    std::optional<SingerSourceMetadata> metadata; // 同响应已有的歌手名称和头像，不推断缺失字段
};

} // namespace orchestrator::modules::crawler::application::model
