#include "crawler/application/use_cases/ResourceProducerWorker.h"
#include "crawler/infrastructure/app/ResourceTransferRuntime.h"

#include <atomic>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace c = orchestrator::modules::crawler;
namespace model = c::application::model;
namespace ports = c::application::ports;
namespace use = c::application::use_cases;
template <class T>
using Result = c::application::result::ServiceResult<T>;
using Error = c::application::result::ServiceErrorType;
using namespace std::chrono_literals;
namespace {
void require(bool value, const char *message)
{
    if (!value) throw std::runtime_error(message);
}
class Snapshot final : public model::ResourceContentSnapshot {
public:
    const std::string &filePath() const override
    {
        return path;
    }
    const std::string &relativePath() const override
    {
        return path;
    }
    const std::string &checksum() const override
    {
        return hash;
    }
    std::uint64_t sizeBytes() const override
    {
        return 11;
    }
    std::string path = "private-input", hash = std::string(64, 'a');
};
class Repository final : public ports::ResourceProducerTaskRepository {
public:
    model::ResourceProducerTask task;
    std::atomic_bool cancelled {false};
    int completions = 0, failures = 0, recoveries = 0;
    bool queue = true, uncertain = false;
    bool capacity_full = false; // 模拟子操作原子受理被拒绝
    model::ResourceTransferJobFailure failure;
    Result<std::optional<model::ResourceProducerTask>> claim(model::ResourceDestination, std::string, std::string, std::string version, int schema, bool key) override
    {
        require(version == task.processor_version && schema == task.schema_version, "claim changed processor version");
        if (!key) {
            task.state = "waiting_input";
            return Result<std::optional<model::ResourceProducerTask>>::success(std::nullopt);
        }
        if (!queue) return Result<std::optional<model::ResourceProducerTask>>::success(std::nullopt);
        queue = false;
        task.state = "running";
        ++task.claim_generation;
        ++task.attempt_count;
        return Result<std::optional<model::ResourceProducerTask>>::success(task);
    }
    Result<bool> shouldStop(const model::ResourceProducerTask &current) override
    {
        return Result<bool>::success(cancelled || current.claim_generation != task.claim_generation);
    }
    Result<std::string> complete(const model::ResourceProducerTask &current, const model::LyricArtifactBundle &bundle, std::uint64_t size) override
    {
        require(current.claim_generation == task.claim_generation && size == 11 && bundle.source_resource_id == 19, "completion lost source or claim");
        if (capacity_full) return Result<std::string>::failure(Error::ExternalApiError, "resource_queue_full");
        ++completions;
        task.state = "succeeded";
        task.child_operation_id = "rt_child";
        if (uncertain) return Result<std::string>::failure(Error::CommitUncertain, "unknown_commit");
        return Result<std::string>::success(task.child_operation_id);
    }
    Result<bool> fail(const model::ResourceProducerTask &, model::ResourceTransferJobFailure value) override
    {
        ++failures;
        failure = std::move(value);
        task.state = "failed";
        return Result<bool>::success(true);
    }
    Result<int> recover(model::ResourceDestination, int stale) override
    {
        require(stale >= 660, "recovery stole active budget");
        ++recoveries;
        return Result<int>::success(0);
    }
    Result<bool> retry(std::uint64_t) override
    {
        queue = true;
        return Result<bool>::success(true);
    }
    Result<std::vector<model::ResourceProducerTask>> list(std::string) override
    {
        return Result<std::vector<model::ResourceProducerTask>>::success({task});
    }
};
class Source final : public ports::ResourceTransferSource {
public:
    std::atomic_int calls {0};
    bool transient_failure = false, restricted = false;
    std::function<void(const model::ResourceTransferContext &)> pause;
    Result<std::shared_ptr<const model::ResourceContentSnapshot>> acquire(const model::ResourceTransferJob &job, const model::ResourceTransferContext &context) override
    {
        ++calls;
        require(job.policy.timeout_ms == 1200 && job.policy.max_bytes == 1024 * 1024, "producer borrowed parent download budget");
        if (pause) pause(context);
        if (restricted) return Result<std::shared_ptr<const model::ResourceContentSnapshot>>::failure(Error::ResourceRestricted, "copyright_restricted");
        if (transient_failure) return Result<std::shared_ptr<const model::ResourceContentSnapshot>>::failure(Error::TransientExternalApiError, "source_unavailable");
        return Result<std::shared_ptr<const model::ResourceContentSnapshot>>::success(std::make_shared<Snapshot>());
    }
};
class Generator final : public ports::ResourceArtifactGenerator {
public:
    int calls = 0;
    bool bad_input = false;
    Result<model::LyricArtifactBundle> generate(const model::ResourceTransferJob &job, const model::ResourceContentSnapshot &source, const model::ResourceTransferContext &) override
    {
        ++calls;
        if (bad_input) return Result<model::LyricArtifactBundle>::failure(Error::InvalidParam, "mrc_invalid_hex_length");
        return Result<model::LyricArtifactBundle>::success({job.resource.resource_id, source.checksum(), "fixture-v1", 1, {{"word_json", {}, 2, std::string(64, 'b')}, {"enhanced_lrc", {}, 3, std::string(64, 'c')}}});
    }
};
model::ResourceTransferTargetProfile profile()
{
    model::ResourceTransferTargetProfile result;
    result.destination = model::ResourceDestination::Baidu;
    result.available = true;
    result.produces_artifacts = true;
    result.fingerprint = std::string(64, 'd');
    result.adapter = "rclone";
    result.producer_version = "fixture-v1";
    result.producer_key_available = true;
    return result;
}
} // namespace

