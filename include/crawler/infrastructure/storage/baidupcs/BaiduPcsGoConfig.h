#pragma once

#include <filesystem>
#include <string>

namespace orchestrator::modules::crawler::infrastructure::storage::baidupcs {

// BaiduPCS-Go 上传配置，敏感登录态由 BaiduPCS-Go 自身账号配置维护。
struct BaiduPcsGoConfig {
    bool enabled = false;                    // 是否启用 BaiduPCS-Go 云端上传
    std::string executable_path;             // BaiduPCS-Go 可执行文件路径
    std::filesystem::path local_root;        // 本地资源缓存根目录
    std::string cloud_root = "/Media";       // 百度网盘云端根目录，资源相对路径会追加在其后
    int upload_parallel = 8;                 // BaiduPCS-Go upload -p 单文件最大线程数
    int upload_load = 8;                     // BaiduPCS-Go upload -l 同时上传文件数
    int upload_retries = 3;                  // BaiduPCS-Go --retry 次数
    std::string upload_policy = "overwrite"; // 上传冲突策略；为空则不传 --policy
};

} // namespace orchestrator::modules::crawler::infrastructure::storage::baidupcs
