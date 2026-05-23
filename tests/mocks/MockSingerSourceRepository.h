#pragma once

#include "crawler/application/ports/persistence/SingerSourceRepository.h"

#include <vector>

namespace orchestrator::modules::crawler::tests {

class MockSingerSourceRepository : public application::ports::SingerSourceRepository {
public:
    bool save_result = true;                                             // 预设保存结果
    std::vector<application::model::SingerSourceMetadata> saved_singers; // 最近一次保存的歌手来源

    bool saveBatch(const std::vector<application::model::SingerSourceMetadata> &singers) override
    {
        saved_singers = singers;
        return save_result;
    }
};

} // namespace orchestrator::modules::crawler::tests
