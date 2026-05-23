#include "crawler/infrastructure/observability/logging/ConsoleDiagnosticsSink.h"

#include <iostream>

namespace orchestrator::modules::crawler::infrastructure::observability::logging {

void ConsoleDiagnosticsSink::error(std::string_view component, std::string_view message)
{
    std::cerr << "[" << component << "] " << message << std::endl;
}

} // namespace orchestrator::modules::crawler::infrastructure::observability::logging
