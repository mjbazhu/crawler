#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace orchestrator::modules::crawler::domain {

// 歌曲信息
struct Song {
    uint64_t song_id = 0;
    uint64_t singer_id = 0;
    uint64_t album_id = 0;
    std::string artwork;
    std::string title;
    std::string artist;
    std::string album;
    std::string lrc;
    uint64_t created_at = 0;
    uint64_t updated_at = 0;
};

// 歌曲列表
struct SongList {
    std::vector<Song> songs;
    int total = 0;
    bool isEnd = false;
};

} // namespace orchestrator::modules::crawler::domain
