#pragma once

#include <cstddef>
#include <string>

namespace orchestrator::modules::crawler::infrastructure::storage::rclone {

// Rclone RC 存储配置，所有敏感值只允许从环境变量或外部配置注入。
struct RcloneConfig {
    bool enabled = false;                         // 是否启用 rclone 远端复制
    std::string rc_url;                           // rclone RC 基础地址，例如 http://127.0.0.1:5572
    std::string rc_user;                          // rclone RC Basic Auth 用户名，可为空
    std::string rc_password;                      // rclone RC Basic Auth 密码，可为空
    std::string target_fs;                        // copyurl 目标 fs，例如 gcloud:Media
    int api_timeout_ms = 10000;                   // 单次 RC API 请求超时
    int job_timeout_ms = 600000;                  // 未传入资源超时时的 job 等待上限
    int job_poll_interval_ms = 2000;              // job/status 轮询间隔
    std::size_t api_max_response_bytes = 1048576; // RC API 响应体上限
};

} // namespace orchestrator::modules::crawler::infrastructure::storage::rclone
