#pragma once

#include "crawler/application/model/SongResourceFetch.h"

#include <vector>

namespace orchestrator::modules::crawler::application::ports {

// 歌曲资源响应 parser port，只负责从资源接口原始响应提取下载入口。
class SongResourceParser {
public:
    virtual ~SongResourceParser() = default;

    // 解析歌曲资源接口响应；失败时返回 ok=false 并尽量保留响应码和说明。
    virtual application::model::SongResourceFetchResult parseSongResource(const std::vector<char> &raw_data) const = 0;
};

} // namespace orchestrator::modules::crawler::application::ports
