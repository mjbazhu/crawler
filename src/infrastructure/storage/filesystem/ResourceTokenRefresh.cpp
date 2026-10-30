#include "crawler/infrastructure/storage/filesystem/ResourceTokenRefresh.h"

#include <algorithm>
#include <chrono>
#include <condition_variable>
#include <map>
#include <mutex>
#include <string>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::storage::filesystem {
namespace model = application::model;
namespace ports = application::ports;

class ResourceTokenRefresh::State {
public:
    // 只保留刷新进度，不存token/响应正文。
    struct Entry {
        bool initial = false;
        bool running = false;
        std::chrono::steady_clock::time_point success {};
    };
    std::mutex mutex;                     // 短时内存状态保护，网络在锁外
    std::condition_variable changed;      // 单项等待可被上下文中断
    std::map<std::string, Entry> entries; // 最多1024个操作，空闲旧条目可淘汰

    // first只用于批次初次强制刷新；force=true仍保留440013的显式重试。
    model::MiguPacmtokenRefreshResult run(const std::string &key, const std::shared_ptr<ports::MiguPacmtokenRefresher> &inner, const std::shared_ptr<ports::MiguPacmtokenStore> &store, const model::ResourceTransferContext &context, std::string token, bool force, int interval, bool first)
    {
        if (token.empty()) {
            const auto current = store->currentPacmtoken();
            if (!current || current->empty()) return {};
            token = *current;
        }
        const auto cached = [&] {
            model::MiguPacmtokenRefreshResult value;
            const auto current = store->currentPacmtoken();
            value.success = current && !current->empty();
            if (current) value.pacmtoken = *current;
            value.pacmtoken_length = value.pacmtoken.size();
            value.updated_at = store->updatedAt();
            return value;
        };
        for (;;) {
            if (context.stopped()) return {};
            std::unique_lock lock(mutex);
            if (!entries.contains(key) && entries.size() >= 1024) {
                const auto free = std::find_if(entries.begin(), entries.end(), [](const auto &item) { return !item.second.running; });
                if (free == entries.end()) return {};
                entries.erase(free);
            }
            auto &entry = entries[key];
            if (entry.running) {
                changed.wait_for(lock, std::chrono::milliseconds(25));
                continue;
            }
            const bool satisfied = first ? entry.initial : (!force && (interval <= 0 || (entry.success != std::chrono::steady_clock::time_point {} && std::chrono::steady_clock::now() - entry.success < std::chrono::seconds(interval))));
            if (satisfied) {
                lock.unlock();
                return cached();
            }
            entry.running = true;
            if (first) entry.initial = true;
            break;
        }
        model::MiguPacmtokenRefreshResult value;
        try {
            if (const auto current = store->currentPacmtoken(); current && !current->empty()) token = *current;
            value = inner->refreshPacmtoken(std::move(token));
            if (value.success) {
                if (value.pacmtoken.empty()) {
                    const auto current = store->currentPacmtoken();
                    if (current) value.pacmtoken = *current;
                }
                if (!value.updated_at) value.updated_at = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
                value.pacmtoken_length = value.pacmtoken.size();
                store->updatePacmtoken(value.pacmtoken, value.updated_at);
            }
        } catch (...) {
            value.error_code = "token_refresh_failed";
        }
        {
            std::lock_guard lock(mutex);
            auto &entry = entries.at(key);
            entry.running = false;
            if (value.success) entry.success = std::chrono::steady_clock::now();
        }
        changed.notify_all();
        return value;
    }
};

// 每项持有自己的HTTP截止，兄弟项只共享有界协调状态。
class ResourceBoundTokenRefresh final : public ports::MiguPacmtokenRefresher {
public:
    ResourceBoundTokenRefresh(std::shared_ptr<ResourceTokenRefresh::State> state, std::string key, std::shared_ptr<ports::MiguPacmtokenRefresher> inner, std::shared_ptr<ports::MiguPacmtokenStore> store, model::ResourceTransferContext context) : state_(std::move(state)), key_(std::move(key)), inner_(std::move(inner)), store_(std::move(store)), context_(std::move(context))
    {}
    model::MiguPacmtokenRefreshResult refreshPacmtoken(std::string token) override
    {
        return state_->run(key_, inner_, store_, context_, std::move(token), true, 0, true);
    }
    model::MiguPacmtokenRefreshResult refreshPacmtokenWithPolicy(std::string token, bool force, int interval) override
    {
        return state_->run(key_, inner_, store_, context_, std::move(token), force, interval, false);
    }

private:
    std::shared_ptr<ResourceTokenRefresh::State> state_;   // 协调器状态
    std::string key_;                                      // 操作身份，不含凭据
    std::shared_ptr<ports::MiguPacmtokenRefresher> inner_; // 本项有界HTTP
    std::shared_ptr<ports::MiguPacmtokenStore> store_;     // 既有线程安全存储
    model::ResourceTransferContext context_;               // 本项取消
};

ResourceTokenRefresh::ResourceTokenRefresh() : state_(std::make_shared<State>())
{}
std::shared_ptr<ports::MiguPacmtokenRefresher> ResourceTokenRefresh::bind(const model::ResourceTransferJob &job, std::shared_ptr<ports::MiguPacmtokenRefresher> inner, std::shared_ptr<ports::MiguPacmtokenStore> store, const model::ResourceTransferContext &context) const
{
    if (!inner || !store) return inner;
    const auto key = job.operation_id.empty() ? "task:" + std::to_string(job.task_id) : job.operation_id;
    return std::make_shared<ResourceBoundTokenRefresh>(state_, key, std::move(inner), std::move(store), context);
}
void ResourceTokenRefresh::initial(const std::shared_ptr<ports::MiguPacmtokenRefresher> &bound) const
{
    // token本身由bound持有的port取得；传空只用于首次刷新入口。
    if (bound) (void)bound->refreshPacmtoken({});
}
} // namespace orchestrator::modules::crawler::infrastructure::storage::filesystem
