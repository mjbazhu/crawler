#include "crawler/application/use_cases/ResourceTransferBudget.h"

#include <array>
#include <condition_variable>
#include <mutex>
#include <stdexcept>
#include <utility>

namespace orchestrator::modules::crawler::application::use_cases {

class ResourceTransferBudget::State {
public:
    std::mutex mutex;                        // 只保护配额数据
    std::condition_variable changed;         // 释放或取消唤醒等待者
    int capacity = 0;                        // 固定总上限
    int used = 0;                            // 当前占用
    int last = 2;                            // 下一次从 local 开始轮转
    std::array<int, 3> waiting {};           // 每端等待数
    std::array<int, 3> used_by_target {};    // 各端当前占用
    std::array<bool, 3> reserved_targets {}; // 正式运行给每个启用目标保留至少一个配额
    // 调用方持锁，选择下一个确有等待者的目标。
    int next() const
    {
        for (int offset = 1; offset <= 3; ++offset) {
            const int candidate = (last + offset) % 3;
            int reserved = 0;
            for (std::size_t other = 0; other < 3; ++other)
                if (other != static_cast<std::size_t>(candidate) && reserved_targets[other] && used_by_target[other] == 0) ++reserved;
            if (waiting[candidate] > 0 && used < capacity - reserved) return candidate;
        }
        return -1;
    }
};

ResourceTransferBudget::Lease::Lease(std::shared_ptr<State> state, std::size_t destination) : state_(std::move(state)), destination_(destination)
{}
ResourceTransferBudget::Lease::~Lease()
{
    {
        std::lock_guard lock(state_->mutex);
        --state_->used;
        --state_->used_by_target[destination_];
    }
    state_->changed.notify_all();
}
ResourceTransferBudget::ResourceTransferBudget(int capacity, std::array<bool, 3> reserved_targets) : state_(std::make_shared<State>())
{
    if (capacity < 1 || capacity > 96) throw std::invalid_argument("invalid resource transfer budget");
    state_->capacity = capacity;
    state_->reserved_targets = reserved_targets;
    int required = 0;
    for (const bool target : reserved_targets) required += target ? 1 : 0;
    if (capacity < required) throw std::invalid_argument("insufficient reserved resource budget");
}
std::unique_ptr<ResourceTransferBudget::Lease> ResourceTransferBudget::acquire(model::ResourceDestination destination, const model::ResourceTransferContext &context)
{
    if (model::resourceDestinationName(destination).empty()) return {};
    const auto index = static_cast<std::size_t>(destination);
    {
        std::lock_guard lock(state_->mutex);
        ++state_->waiting[index];
    }
    for (;;) {
        // 持久取消检查可能访问数据库，必须在临界区外执行。
        const bool stopped = context.stopped();
        std::unique_lock lock(state_->mutex);
        if (stopped) {
            --state_->waiting[index];
            lock.unlock();
            state_->changed.notify_all();
            return {};
        }
        int reserved = 0;
        for (std::size_t other = 0; other < 3; ++other)
            if (other != index && state_->reserved_targets[other] && state_->used_by_target[other] == 0) ++reserved;
        if (state_->used < state_->capacity - reserved && state_->next() == static_cast<int>(index)) {
            --state_->waiting[index];
            ++state_->used;
            ++state_->used_by_target[index];
            state_->last = static_cast<int>(index);
            lock.unlock();
            state_->changed.notify_all();
            return std::make_unique<Lease>(state_, index);
        }
        state_->changed.wait_for(lock, std::chrono::milliseconds(20));
    }
}

} // namespace orchestrator::modules::crawler::application::use_cases
