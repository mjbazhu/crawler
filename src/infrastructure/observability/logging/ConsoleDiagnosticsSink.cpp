#include "crawler/infrastructure/observability/logging/ConsoleDiagnosticsSink.h"

#include "crawler/infrastructure/observability/logging/Logger.h"

namespace orchestrator::modules::crawler::infrastructure::observability::logging {

namespace {

// 将 application 诊断级别映射到基础设施 logger 级别。
LogLevel toLogLevel(application::ports::DiagnosticsLevel level)
{
    switch (level) {
    case application::ports::DiagnosticsLevel::Debug: return LogLevel::L_DEBUG;
    case application::ports::DiagnosticsLevel::Info: return LogLevel::L_INFO;
    case application::ports::DiagnosticsLevel::Warn: return LogLevel::L_WARN;
    case application::ports::DiagnosticsLevel::Error: return LogLevel::L_ERROR;
    }
    return LogLevel::L_INFO;
}

} // namespace

void ConsoleDiagnosticsSink::log(application::ports::DiagnosticsLevel level, std::string_view component, std::string_view message)
{
    Logger::instance().log(toLogLevel(level), component, message);
}

void ConsoleDiagnosticsSink::event(application::ports::DiagnosticsLevel level, std::string_view component, std::string_view event_type, std::string_view message, std::string_view payload)
{
    Logger::instance().logEvent(toLogLevel(level), component, event_type, message, payload);
}

} // namespace orchestrator::modules::crawler::infrastructure::observability::logging
