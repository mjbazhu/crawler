#include "crawler/application/ports/diagnostics/DiagnosticsSink.h"

#include <string>

namespace orchestrator::modules::crawler::application::ports {

void DiagnosticsSink::event(DiagnosticsLevel level, std::string_view component, std::string_view event_type, std::string_view message, std::string_view)
{
    log(level, component, std::string(event_type) + ": " + std::string(message));
}

void DiagnosticsSink::debug(std::string_view component, std::string_view message)
{
    log(DiagnosticsLevel::Debug, component, message);
}

void DiagnosticsSink::info(std::string_view component, std::string_view message)
{
    log(DiagnosticsLevel::Info, component, message);
}

void DiagnosticsSink::warn(std::string_view component, std::string_view message)
{
    log(DiagnosticsLevel::Warn, component, message);
}

void DiagnosticsSink::error(std::string_view component, std::string_view message)
{
    log(DiagnosticsLevel::Error, component, message);
}

void NullDiagnosticsSink::log(DiagnosticsLevel, std::string_view, std::string_view)
{}

} // namespace orchestrator::modules::crawler::application::ports
