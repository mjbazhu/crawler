#pragma once

#include "crawler/application/result/ServiceResult.h"

#include <array>
#include <cstdint>
#include <string>
#include <string_view>

namespace orchestrator::modules::crawler::infrastructure::music::migu::lyrics {

// MRC 64 位密文 adapter；密钥仅由组合根注入，实例创建后线程安全。
class MiguMrcCipher {
public:
    // 接受 ASCII 格式密钥，内部按协议扩展为 UTF-16LE 字块。
    explicit MiguMrcCipher(std::string_view key);
    // 仅执行十六进制、64 位变换及 UTF-16LE 解码，不解析歌词语法。
    application::result::ServiceResult<std::string> decrypt(std::string_view content) const;

private:
    std::array<std::uint64_t, 4> key_ {}; // 协议字块，不保存或输出原始密钥
    bool valid_key_ = false;              // 配置为空或包含非 ASCII 时拒绝解密
};

} // namespace orchestrator::modules::crawler::infrastructure::music::migu::lyrics
