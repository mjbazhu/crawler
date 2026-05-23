#pragma once

#include "crawler/application/model/SingerSourceMetadata.h"

#include <string_view>
#include <vector>

namespace orchestrator::modules::crawler::application::ports {

// 歌手来源 parser port，只解析可直接入库的平台歌手来源元数据。
class SingerSourceParser {
public:
    virtual ~SingerSourceParser() = default;

    // 解析平台歌手来源元数据，tab 用于补齐响应中缺失的地区和性别。
    virtual std::vector<application::model::SingerSourceMetadata> parseSingerSources(const std::vector<char> &raw_data, std::string_view tab) const = 0;
};

} // namespace orchestrator::modules::crawler::application::ports
