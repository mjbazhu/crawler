#pragma once

#include <string>
#include <string_view>

namespace orchestrator::modules::crawler::application::ports {

// application 诊断输出 port，用于隔离控制台和具体日志实现。
class DiagnosticsSink {
public:
    virtual ~DiagnosticsSink() = default;

    // 记录错误级诊断信息，调用方负责传入脱敏后的上下文。
    virtual void error(std::string_view component, std::string_view message) = 0;
};

// 空诊断输出，用于没有外部诊断实现时保持业务流程静默。
class NullDiagnosticsSink : public DiagnosticsSink {
public:
    // 丢弃错误信息。
    void error(std::string_view component, std::string_view message) override;
};

} // namespace orchestrator::modules::crawler::application::ports
