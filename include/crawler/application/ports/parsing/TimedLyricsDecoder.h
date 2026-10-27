#pragma once

#include "crawler/application/result/ServiceResult.h"
#include "crawler/domain/music/TimedLyrics.h"

#include <string_view>

namespace orchestrator::modules::crawler::application::ports {

// 平台歌词解码 port；实现必须可并发调用，且不得执行网络或存储操作。
class TimedLyricsDecoder {
public:
    // 经由抽象释放具体 decoder。
    virtual ~TimedLyricsDecoder() = default;
    // 解码一份完整来源内容，失败时保留稳定且不含密钥的错误码。
    virtual result::ServiceResult<domain::TimedLyrics> decode(std::string_view content) const = 0;
    // 标识算法及语法版本，用于产物失效判断；必须是安全路径片段。
    virtual std::string_view version() const = 0;
};

} // namespace orchestrator::modules::crawler::application::ports
