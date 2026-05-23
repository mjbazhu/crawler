#pragma once

#include "crawler/application/ports/provider/MiguPacmtokenStore.h"

#include <cstdint>
#include <mutex>
#include <optional>
#include <string>

namespace orchestrator::modules::crawler::infrastructure::music::migu::provider {

// Migu pacmtoken 内存存储，线程安全；进程重启后由前端重新传入 token。
class MiguRuntimePacmtokenStore : public application::ports::MiguPacmtokenStore {
public:
    // 返回当前运行期 pacmtoken。
    std::optional<std::string> currentPacmtoken() const override;

    // 更新当前运行期 pacmtoken。
    void updatePacmtoken(std::string pacmtoken, std::uint64_t updated_at) override;

    // 返回最近一次更新时间。
    std::uint64_t updatedAt() const override;

private:
    mutable std::mutex mutex_;     // 保护 token 和更新时间
    std::string pacmtoken_;        // 当前运行期 pacmtoken
    std::uint64_t updated_at_ = 0; // 最近一次更新时间，Unix 秒
};

} // namespace orchestrator::modules::crawler::infrastructure::music::migu::provider
