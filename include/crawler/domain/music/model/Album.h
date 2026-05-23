#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "crawler/domain/music/model/Song.h"

namespace orchestrator::modules::crawler::domain {

// 专辑信息
struct Album {
    // 传入的参数
    std::string title;
    uint64_t album_id = 0;
    std::string artwork;
    std::string artist;
    uint64_t singer_id = 0;
    std::string album_infos;
    uint64_t listen_num = 0;

    std::vector<Song> vaild_songs;
    // 返回的参数
    int type = 0;

    uint64_t created_at = 0;
    uint64_t updated_at = 0;
};

//  专辑列表
struct AlbumList {
    std::vector<Album> albums;
    int total = 0;
    bool isEnd = false;
};

} // namespace orchestrator::modules::crawler::domain
