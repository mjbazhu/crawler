#include "crawler/application/ports/diagnostics/DiagnosticsSink.h"

namespace orchestrator::modules::crawler::application::ports {

void NullDiagnosticsSink::error(std::string_view, std::string_view)
{}

} // namespace orchestrator::modules::crawler::application::ports
