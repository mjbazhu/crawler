#pragma once

#include "crawler/application/model/SongResourceFile.h"

namespace orchestrator::modules::crawler::application::use_cases {

// 歌曲资源本地路径规划器，生成稳定且可回写的相对路径。
class SongResourcePathPlanner {
public:
    // 根据资源元数据生成存储相对路径。
    application::model::SongResourcePathResult plan(application::model::SongResourcePathRequest request) const;
};

} // namespace orchestrator::modules::crawler::application::use_cases
