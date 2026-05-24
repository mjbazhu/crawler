#include "crawler/infrastructure/observability/logging/Logger.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <system_error>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::observability::logging {

namespace {

// JSON 字符串转义，避免日志内容破坏 JSON 行格式。
std::string escapeJson(std::string_view value)
{
    std::string escaped;
    escaped.reserve(value.size() + 8);
    for (char ch : value) {
        switch (ch) {
        case '\\': escaped += "\\\\"; break;
        case '"': escaped += "\\\""; break;
        case '\b': escaped += "\\b"; break;
        case '\f': escaped += "\\f"; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default:
            if (static_cast<unsigned char>(ch) < 0x20) {
                std::ostringstream out;
                out << "\\u" << std::hex << std::setw(4) << std::setfill('0') << static_cast<int>(static_cast<unsigned char>(ch));
                escaped += out.str();
            } else {
                escaped.push_back(ch);
            }
            break;
        }
    }
    return escaped;
}

// 判断 payload 是否看起来是 JSON object/array，便于 JSON 日志保留结构。
bool looksLikeJsonContainer(std::string_view payload)
{
    auto begin = payload.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos) {
        return false;
    }
    auto end = payload.find_last_not_of(" \t\r\n");
    const char first = payload[begin];
    const char last = payload[end];
    return (first == '{' && last == '}') || (first == '[' && last == ']');
}

// 文本日志字段值转义，保持单行可读，不输出 JSON 片段。
std::string quoteTextValue(std::string value)
{
    if (value.empty()) {
        return "\"\"";
    }
    const bool needs_quote = std::any_of(value.begin(), value.end(), [](unsigned char ch) {
        return std::isspace(ch) != 0 || ch == '"' || ch == '\\' || ch == '=';
    });
    if (!needs_quote) {
        return value;
    }

    std::string quoted;
    quoted.reserve(value.size() + 2);
    quoted.push_back('"');
    for (char ch : value) {
        switch (ch) {
        case '\\': quoted += "\\\\"; break;
        case '"': quoted += "\\\""; break;
        case '\n': quoted += "\\n"; break;
        case '\r': quoted += "\\r"; break;
        case '\t': quoted += "\\t"; break;
        default: quoted.push_back(ch); break;
        }
    }
    quoted.push_back('"');
    return quoted;
}

// 将 JSON 标量转换为文本日志字段值。
std::string jsonScalarToText(const nlohmann::json &value)
{
    if (value.is_string()) {
        return quoteTextValue(value.get<std::string>());
    }
    if (value.is_boolean()) {
        return value.get<bool>() ? "true" : "false";
    }
    if (value.is_number_unsigned()) {
        return std::to_string(value.get<unsigned long long>());
    }
    if (value.is_number_integer()) {
        return std::to_string(value.get<long long>());
    }
    if (value.is_number_float()) {
        std::ostringstream out;
        out << value.get<double>();
        return out.str();
    }
    if (value.is_null()) {
        return "null";
    }
    return {};
}

// 数组只摘要标量元素，避免文本日志重新混入 JSON 对象。
std::string jsonArrayToText(const nlohmann::json &value)
{
    if (value.empty()) {
        return {};
    }

    std::vector<std::string> parts;
    for (const auto &item : value) {
        if (!item.is_primitive()) {
            continue;
        }
        parts.push_back(jsonScalarToText(item));
    }
    std::ostringstream out;
    out << '[';
    for (std::size_t index = 0; index < parts.size(); ++index) {
        if (index > 0) {
            out << ',';
        }
        out << parts[index];
    }
    out << ']';
    return out.str();
}

// 文本日志已经有 event= 字段时，去掉 message 中重复的事件名前缀。
std::string textMessageWithoutRepeatedEvent(std::string_view message, std::string_view event_type)
{
    if (event_type.empty() || message.empty()) {
        return std::string(message);
    }
    if (message == event_type) {
        return {};
    }
    if (message.size() > event_type.size() && message.substr(0, event_type.size()) == event_type && std::isspace(static_cast<unsigned char>(message[event_type.size()])) != 0) {
        std::size_t begin = event_type.size();
        while (begin < message.size() && std::isspace(static_cast<unsigned char>(message[begin])) != 0) {
            ++begin;
        }
        return std::string(message.substr(begin));
    }
    return std::string(message);
}

