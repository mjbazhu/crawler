#pragma once

#include <fstream>
#include <mutex>
#include <string>
#include <string_view>

namespace orchestrator::modules::crawler::infrastructure::observability::logging {

enum class LogLevel {
    L_DEBUG,
    L_INFO,
    L_WARN,
    L_ERROR
};

// 日志输出格式。
class Logger {
public:
    // 运行日志配置，控制台固定文本输出，文本文件和 JSON 行文件可独立开启。
    struct Options {
        LogLevel level = LogLevel::L_INFO; // 最小输出级别
        bool console_enabled = true;        // 是否输出到 stdout/stderr
        bool text_file_enabled = false;     // 是否写入文本日志文件
        std::string text_file_path;         // 文本日志文件路径
        bool json_file_enabled = false;     // 是否写入 JSONL 日志文件
        std::string json_file_path;         // JSONL 日志文件路径
    };

    // 获取单例实例
    static Logger &instance();

    // 禁止拷贝和移动
    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;

    // 应用完整日志配置。
    void configure(Options options);

    // 设置日志级别
    void setLevel(LogLevel level);

    // 设置是否输出到控制台
    void setConsoleEnabled(bool enabled);

    // 核心日志方法
    void log(LogLevel level, const std::string &file, int line, const std::string &message);

    // 组件日志方法，不暴露源码文件路径。
    void log(LogLevel level, std::string_view component, std::string_view message);

    // 结构化事件日志方法，payload 必须是脱敏 JSON object 字符串。
    void logEvent(LogLevel level, std::string_view component, std::string_view event_type, std::string_view message, std::string_view payload);

private:
    Logger() = default;
    ~Logger();

    // 日志条目的内部渲染格式，配置层不再暴露旧单文件格式开关。
    enum class EntryFormat {
        Text,
        Json
    };

    std::string getTimestamp();
    std::string formatEntry(EntryFormat format, LogLevel level, std::string_view source, int line, std::string_view component, std::string_view event_type, std::string_view message, std::string_view payload);
    std::string levelToString(LogLevel level) const;
    bool openLogFile(std::ofstream &stream, std::string &stored_path, const std::string &filename);

    LogLevel currentLevel_ = LogLevel::L_INFO;
    bool console_enabled_ = true;
    std::mutex mutex_;
    std::ofstream textLogFile_;
    std::ofstream jsonLogFile_;
    std::string text_log_file_path_;
    std::string json_log_file_path_;
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
