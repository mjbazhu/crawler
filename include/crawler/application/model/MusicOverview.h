#pragma once

#include <cstdint>
#include <map>
#include <string>

namespace orchestrator::modules::crawler::application::model {

// 音乐采集控制台总览聚合结果。
struct MusicOverview {
    std::uint64_t singers = 0;             // 公共歌手数量
    std::uint64_t singer_sources = 0;      // 平台歌手来源数量
    std::uint64_t songs = 0;               // 公共歌曲数量
    std::uint64_t song_sources = 0;        // 平台歌曲来源数量
    std::uint64_t albums = 0;              // 公共专辑数量
    std::uint64_t album_sources = 0;       // 平台专辑来源数量
    std::map<int, std::uint64_t> singer_song_sync;  // 歌手歌曲同步状态分布
    std::map<int, std::uint64_t> singer_album_sync; // 歌手专辑同步状态分布
    std::map<int, std::uint64_t> album_detail_sync; // 专辑详情同步状态分布
    std::map<int, std::uint64_t> album_song_sync;   // 专辑歌曲同步状态分布
    std::map<int, std::uint64_t> resource_status;   // 资源状态分布
};

} // namespace orchestrator::modules::crawler::application::model
