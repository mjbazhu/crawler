#pragma once

#include "crawler/application/ports/parsing/TimedLyricsDecoder.h"
#include "crawler/infrastructure/music/migu/lyrics/MiguMrcCipher.h"
#include "crawler/infrastructure/music/migu/lyrics/MiguMrcTextParser.h"

#include <string_view>

namespace orchestrator::modules::crawler::infrastructure::music::migu::lyrics {

// Migu 逐字歌词 adapter，只组合密文解码和平台语法解析；线程安全。
class MiguMrcLyricsDecoder final : public application::ports::TimedLyricsDecoder {
public:
    // 密钥从配置注入，不硬编码平台参数。
    explicit MiguMrcLyricsDecoder(std::string_view key);
    // 返回平台无关的领域值模型。
    application::result::ServiceResult<domain::TimedLyrics> decode(std::string_view content) const override;
    // 算法或语法调整时升级版本，使旧产物可以独立重新生成。
    std::string_view version() const override;

private:
    MiguMrcCipher cipher_;     // 64 位协议变换
    MiguMrcTextParser parser_; // 原生文本语法
};

} // namespace orchestrator::modules::crawler::infrastructure::music::migu::lyrics
