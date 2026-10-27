#pragma once

#include "crawler/application/result/ServiceResult.h"
#include "crawler/domain/music/TimedLyrics.h"

#include <string_view>

namespace orchestrator::modules::crawler::infrastructure::music::migu::lyrics {

// 原生 MRC 文本语法 adapter；无状态且线程安全，不承担密文变换。
class MiguMrcTextParser {
public:
    // 严格解析行和词时间，保留重复标签、标点、空格及零时长署名行。
    application::result::ServiceResult<domain::TimedLyrics> parse(std::string_view content) const;
};

} // namespace orchestrator::modules::crawler::infrastructure::music::migu::lyrics
