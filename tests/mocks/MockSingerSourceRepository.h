#pragma once

#include "crawler/application/ports/persistence/SingerSourceRepository.h"

#include <vector>

namespace orchestrator::modules::crawler::tests {

class MockSingerSourceRepository : public application::ports::SingerSourceRepository {
public:
    bool save_result = true;                                             // 预设保存结果
    bool profile_result = true;                                          // 预设概览更新结果
    bool pinyin_result = true;                                           // 预设拼音回填结果
    std::vector<application::model::SingerSourceMetadata> saved_singers; // 最近一次保存的歌手来源
    application::model::SingerSourceProfile saved_profile;               // 最近一次更新的歌手概览
    std::vector<application::model::SingerSourceNamePinyinPatch> saved_pinyin_patches; // 最近一次拼音回填参数

    bool saveBatch(const std::vector<application::model::SingerSourceMetadata> &singers) override
    {
        saved_singers = singers;
        return save_result;
    }

    bool updateProfileCounts(const application::model::SingerSourceProfile &profile) override
    {
        saved_profile = profile;
        return profile_result;
    }

    bool updateNamePinyinBatch(const std::vector<application::model::SingerSourceNamePinyinPatch> &patches) override
    {
        saved_pinyin_patches = patches;
        return pinyin_result;
    }
};

} // namespace orchestrator::modules::crawler::tests
