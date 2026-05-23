#include "crawler/infrastructure/music/migu/provider/MiguRuntimePacmtokenStore.h"

#include <mutex>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::migu::provider {

std::optional<std::string> MiguRuntimePacmtokenStore::currentPacmtoken() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (pacmtoken_.empty()) {
        return std::nullopt;
    }
    return pacmtoken_;
}

void MiguRuntimePacmtokenStore::updatePacmtoken(std::string pacmtoken, std::uint64_t updated_at)
{
    std::lock_guard<std::mutex> lock(mutex_);
    pacmtoken_ = std::move(pacmtoken);
    updated_at_ = updated_at;
}

std::uint64_t MiguRuntimePacmtokenStore::updatedAt() const
{
    std::lock_guard<std::mutex> lock(mutex_);
    return updated_at_;
}

} // namespace orchestrator::modules::crawler::infrastructure::music::migu::provider
