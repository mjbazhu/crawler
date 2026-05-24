#pragma once

#include "crawler/application/ports/diagnostics/DiagnosticsSink.h"

namespace orchestrator::modules::crawler::infrastructure::observability::logging {

// 控制台诊断输出 adapter，仅在 infrastructure 边界使用 std::cerr。
class ConsoleDiagnosticsSink : public application::ports::DiagnosticsSink {
public:
    // 输出普通诊断到运行日志。
    void log(application::ports::DiagnosticsLevel level, std::string_view component, std::string_view message) override;

    // 输出结构化事件到运行日志。
    void event(application::ports::DiagnosticsLevel level, std::string_view component, std::string_view event_type, std::string_view message, std::string_view payload) override;
};

} // namespace orchestrator::modules::crawler::infrastructure::observability::logging
