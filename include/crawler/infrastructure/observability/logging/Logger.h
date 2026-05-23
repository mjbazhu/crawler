#pragma once

#include <string>
#include <mutex>
#include <iostream>
#include <fstream>
#include <sstream>
#include <ctime>
#include <iomanip>

namespace orchestrator::modules::crawler::infrastructure::observability::logging {

enum class LogLevel {
    L_DEBUG,
    L_INFO,
    L_WARN,
    L_ERROR
};

class Logger {
public:
    // 获取单例实例
    static Logger &instance();

    // 禁止拷贝和移动
    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;

    // 设置日志级别
    void setLevel(LogLevel level);

    // 设置输出文件（若不设置则默认输出到控制台）
    void setLogFile(const std::string &filename);

    // 核心日志方法
    void log(LogLevel level, const std::string &file, int line, const std::string &message);

private:
    Logger() = default;
    ~Logger();

    std::string getTimestamp();
    std::string levelToString(LogLevel level);

    LogLevel currentLevel_ = LogLevel::L_INFO;
    std::mutex mutex_;
    std::ofstream logFile_;
};

} // namespace orchestrator::modules::crawler::infrastructure::observability::logging

// 便捷宏定义
#define LOG_DEBUG(msg) \
    ::orchestrator::modules::crawler::infrastructure::observability::logging::Logger::instance().log(::orchestrator::modules::crawler::infrastructure::observability::logging::LogLevel::L_DEBUG, __FILE__, __LINE__, msg)

#define LOG_INFO(msg) \
    ::orchestrator::modules::crawler::infrastructure::observability::logging::Logger::instance().log(::orchestrator::modules::crawler::infrastructure::observability::logging::LogLevel::L_INFO, __FILE__, __LINE__, msg)

#define LOG_WARN(msg) \
    ::orchestrator::modules::crawler::infrastructure::observability::logging::Logger::instance().log(::orchestrator::modules::crawler::infrastructure::observability::logging::LogLevel::L_WARN, __FILE__, __LINE__, msg)

#define LOG_ERROR(msg) \
    ::orchestrator::modules::crawler::infrastructure::observability::logging::Logger::instance().log(::orchestrator::modules::crawler::infrastructure::observability::logging::LogLevel::L_ERROR, __FILE__, __LINE__, msg)

// 支持流式输入的辅助宏 (例如 LOG_INFO_S << "Value: " << 123;)
#define LOG_STREAM(level)                    \
    if (true) {                              \
    } else                                   \
        (void)0; /* 占位符，防止悬挂 else */ \
    std::stringstream ss;                    \
    ss

// 注意：流式宏的完整实现需要更复杂的临时对象生命周期管理，
// 为保持简洁，当前阶段推荐使用 std::to_string 或 fmt 库配合上述基础宏。
