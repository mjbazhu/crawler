#include "crawler/infrastructure/observability/logging/Logger.h"
#include <filesystem>

namespace orchestrator::modules::crawler::infrastructure::observability::logging {

Logger &Logger::instance()
{
    static Logger instance;
    return instance;
}

Logger::~Logger()
{
    if (logFile_.is_open()) {
        logFile_.close();
    }
}

void Logger::setLevel(LogLevel level)
{
    std::lock_guard<std::mutex> lock(mutex_);
    currentLevel_ = level;
}

void Logger::setLogFile(const std::string &filename)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (logFile_.is_open()) {
        logFile_.close();
    }
    logFile_.open(filename, std::ios::app);
}

void Logger::log(LogLevel level, const std::string &file, int line, const std::string &message)
{
    if (level < currentLevel_) {
        return;
    }

    // 提取文件名（去除路径）
    std::filesystem::path p(file);
    std::string filename = p.filename().string();

    std::lock_guard<std::mutex> lock(mutex_);

    std::string timestamp = getTimestamp();
    std::string levelStr = levelToString(level);

    // 格式: [时间] [级别] [文件:行号] 消息
    std::stringstream ss;
    ss << "[" << timestamp << "] "
       << "[" << levelStr << "] "
       << "[" << filename << ":" << line << "] "
       << message << std::endl;

    std::string logEntry = ss.str();

    // 输出到控制台
    if (level == LogLevel::L_ERROR) {
        std::cerr << logEntry;
    } else {
        std::cout << logEntry;
    }

    // 输出到文件
    if (logFile_.is_open()) {
        logFile_ << logEntry;
        logFile_.flush(); // 确保实时写入
    }
}

std::string Logger::getTimestamp()
{
    struct tm localTime;
    std::time_t now = std::time(nullptr);

    std::stringstream ss;
    // Windows (MSVC) 下：第一个参数是结果指针，返回值是错误码
    if (localtime_s(&localTime, &now) == 0) {
        // 直接使用 &localTime
        ss << std::put_time(&localTime, "%Y-%m-%d %H:%M:%S");

        std::cout << "当前时间: " << ss.str() << std::endl;
    }
    return ss.str();
}

std::string Logger::levelToString(LogLevel level)
{
    switch (level) {
    case LogLevel::L_DEBUG: return "DEBUG";
    case LogLevel::L_INFO: return "INFO";
    case LogLevel::L_WARN: return "WARN";
    case LogLevel::L_ERROR: return "ERROR";
    default: return "UNKNOWN";
    }
}

} // namespace orchestrator::modules::crawler::infrastructure::observability::logging