int main()
{
    try {
        auto tasks = std::make_shared<Repository>();
        tasks->task.producer_id = 4;
        tasks->task.processor_version = "fixture-v1";
        tasks->task.source_job.resource = {model::ResourceOwner::Song, 19};
        tasks->task.source_job.destination = model::ResourceDestination::Baidu;
        tasks->task.source_job.state = "succeeded";
        tasks->task.source_job.policy.timeout_ms = 1;
        tasks->task.policy = {1200, 2, 1024 * 1024, 0, false};
        auto source = std::make_shared<Source>();
        auto generator = std::make_shared<Generator>();
        auto budget = std::make_shared<use::ResourceTransferBudget>(1);
        auto unavailable = profile();
        unavailable.producer_key_available = false;
        use::ResourceProducerWorker waiting(tasks, source, generator, unavailable, budget);
        require(waiting.runOne().isSuccess() && tasks->task.state == "waiting_input" && source->calls == 0, "missing key fetched input or lost waiting status");
        use::ResourceProducerWorker worker(tasks, source, generator, profile(), budget);
        require(worker.runOne().isSuccess() && tasks->completions == 1 && generator->calls == 1, "terminal MRC prevented independent producer completion");
        tasks->retry(4);
        tasks->task.source_job.state = "skipped";
        tasks->task.source_job.result_code = "producer_only";
        const auto explicit_action = model::parseResourceTransferAction("produce_artifacts");
        require(explicit_action.has_value(), "explicit producer action missing");
        tasks->task.source_job.action = *explicit_action;
        require(worker.runOne().isSuccess() && tasks->completions == 2 && generator->calls == 2, "explicit historical producer depended on parent transfer or skip state");
        require(tasks->task.source_job.state == "skipped" && tasks->task.source_job.policy.timeout_ms == 1, "producer changed skipped parent or borrowed its execution budget");
        tasks->retry(4);
        auto held = budget->acquire(model::ResourceDestination::Baidu, {std::chrono::steady_clock::now() + 2s, {}});
        const int source_before_wait = source->calls.load();
        auto pending = std::async(std::launch::async, [&] { return worker.runOne(); });
        require(pending.wait_for(30ms) == std::future_status::timeout && source->calls == source_before_wait, "producer bypassed shared source concurrency limit");
        held.reset();
        require(pending.get().isSuccess(), "producer did not resume after transfer released shared source permit");
        tasks->retry(4);
        generator->bad_input = true;
        require(worker.runOne().isSuccess() && tasks->failures == 1 && tasks->failure.result_code == "mrc_invalid_hex_length" && !tasks->failure.retryable, "decoder failure was hidden or retried indefinitely");
        generator->bad_input = false;
        tasks->retry(4);
        source->transient_failure = true;
        require(worker.runOne().isSuccess() && tasks->failure.retryable, "transient source error cannot use bounded retry");
        source->transient_failure = false;
        tasks->retry(4);
        source->restricted = true;
        require(worker.runOne().isSuccess() && tasks->failure.source_restricted && !tasks->failure.retryable, "producer lost permanent source restriction");
        source->restricted = false;
        tasks->retry(4);
        tasks->uncertain = true;
        const auto before = tasks->failures;
        require(!worker.runOne().isSuccess() && tasks->task.state == "succeeded" && tasks->failures == before, "unknown commit overwrote durable completion");

        tasks->uncertain = false;
        tasks->capacity_full = true;
        tasks->retry(4);
        require(worker.runOne().isSuccess() && tasks->failure.retryable && tasks->failure.error_category == "capacity" && tasks->failure.result_code == "resource_queue_full", "full child queue lost bounded producer retry");
        tasks->capacity_full = false;
        tasks->retry(4);
        std::promise<void> entered, finished;
        auto ready = entered.get_future(), exited = finished.get_future();
        source->pause = [&](const auto &context) { entered.set_value(); while (!context.stopped()) std::this_thread::sleep_for(1ms); };
        std::atomic_int normal {0};
        c::infrastructure::app::ResourceTransferRuntime runtime(true, {profile()}, {{model::ResourceDestination::Baidu, 1, [&](auto) { ++normal; return false; }}, {model::ResourceDestination::Baidu, 1, [&](auto stopped) { const auto result = worker.runOne(stopped); finished.set_value(); return result.isSuccess() && *result.data(); }, "producer"}}, 10);
        runtime.start();
        require(ready.wait_for(1s) == std::future_status::ready, "producer did not start");
        const auto sibling_deadline = std::chrono::steady_clock::now() + 1s;
        while (normal == 0 && std::chrono::steady_clock::now() < sibling_deadline) std::this_thread::sleep_for(1ms);
        require(normal > 0, "producer occupied parent transfer worker");
        require(!runtime.shutdown(), "blocked producer falsely drained");
        require(exited.wait_for(1s) == std::future_status::ready, "cancelled producer did not exit");
        require(tasks->failure.result_code == "cancelled", "shutdown producer did not retain cancelled failure");
        std::cout << "producer worker: independent budget, durable waiting/failure, bounded retry/recovery and shutdown passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
