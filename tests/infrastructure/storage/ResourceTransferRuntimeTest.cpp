#include "crawler/infrastructure/app/ResourceTransferRuntime.h"
#include "crawler/infrastructure/app/ResourcePipelineConfiguration.h"
#include "crawler/application/use_cases/ResourceTransferBudget.h"
#include "../../../src/infrastructure/app/ResourceTransferIdentityFence.h"

#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <thread>

namespace c = orchestrator::modules::crawler;
namespace app = c::infrastructure::app;
namespace model = c::application::model;
namespace ports = c::application::ports;
namespace result = c::application::result;
using Destination = model::ResourceDestination;
using namespace std::chrono_literals;
namespace {
// 固定说明避免依赖 assert 构建开关。
void require(bool value, const char *message)
{
    if (!value) throw std::runtime_error(message);
}
// 仅生成无凭据的测试配置。
std::vector<model::ResourceTransferTargetProfile> profiles()
{
    std::vector<model::ResourceTransferTargetProfile> result;
    for (const auto destination : {Destination::Local, Destination::Baidu, Destination::Gdrive}) {
        model::ResourceTransferTargetProfile profile;
        profile.destination = destination;
        profile.adapter = destination == Destination::Local ? "filesystem" : "rclone";
        profile.fingerprint = std::string(64, char('a' + static_cast<int>(destination)));
        result.push_back(profile);
    }
    return result;
}

class TestSource final : public ports::ResourceTransferSource {
public:
    int calls = 0;
    result::ServiceResult<std::shared_ptr<const model::ResourceContentSnapshot>> acquire(const model::ResourceTransferJob &, const model::ResourceTransferContext &) override
    {
        ++calls;
        return result::ServiceResult<std::shared_ptr<const model::ResourceContentSnapshot>>::success(nullptr);
    }
};
class TestWrite final : public ports::ResourceTransferWrite {
public:
    int calls = 0;
    std::function<void()> on_publish;
    model::ResourceVerification verification = model::ResourceVerification::HashMatched;
    model::ResourceTransferAttempt attempt() const override
    {
        return {};
    }
    result::ServiceResult<model::ResourceTransferReceipt> publish(const model::ResourceTransferContext &) override
    {
        ++calls;
        if (on_publish) on_publish();
        model::ResourceTransferReceipt receipt;
        receipt.verification = verification;
        return result::ServiceResult<model::ResourceTransferReceipt>::success(receipt);
    }
    void settle(model::ResourceTransferSettlement) noexcept override
    {}
};
class TestDestination final : public ports::ResourceTransferDestination {
public:
    std::shared_ptr<TestWrite> write = std::make_shared<TestWrite>();
    result::ServiceResult<model::ResourceAttemptInspection> inspect(const model::ResourceTransferJob &, const model::ResourceTransferContext &) override
    {
        model::ResourceAttemptInspection value;
        value.observation = model::ResourceAttemptObservation::Matched;
        value.receipt.verification = write->verification;
        return result::ServiceResult<model::ResourceAttemptInspection>::success(value);
    }
    result::ServiceResult<std::shared_ptr<ports::ResourceTransferWrite>> prepare(const model::ResourceTransferJob &, std::shared_ptr<const model::ResourceContentSnapshot>, const model::ResourceTransferContext &) override
    {
        return result::ServiceResult<std::shared_ptr<ports::ResourceTransferWrite>>::success(write);
    }
};
// 验证身份变化的每个副作用边界和冻结的强校验要求。
void identityFence()
{
    model::ResourceTransferJob job;
    job.destination = Destination::Baidu;
    job.adapter = "rclone";
    job.target_fingerprint = "expected";
    app::ResourceTargetVerification observed;
    observed.profile = {job.destination, job.target_fingerprint, {}, true, job.adapter};
    auto probe = [&](const auto &) { return observed; };
    model::ResourceTransferContext context {std::chrono::steady_clock::now() + 5s, {}};
    auto source = std::make_shared<TestSource>();
    auto source_fence = app::fenceResourceSource(source, probe);
    observed.profile.fingerprint = "changed";
    require(!source_fence->acquire(job, context).isSuccess() && source->calls == 0, "identity change acquired source");
    observed.profile.fingerprint = "expected";
    require(source_fence->acquire(job, context).isSuccess() && source->calls == 1, "matching source identity rejected");
    auto destination = std::make_shared<TestDestination>();
    auto fence = app::fenceResourceDestination(destination, probe);
    auto prepared = fence->prepare(job, nullptr, context);
    require(prepared.isSuccess(), "matching destination identity rejected");
    observed.profile.fingerprint = "changed";
    require(!(*prepared.data())->publish(context).isSuccess() && destination->write->calls == 0, "identity change published object");
    observed.profile.fingerprint = "expected";
    destination->write->on_publish = [&] { observed.profile.available = false; };
    const auto uncertain = (*prepared.data())->publish(context);
    require(!uncertain.isSuccess() && uncertain.error() == result::ServiceErrorType::CommitUncertain && destination->write->calls == 1, "post-publish unavailable identity reported confirmed");
    observed.profile.available = true;
    destination->write->on_publish = {};
    job.policy.strict_sha256 = true;
    prepared = fence->prepare(job, nullptr, context);
    const auto weak = (*prepared.data())->publish(context);
    require(!weak.isSuccess() && weak.error() == result::ServiceErrorType::CommitUncertain, "strict frozen job accepted MD5");
    require(fence->inspect(job, context).data()->observation == model::ResourceAttemptObservation::Unknown, "strict repair accepted weak receipt");
    destination->write->verification = model::ResourceVerification::Sha256Verified;
    require((*prepared.data())->publish(context).isSuccess(), "strict SHA256 receipt rejected");
}
} // namespace

