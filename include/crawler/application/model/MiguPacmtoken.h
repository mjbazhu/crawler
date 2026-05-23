#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

namespace orchestrator::modules::crawler::application::model {

// Migu pacmtoken 刷新请求，token 由前端或运行期存储提供。
struct MiguPacmtokenRefreshRequest {
    std::string pacmtoken; // 当前 pacmtoken，允许为空；为空时尝试使用后端运行期存储
    bool store = true;     // 刷新成功后是否写入后端运行期存储
};

// Migu pacmtoken 刷新结果，token 只用于 API 返回和运行期内存，不写日志。
struct MiguPacmtokenRefreshResult {
    bool success = false;             // 刷新或校验是否成功
    bool refreshed = false;           // 上游是否返回了不同的新 token
    std::string pacmtoken;            // 可继续使用的 pacmtoken
    std::size_t pacmtoken_length = 0; // token 长度，便于前端脱敏展示
    int upstream_status = 0;          // checkPacMtoken 上游 HTTP 状态码
    std::string error_code;           // 稳定错误码
    std::string message;              // 脱敏结果说明
    std::uint64_t updated_at = 0;     // 运行期 token 更新时间，Unix 秒
};

// Migu pacmtoken 运行期状态，供前端脱敏轮询。
struct MiguPacmtokenState {
    bool configured = false;          // 后端是否装配了 pacmtoken 存储
    bool has_pacmtoken = false;       // 当前进程内是否已有 token
    std::size_t pacmtoken_length = 0; // 当前 token 长度
    std::uint64_t updated_at = 0;     // 最近更新时间，Unix 秒
    std::string pacmtoken;            // 仅 current 接口返回；state 接口保持为空
};

} // namespace orchestrator::modules::crawler::application::model