// 文本格式只展开顶层标量字段；完整结构留给 JSON 日志。
std::string formatTextPayloadFields(std::string_view payload)
{
    if (!looksLikeJsonContainer(payload)) {
        return payload.empty() ? std::string {} : "payload=" + quoteTextValue(std::string(payload));
    }

    const auto parsed = nlohmann::json::parse(payload, nullptr, false);
    if (parsed.is_discarded()) {
        return "payload=" + quoteTextValue(std::string(payload));
    }
    if (!parsed.is_object()) {
        return {};
    }

    std::ostringstream out;
    bool first = true;
    for (auto it = parsed.begin(); it != parsed.end(); ++it) {
        const auto &value = it.value();
        std::string rendered;
        if (value.is_primitive()) {
            if (value.is_string() && value.get<std::string>().empty()) {
                continue;
            }
            rendered = jsonScalarToText(value);
        } else if (value.is_array()) {
            rendered = jsonArrayToText(value);
        }
        if (rendered.empty()) {
            continue;
        }
        if (!first) {
            out << ' ';
        }
        first = false;
        out << it.key() << '=' << rendered;
    }
    return out.str();
}

} // namespace

Logger &Logger::instance()
{
    static Logger instance;
    return instance;
}

Logger::~Logger()
{
    if (textLogFile_.is_open()) {
        textLogFile_.close();
    }
    if (jsonLogFile_.is_open()) {
        jsonLogFile_.close();
    }
}

void Logger::configure(Options options)
{
    std::lock_guard<std::mutex> lock(mutex_);
    currentLevel_ = options.level;
    console_enabled_ = options.console_enabled;

    if (textLogFile_.is_open()) {
        textLogFile_.close();
    }
    if (jsonLogFile_.is_open()) {
        jsonLogFile_.close();
    }
    text_log_file_path_.clear();
    json_log_file_path_.clear();

    if (options.text_file_enabled) {
        const std::string path = options.text_file_path.empty() ? "logs/crawler.log" : options.text_file_path;
        (void)openLogFile(textLogFile_, text_log_file_path_, path);
    }
    if (options.json_file_enabled) {
        const std::string path = options.json_file_path.empty() ? "logs/crawler.jsonl" : options.json_file_path;
        (void)openLogFile(jsonLogFile_, json_log_file_path_, path);
    }
}

void Logger::setLevel(LogLevel level)
{
    std::lock_guard<std::mutex> lock(mutex_);
    currentLevel_ = level;
}

void Logger::setConsoleEnabled(bool enabled)
{
    std::lock_guard<std::mutex> lock(mutex_);
    console_enabled_ = enabled;
}

void Logger::log(LogLevel level, const std::string &file, int line, const std::string &message)
{
    // 提取文件名（去除路径）
    std::filesystem::path p(file);
    std::string filename = p.filename().string();

    std::lock_guard<std::mutex> lock(mutex_);
    if (level < currentLevel_) {
        return;
    }
    std::string textEntry;
    if (console_enabled_ || textLogFile_.is_open()) {
        textEntry = formatEntry(EntryFormat::Text, level, filename, line, {}, {}, message, {});
    }
    if (console_enabled_) {
        if (level == LogLevel::L_ERROR) {
            std::cerr << textEntry;
            std::cerr.flush();
        } else {
            std::cout << textEntry;
            std::cout.flush();
        }
    }
    if (textLogFile_.is_open()) {
        textLogFile_ << textEntry;
        textLogFile_.flush();
    }
    if (jsonLogFile_.is_open()) {
        jsonLogFile_ << formatEntry(EntryFormat::Json, level, filename, line, {}, {}, message, {});
        jsonLogFile_.flush();
    }
}

