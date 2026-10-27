#pragma once

#include "crawler/application/ports/diagnostics/DiagnosticsSink.h"

#include <iostream>
#include <string_view>

namespace orchestrator::modules::crawler::tests::migu::support {

// 串行集成测试保留持久化失败原因，不让 bool 断言掩盖实际 SQL 诊断。
class MetadataTestDiagnostics final : public application::ports::DiagnosticsSink {
public:
    // 只输出调用方已经脱敏的错误；不读取连接配置或凭据。
    void log(application::ports::DiagnosticsLevel level, std::string_view component, std::string_view message) override
    {
        if (level == application::ports::DiagnosticsLevel::Error) std::cerr << component << ": " << message << '\n';
    }
};

} // namespace orchestrator::modules::crawler::tests::migu::support