int main()
{
    try {
        identityFence();
        std::atomic_int local {0}, baidu {0}, google {0};
        std::promise<void> entered, completed;
        auto ready = entered.get_future(), stopped = completed.get_future();
        auto dependency = std::make_shared<int>(1);
        std::weak_ptr<int> retained = dependency;
        std::vector<app::ResourceRuntimeLane> lanes {
            {Destination::Local, 1, [&](auto) { return local.fetch_add(1) == 0; }},
            {Destination::Baidu, 1, [&, keep = dependency](auto cancelled) {
                 if (baidu.fetch_add(1)) return false;
                 entered.set_value();
                 while (!cancelled()) std::this_thread::sleep_for(1ms);
                 require(*keep == 1, "shutdown destroyed active dependency");
                 completed.set_value();
                 return true;
             }},
            {Destination::Gdrive, 1, [&](auto) { return google.fetch_add(1) == 0; }}
        };
        auto runtime = std::make_unique<app::ResourceTransferRuntime>(true, profiles(), std::move(lanes), 20);
        dependency.reset();
        require(local == 0 && baidu == 0 && google == 0, "construction started content work");
        runtime->start();
        runtime->start();
        require(ready.wait_for(1s) == std::future_status::ready, "Baidu did not start");
        const auto until = std::chrono::steady_clock::now() + 1s;
        while ((!local || !google) && std::chrono::steady_clock::now() < until) std::this_thread::sleep_for(1ms);
        require(local && google, "blocked Baidu stopped sibling lanes");
        auto snapshot = runtime->profiles();
        auto policy = snapshot[2].policy;
        policy.retries = 4;
        require(runtime->updatePolicy(Destination::Gdrive, policy).isSuccess() && runtime->profiles()[2].policy.retries == 4 && snapshot[2].policy.retries == 1, "policy update mutated existing snapshot");
        const auto begin = std::chrono::steady_clock::now();
        require(!runtime->shutdown(), "unresolved work was reported drained");
        require(std::chrono::steady_clock::now() - begin < 500ms && !runtime->parallelEnabled(), "shutdown ignored grace or kept admission enabled");
        runtime.reset();
        require(stopped.wait_for(1s) == std::future_status::ready, "grace expiration failed to cancel active lane");
        const auto expiry = std::chrono::steady_clock::now() + 1s;
        while (!retained.expired() && std::chrono::steady_clock::now() < expiry) std::this_thread::sleep_for(1ms);
        require(retained.expired(), "finished detached lane leaked dependencies");

        std::promise<void> drain_entered;
        auto drain_ready = drain_entered.get_future();
        std::atomic_bool drain_cancelled {false};
        app::ResourceTransferRuntime draining(true, profiles(), {{Destination::Local, 1, [&](auto cancel) {
                                                                      drain_entered.set_value();
                                                                      std::this_thread::sleep_for(40ms);
                                                                      drain_cancelled.store(cancel());
                                                                      return false;
                                                                  }}},
                                              1000);
        draining.start();
        require(drain_ready.wait_for(1s) == std::future_status::ready, "draining lane did not start");
        require(draining.shutdown() && !drain_cancelled, "shutdown cancelled work before grace or falsely failed drain");

        std::atomic_int observed {0};
        std::promise<void> observer_started;
        auto observer_ready = observer_started.get_future();
        app::ResourceTransferRuntime observer(true, {}, {}, 1000);
        observer.addObserver([&] { if (observed.fetch_add(1) == 0) observer_started.set_value(); return true; });
        observer.start();
        require(observer_ready.wait_for(1s) == std::future_status::ready, "observer depended on enabled local profile");
        std::this_thread::sleep_for(40ms);
        require(observed == 1 && observer.shutdown(), "successful observer busy-polled or failed to drain");

        c::application::use_cases::ResourceTransferBudget budget(3, {true, true, true});
        model::ResourceTransferContext context {std::chrono::steady_clock::now() + 2s, {}};
        auto first = budget.acquire(Destination::Local, context);
        std::atomic_bool cancel_waiter {false};
        auto pending = std::async(std::launch::async, [&] { auto waiting = context; waiting.cancelled = [&] { return cancel_waiter.load(); }; return budget.acquire(Destination::Local, waiting); });
        require(pending.wait_for(30ms) == std::future_status::timeout, "one lane consumed another reserved permit");
        auto second = budget.acquire(Destination::Baidu, context);
        auto third = budget.acquire(Destination::Gdrive, context);
        require(first && second && third, "reserved permits deadlocked target rotation");
        cancel_waiter.store(true);
        require(!pending.get(), "cancelled quota waiter acquired a permit");

        app::ServerApplication::Config config;
        app::validateResourcePipelineConfiguration(config);
        config.resource_pipeline.mode = "parallel";
        config.resource_pipeline.targets[0].enabled = true;
        config.resource_pipeline.targets[0].adapter = "filesystem";
        config.resource_pipeline.targets[1].enabled = true;
        config.resource_pipeline.targets[1].adapter = "rclone";
        app::validateResourcePipelineConfiguration(config);
        config.resource_pipeline.source_concurrency = 1;
        bool rejected = false;
        try {
            app::validateResourcePipelineConfiguration(config);
        } catch (const std::invalid_argument &) {
            rejected = true;
        }
        require(rejected, "insufficient source capacity was silently accepted");
        std::cout << "resource runtime: independent lanes, immutable policy snapshots, bounded shutdown and reserved budgets passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "resource runtime test failed: " << error.what() << '\n';
        return 1;
    }
}