void Logger::log(LogLevel level, std::string_view component, std::string_view message)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (level < currentLevel_) {
        return;
    }
    std::string textEntry;
    if (console_enabled_ || textLogFile_.is_open()) {
        textEntry = formatEntry(EntryFormat::Text, level, {}, 0, component, {}, message, {});
    }
    if (console_enabled_) {
        if (level == LogLevel::L_ERROR) {
            std::cerr << textEntry;
            std::cerr.flush();
        } else {
            std::cout << textEntry;
            std::cout.flush();
        }
    }
    if (textLogFile_.is_open()) {
        textLogFile_ << textEntry;
        textLogFile_.flush();
    }
    if (jsonLogFile_.is_open()) {
        jsonLogFile_ << formatEntry(EntryFormat::Json, level, {}, 0, component, {}, message, {});
        jsonLogFile_.flush();
    }
}

void Logger::logEvent(LogLevel level, std::string_view component, std::string_view event_type, std::string_view message, std::string_view payload)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (level < currentLevel_) {
        return;
    }
    std::string textEntry;
    if (console_enabled_ || textLogFile_.is_open()) {
        textEntry = formatEntry(EntryFormat::Text, level, {}, 0, component, event_type, message, payload);
    }
    if (console_enabled_) {
        if (level == LogLevel::L_ERROR) {
            std::cerr << textEntry;
            std::cerr.flush();
        } else {
            std::cout << textEntry;
            std::cout.flush();
        }
    }
    if (textLogFile_.is_open()) {
        textLogFile_ << textEntry;
        textLogFile_.flush();
    }
    if (jsonLogFile_.is_open()) {
        jsonLogFile_ << formatEntry(EntryFormat::Json, level, {}, 0, component, event_type, message, payload);
        jsonLogFile_.flush();
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
    }
    return ss.str();
}

std::string Logger::formatEntry(EntryFormat format, LogLevel level, std::string_view source, int line, std::string_view component, std::string_view event_type, std::string_view message, std::string_view payload)
{
    const std::string timestamp = getTimestamp();
    const std::string levelStr = levelToString(level);
    std::ostringstream ss;

    if (format == EntryFormat::Json) {
        ss << "{\"timestamp\":\"" << escapeJson(timestamp) << "\""
           << ",\"level\":\"" << escapeJson(levelStr) << "\"";
        if (!component.empty()) {
            ss << ",\"component\":\"" << escapeJson(component) << "\"";
        }
        if (!source.empty()) {
            ss << ",\"source\":\"" << escapeJson(source) << "\"";
        }
        if (line > 0) {
            ss << ",\"line\":" << line;
        }
        if (!event_type.empty()) {
            ss << ",\"event_type\":\"" << escapeJson(event_type) << "\"";
        }
        ss << ",\"message\":\"" << escapeJson(message) << "\"";
        if (!payload.empty()) {
            ss << ",\"payload\":";
            if (looksLikeJsonContainer(payload)) {
                ss << payload;
            } else {
                ss << "\"" << escapeJson(payload) << "\"";
            }
        }
        ss << "}\n";
        return ss.str();
    }

    ss << "[" << timestamp << "] [" << levelStr << "] ";
    if (!component.empty()) {
        ss << "[" << component << "] ";
    } else if (!source.empty()) {
        ss << "[" << source;
        if (line > 0) {
            ss << ":" << line;
        }
        ss << "] ";
    }
    if (!event_type.empty()) {
        ss << "event=" << event_type << " ";
    }
    const std::string text_message = textMessageWithoutRepeatedEvent(message, event_type);
    ss << text_message;
    if (!payload.empty()) {
        const std::string fields = formatTextPayloadFields(payload);
        if (!fields.empty()) {
            if (!text_message.empty()) {
                ss << ' ';
            }
            ss << fields;
        }
    }
    ss << '\n';
    return ss.str();
}

bool Logger::openLogFile(std::ofstream &stream, std::string &stored_path, const std::string &filename)
{
    std::filesystem::path path(filename);
    if (path.has_parent_path()) {
        std::error_code ec;
        std::filesystem::create_directories(path.parent_path(), ec);
        if (ec && console_enabled_) {
            std::cerr << "[Logger] Failed to create log directory: " << path.parent_path().string() << " Error: " << ec.message() << std::endl;
        }
    }
    stream.open(path, std::ios::app);
    if (stream.is_open()) {
        stored_path = path.string();
        return true;
    }
    if (console_enabled_) {
        std::cerr << "[Logger] Failed to open log file: " << path.string() << std::endl;
    }
    return false;
}

std::string Logger::levelToString(LogLevel level) const
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
