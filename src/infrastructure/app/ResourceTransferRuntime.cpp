#include "crawler/infrastructure/app/ResourceTransferRuntime.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <condition_variable>
#include <mutex>
#include <set>
#include <stdexcept>
#include <thread>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::app {
namespace model = application::model;
namespace result = application::result;

class ResourceTransferRuntime::State {
public:
    mutable std::mutex mutex;                                   // 只保护配置与线程生命周期，不持锁执行 worker
    std::condition_variable changed;                            // 空闲、退出和 shutdown 唤醒
    bool parallel = false;                                      // 模式不可热切换
    bool started = false;                                       // 防止重复启动
    bool stopping = false;                                      // 停止新认领
    std::atomic_bool cancelled {false};                         // 宽限到期后的在途协作取消
    int grace_ms = 0;                                           // 固定关闭预算
    std::size_t alive = 0;                                      // 尚未退出的 worker
    std::vector<model::ResourceTransferTargetProfile> profiles; // 新受理时读取，不改变已冻结任务
    std::vector<ResourceRuntimeLane> lanes;                     // 持有全部传输依赖
    std::vector<std::thread> threads;                           // 生命周期句柄，超时后安全转交共享状态
};

namespace {
// 迟到任务保留持久认领，由既有 recover/repair 核对，不清空旧意图。
void loop(const std::shared_ptr<ResourceTransferRuntime::State> &state, ResourceRuntimeLane lane)
{
    for (;;) {
        {
            std::lock_guard lock(state->mutex);
            if (state->stopping) break;
        }
        bool handled = false;
        try {
            handled = lane.run_one([state] { return state->cancelled.load(); });
        } catch (...) {} // worker 的持久状态留给恢复，不记录可能携带凭据的异常正文
        if (!handled) {
            std::unique_lock lock(state->mutex);
            state->changed.wait_for(lock, std::chrono::milliseconds(200), [&] { return state->stopping; });
        }
    }
    {
        std::lock_guard lock(state->mutex);
        --state->alive;
    }
    state->changed.notify_all();
}

// 不在控制锁内 join；detach 的线程仅引用共享状态，不能访问已析构外观。
bool stop(const std::shared_ptr<ResourceTransferRuntime::State> &state, int grace)
{
    std::vector<std::thread> threads;
    bool drained = false;
    {
        std::unique_lock lock(state->mutex);
        if (state->stopping) grace = 0;
        state->stopping = true;
        state->changed.notify_all();
        drained = state->changed.wait_for(lock, std::chrono::milliseconds(grace), [&] { return state->alive == 0; });
        if (!drained) state->cancelled.store(true);
        threads.swap(state->threads);
    }
    state->changed.notify_all();
    for (auto &thread : threads) {
        if (drained)
            thread.join();
        else
            thread.detach();
    }
    return drained;
}
} // namespace

ResourceTransferRuntime::ResourceTransferRuntime(bool parallel, std::vector<model::ResourceTransferTargetProfile> profiles, std::vector<ResourceRuntimeLane> lanes, int shutdown_grace_ms) : state_(std::make_shared<State>())
{
    if (shutdown_grace_ms < 0 || shutdown_grace_ms > 600000) throw std::invalid_argument("invalid resource shutdown budget");
    std::set<std::pair<model::ResourceDestination, std::string>> selected;
    for (const auto &lane : lanes)
        if (!lane.run_one || lane.concurrency < 1 || lane.concurrency > 32 || (lane.kind != "transfer" && lane.kind != "producer") || !selected.emplace(lane.destination, lane.kind).second) throw std::invalid_argument("invalid resource runtime lane");
    state_->parallel = parallel;
    state_->profiles = std::move(profiles);
    state_->lanes = std::move(lanes);
    state_->grace_ms = shutdown_grace_ms;
}

ResourceTransferRuntime::~ResourceTransferRuntime()
{
    (void)stop(state_, 0);
}

void ResourceTransferRuntime::start()
{
    std::lock_guard lock(state_->mutex);
    if (!state_->parallel || state_->started || state_->stopping) return;
    state_->started = true;
    try {
        for (const auto &lane : state_->lanes)
            for (int index = 0; index < lane.concurrency; ++index) {
                ++state_->alive;
                try {
                    state_->threads.emplace_back([state = state_, lane] { loop(state, lane); });
                } catch (...) {
                    --state_->alive;
                    throw;
                }
            }
    } catch (...) {
        state_->stopping = true;
        state_->cancelled.store(true);
        state_->changed.notify_all();
        throw;
    }
}

void ResourceTransferRuntime::addObserver(std::function<bool()> poll)
{
    if (!poll) throw std::invalid_argument("resource observer required");
    std::lock_guard lock(state_->mutex);
    if (state_->started || state_->stopping) throw std::logic_error("resource observer must be configured before start");
    state_->lanes.push_back({model::ResourceDestination::Local, 1, [poll = std::move(poll)](auto stopped) { if (!stopped()) (void)poll(); return false; }, "projection"});
}

bool ResourceTransferRuntime::shutdown()
{
    return stop(state_, state_->grace_ms);
}

bool ResourceTransferRuntime::parallelEnabled() const
{
    std::lock_guard lock(state_->mutex);
    return state_->parallel && !state_->stopping;
}

std::vector<model::ResourceTransferTargetProfile> ResourceTransferRuntime::profiles() const
{
    std::lock_guard lock(state_->mutex);
    return state_->profiles;
}

result::ServiceResult<bool> ResourceTransferRuntime::updatePolicy(model::ResourceDestination destination, model::ResourceTransferJobPolicy policy)
{
    using Result = result::ServiceResult<bool>;
    if (policy.timeout_ms < 100 || policy.timeout_ms > 600000 || policy.retries < 0 || policy.retries > 5 || policy.max_bytes == 0 || policy.max_bytes > 4ULL * 1024 * 1024 * 1024 || policy.bytes_per_second > (1ULL << 40)) return Result::failure(result::ServiceErrorType::InvalidParam, "invalid_transfer_policy");
    std::lock_guard lock(state_->mutex);
    if (!state_->parallel || state_->stopping) return Result::failure(result::ServiceErrorType::ExternalApiError, "parallel_resource_runtime_unavailable");
    const auto target = std::find_if(state_->profiles.begin(), state_->profiles.end(), [&](const auto &profile) { return profile.destination == destination; });
    if (target == state_->profiles.end()) return Result::failure(result::ServiceErrorType::NotFound, "resource_destination_disabled");
    if (target->adapter == "baidupcs" && policy.bytes_per_second) return Result::failure(result::ServiceErrorType::InvalidParam, "baidu_task_rate_unsupported");
    policy.strict_sha256 = target->policy.strict_sha256;
    target->policy = policy;
    return Result::success(true);
}

} // namespace orchestrator::modules::crawler::infrastructure::app
