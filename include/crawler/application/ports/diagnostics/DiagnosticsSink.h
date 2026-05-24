#pragma once

#include <string>
#include <string_view>

namespace orchestrator::modules::crawler::application::ports {

// 诊断日志级别，只表达业务诊断严重程度，不绑定具体日志库。
enum class DiagnosticsLevel {
    Debug,
    Info,
    Warn,
    Error
};

// application 诊断输出 port，用于隔离控制台和具体日志实现。
class DiagnosticsSink {
public:
    virtual ~DiagnosticsSink() = default;

    // 记录普通诊断信息，调用方负责传入脱敏后的上下文。
    virtual void log(DiagnosticsLevel level, std::string_view component, std::string_view message) = 0;

    // 记录结构化阶段事件；payload 应为脱敏 JSON object 字符串。
    virtual void event(DiagnosticsLevel level, std::string_view component, std::string_view event_type, std::string_view message, std::string_view payload);

    // 记录调试级诊断信息。
    void debug(std::string_view component, std::string_view message);

    // 记录信息级诊断信息。
    void info(std::string_view component, std::string_view message);

    // 记录警告级诊断信息。
    void warn(std::string_view component, std::string_view message);

    // 记录错误级诊断信息，调用方负责传入脱敏后的上下文。
    void error(std::string_view component, std::string_view message);
};

// 空诊断输出，用于没有外部诊断实现时保持业务流程静默。
class NullDiagnosticsSink : public DiagnosticsSink {
public:
    // 丢弃诊断信息。
    void log(DiagnosticsLevel level, std::string_view component, std::string_view message) override;
};

} // namespace orchestrator::modules::crawler::application::ports
