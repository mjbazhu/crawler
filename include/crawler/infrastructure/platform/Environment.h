#pragma once

#include <optional>
#include <string>
#include <string_view>

namespace orchestrator::modules::crawler::infrastructure::platform {

// 进程环境变量读取工具，支持从 .env 文件加载本地开发配置。
class Environment {
public:
    // 从 dotenv 文件加载键值到当前进程环境；文件不存在时返回 false。
    static bool loadDotEnvFile(std::string_view path, bool overwrite_existing = false);

    // 读取字符串环境变量；变量不存在或为空时返回 std::nullopt。
    static std::optional<std::string> getString(std::string_view key);

    // 读取字符串环境变量；变量不存在时返回 fallback。
    static std::string getStringOr(std::string_view key, std::string fallback);

    // 读取 int 环境变量；解析失败时返回 fallback。
    static int getIntOr(std::string_view key, int fallback);

    // 读取无符号短整型端口；解析失败或越界时返回 fallback。
    static unsigned short getPortOr(std::string_view key, unsigned short fallback);

    // 读取 bool 环境变量；支持 true/false、1/0、yes/no、on/off。
    static bool getBoolOr(std::string_view key, bool fallback);
};

} // namespace orchestrator::modules::crawler::infrastructure::platform
