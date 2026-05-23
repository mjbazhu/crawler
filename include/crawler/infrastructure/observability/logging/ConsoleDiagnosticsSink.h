#pragma once

#include "crawler/application/ports/diagnostics/DiagnosticsSink.h"

namespace orchestrator::modules::crawler::infrastructure::observability::logging {

// 控制台诊断输出 adapter，仅在 infrastructure 边界使用 std::cerr。
class ConsoleDiagnosticsSink : public application::ports::DiagnosticsSink {
public:
    // 输出错误诊断到标准错误流。
    void error(std::string_view component, std::string_view message) override;
};

} // namespace orchestrator::modules::crawler::infrastructure::observability::logging
