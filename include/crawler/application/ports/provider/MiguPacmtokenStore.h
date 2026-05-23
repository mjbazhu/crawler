#pragma once

#include <cstdint>
#include <optional>
#include <string>

namespace orchestrator::modules::crawler::application::ports {

// Migu pacmtoken 运行期存储 port，线程安全由实现类保证。
class MiguPacmtokenStore {
public:
    virtual ~MiguPacmtokenStore() = default;

    // 返回当前运行期 pacmtoken；未设置时返回空。
    virtual std::optional<std::string> currentPacmtoken() const = 0;

    // 更新运行期 pacmtoken，不做持久化，调用方不得传入完整 Cookie。
    virtual void updatePacmtoken(std::string pacmtoken, std::uint64_t updated_at) = 0;

    // 返回最近一次更新时间；未设置时为 0。
    virtual std::uint64_t updatedAt() const = 0;
};

} // namespace orchestrator::modules::crawler::application::ports
