#pragma once

#include "crawler/application/ports/persistence/SongSourceRepository.h"

#include <vector>

namespace orchestrator::modules::crawler::tests {

class MockSongSourceRepository : public application::ports::SongSourceRepository {
public:
    std::vector<application::model::SongSourceMetadata> saved_songs; // 所有保存过的歌曲来源
    int save_calls = 0;                                              // 保存调用次数

    bool saveBatch(const std::vector<application::model::SongSourceMetadata> &songs) override
    {
        ++save_calls;
        saved_songs.insert(saved_songs.end(), songs.begin(), songs.end());
        return !songs.empty();
    }
};

} // namespace orchestrator::modules::crawler::tests
