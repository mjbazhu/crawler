#pragma once

#include "crawler/application/model/SingerSourceProfile.h"

#include <vector>

namespace orchestrator::modules::crawler::application::ports {

// 歌手概览 parser port，只负责从平台详情响应中解析声明总数。
class SingerProfileParser {
public:
    virtual ~SingerProfileParser() = default;

    // 解析歌手来源概览；解析失败时返回空字段对象。
    virtual application::model::SingerSourceProfile parseSingerSourceProfile(const std::vector<char> &raw_data) const = 0;
};

} // namespace orchestrator::modules::crawler::application::ports
