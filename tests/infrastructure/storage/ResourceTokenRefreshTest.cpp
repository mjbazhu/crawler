#include "crawler/infrastructure/storage/filesystem/ResourceTokenRefresh.h"
#include "infrastructure/storage/support/CloudTransferTestSupport.h"

#include <atomic>
#include <future>
#include <iostream>
#include <mutex>
#include <thread>

namespace c = orchestrator::modules::crawler;
namespace model = c::application::model;
namespace ports = c::application::ports;
namespace files = c::infrastructure::storage::filesystem;
namespace test = c::tests::cloud_transfer;
namespace {
// fixture token只在内存，永不写日志或文件。
class Store final : public ports::MiguPacmtokenStore {
public:
    std::optional<std::string> currentPacmtoken() const override
    {
        std::lock_guard lock(mutex_);
        return token_;
    }
    void updatePacmtoken(std::string token, std::uint64_t time) override
    {
        std::lock_guard lock(mutex_);
        token_ = std::move(token);
        time_ = time;
    }
    std::uint64_t updatedAt() const override
    {
        std::lock_guard lock(mutex_);
        return time_;
    }

private:
    mutable std::mutex mutex_;                          // fixture并发保护
    std::optional<std::string> token_ = "fixture-only"; // 无效fixture，不是外部登录态
    std::uint64_t time_ = 1;                            // 固定初始时间
};
// 可阻塞一次刷新，证明取消等待者不会中止持有者。
class Refresher final : public ports::MiguPacmtokenRefresher {
public:
    std::atomic_int calls {0};    // 真实调用次数
    std::function<void()> during; // 测试屏障，装配后不修改
    model::MiguPacmtokenRefreshResult refreshPacmtoken(std::string token) override
    {
        ++calls;
        test::require(!token.empty(), "initial refresh lost current token");
        if (during) during();
        model::MiguPacmtokenRefreshResult value;
        value.success = true;
        value.pacmtoken = std::move(token);
        value.updated_at = 2;
        return value;
    }
};
} // namespace

int main()
{
    try {
        auto store = std::make_shared<Store>();
        auto inner = std::make_shared<Refresher>();
        auto job = test::job(model::ResourceDestination::Local);
        job.operation_id = "fixture_operation";
        files::ResourceTokenRefresh coordinator;
        auto bound = coordinator.bind(job, inner, store, test::context());
        coordinator.initial(bound);
        coordinator.initial(bound);
        test::require(inner->calls == 1, "same operation repeated initial refresh");
        (void)bound->refreshPacmtokenWithPolicy("fixture-only", false, 0);
        (void)bound->refreshPacmtokenWithPolicy("fixture-only", false, 300);
        test::require(inner->calls == 1, "interval zero or cached period triggered network");
        (void)bound->refreshPacmtokenWithPolicy("fixture-only", true, 300);
        test::require(inner->calls == 2, "explicit rejection refresh was suppressed");
        auto next = job;
        next.operation_id = "fixture_next";
        coordinator.initial(coordinator.bind(next, inner, store, test::context()));
        test::require(inner->calls == 3, "new operation did not receive initial refresh");

        std::promise<void> entered, release;
        auto begun = entered.get_future();
        auto resume = release.get_future().share();
        auto blocking = std::make_shared<Refresher>();
        blocking->during = [&] { entered.set_value(); resume.wait(); };
        next.operation_id = "fixture_blocked";
        auto owner = coordinator.bind(next, blocking, store, test::context());
        auto active = std::async(std::launch::async, [&] { coordinator.initial(owner); });
        test::require(begun.wait_for(std::chrono::seconds(1)) == std::future_status::ready, "refresh did not reach barrier");
        std::atomic_bool cancelled {false};
        auto context = test::context();
        context.cancelled = [&] { return cancelled.load(); };
        auto waiter = coordinator.bind(next, blocking, store, context);
        auto waiting = std::async(std::launch::async, [&] { coordinator.initial(waiter); });
        cancelled.store(true);
        const bool exited = waiting.wait_for(std::chrono::seconds(1)) == std::future_status::ready;
        release.set_value();
        active.get();
        waiting.get();
        test::require(exited && blocking->calls == 1, "cancelled waiter affected active refresh or requested again");
        std::cout << "PASS: initial/periodic/forced refresh and independent waiter cancellation\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "token refresh test failed: " << error.what() << '\n';
        return 1;
    }
}
