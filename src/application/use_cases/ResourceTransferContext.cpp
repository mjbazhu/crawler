#include "crawler/application/model/ResourceTransferExecution.h"

#include <algorithm>
#include <limits>

namespace orchestrator::modules::crawler::application::model {

int ResourceTransferContext::remaining() const
{
    const auto left = std::chrono::duration_cast<std::chrono::milliseconds>(deadline - std::chrono::steady_clock::now()).count();
    return static_cast<int>((std::clamp)(left, std::int64_t(0), std::int64_t((std::numeric_limits<int>::max)())));
}
bool ResourceTransferContext::stopped() const
{
    if (remaining() == 0) return true;
    try {
        return cancelled && cancelled();
    } catch (...) {
        return true;
    }
}

} // namespace orchestrator::modules::crawler::application::model
