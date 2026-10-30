#include "crawler/application/use_cases/ResourceProducerWorker.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <string>
#include <utility>

namespace orchestrator::modules::crawler::application::use_cases {
namespace {
// 取消轮询不占有 application 锁；来源 adapter 将此检查移出网络 strand。
struct Cancellation {
    std::shared_ptr<ports::ResourceProducerTaskRepository> tasks;
    model::ResourceProducerTask task;
    std::function<bool()> stopping;
    std::atomic<std::int64_t> checked {0};
    std::atomic_bool querying {false}, stopped {false};
    bool check()
    {
        if (stopped.load()) return true;
        if (stopping && stopping()) {
            stopped.store(true);
            return true;
        }
        const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        if (now - checked.load() >= 200 && !querying.exchange(true)) {
            checked.store(now);
            const auto current = tasks->shouldStop(task);
            if (!current.isSuccess() || *current.data()) stopped.store(true);
            querying.store(false);
        }
        return stopped.load();
    }
};
// 持久失败只接受稳定码，密钥/正文/URL 不进入任务状态。
std::string code(const std::string &value)
{
    return !value.empty() && value.size() <= 64 && std::all_of(value.begin(), value.end(), [](char ch) { return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_'; }) ? value : "producer_failed";
}
} // namespace

ResourceProducerWorker::ResourceProducerWorker(std::shared_ptr<ports::ResourceProducerTaskRepository> tasks, std::shared_ptr<ports::ResourceTransferSource> source, std::shared_ptr<ports::ResourceArtifactGenerator> generator, model::ResourceTransferTargetProfile profile, std::shared_ptr<ResourceTransferBudget> source_budget) : tasks_(std::move(tasks)), source_(std::move(source)), generator_(std::move(generator)), profile_(std::move(profile)), source_budget_(std::move(source_budget))
{
    if (!tasks_ || !source_ || !generator_ || !source_budget_) throw std::invalid_argument("producer dependencies required");
}

result::ServiceResult<bool> ResourceProducerWorker::runOne(std::function<bool()> stopping)
{
    using Result = result::ServiceResult<bool>;
    if (!profile_.available || !profile_.produces_artifacts || (stopping && stopping())) return Result::success(false);
    const auto now = std::chrono::duration_cast<std::chrono::seconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
    auto previous = last_recovery_.load();
    if (now - previous >= 30 && last_recovery_.compare_exchange_strong(previous, now)) {
        // 600 秒是允许的最大执行预算，再留 60 秒给取消收尾；仓储还须逐项核对过期。
        const auto recovered = tasks_->recover(profile_.destination, 660);
        if (!recovered.isSuccess()) return Result::failure(recovered.error(), recovered.message());
    }
    const auto claimed = tasks_->claim(profile_.destination, profile_.fingerprint, profile_.adapter, profile_.producer_version, profile_.producer_schema_version, profile_.producer_key_available);
    if (!claimed.isSuccess()) return Result::failure(claimed.error(), claimed.message());
    if (!claimed.data()->has_value()) return Result::success(false);
    const auto task = **claimed.data();
    auto cancellation = std::make_shared<Cancellation>();
    cancellation->tasks = tasks_;
    cancellation->task = task;
    cancellation->stopping = std::move(stopping);
    model::ResourceTransferContext context {std::chrono::steady_clock::now() + std::chrono::milliseconds(task.policy.timeout_ms), [cancellation] { return cancellation->check(); }};
    const auto failed = [&](result::ServiceErrorType error, const std::string &message) {
        model::ResourceTransferJobFailure failure;
        failure.result_code = code(message);
        failure.error_category = message == "resource_queue_full" ? "capacity" : error == result::ServiceErrorType::Conflict ? "content" :
                                                                             error == result::ServiceErrorType::InvalidParam ? "producer" :
                                                                                                                               "network";
        failure.retryable = error == result::ServiceErrorType::TransientExternalApiError || error == result::ServiceErrorType::ExternalRateLimited || message == "timeout" || message == "cancelled" || message == "resource_queue_full";
        failure.uncertain = error == result::ServiceErrorType::CommitUncertain;
        failure.source_restricted = error == result::ServiceErrorType::ResourceRestricted;
        const auto saved = tasks_->fail(task, std::move(failure));
        return saved.isSuccess() ? Result::success(true) : Result::failure(saved.error(), saved.message());
    };
    try {
        auto permit = source_budget_->acquire(task.source_job.destination, context);
        if (!permit) return failed(result::ServiceErrorType::ExternalApiError, context.remaining() == 0 ? "timeout" : "cancelled");
        auto source_job = task.source_job;
        source_job.policy = task.policy;
        source_job.policy.max_bytes = (std::min<std::uint64_t>)(source_job.policy.max_bytes, 1024 * 1024);
        const auto acquired = source_->acquire(source_job, context);
        if (!acquired.isSuccess()) return failed(acquired.error(), acquired.message());
        if (!*acquired.data() || (*acquired.data())->sizeBytes() == 0 || (*acquired.data())->sizeBytes() > source_job.policy.max_bytes) return failed(result::ServiceErrorType::InvalidParam, "invalid_source_snapshot");
        permit.reset();
        if (context.stopped()) return failed(result::ServiceErrorType::ExternalApiError, context.remaining() == 0 ? "timeout" : "cancelled");
        const auto bundle = generator_->generate(source_job, **acquired.data(), context);
        if (!bundle.isSuccess()) return failed(bundle.error(), bundle.message());
        if (bundle.data()->processor_version != task.processor_version || bundle.data()->schema_version != task.schema_version) return failed(result::ServiceErrorType::Conflict, "producer_version_changed");
        if (context.stopped()) return failed(result::ServiceErrorType::ExternalApiError, context.remaining() == 0 ? "timeout" : "cancelled");
        const auto completed = tasks_->complete(task, *bundle.data(), (*acquired.data())->sizeBytes());
        // 提交响应未知时不再 fail 改写可能已完成的行；租约恢复先读持久结果。
        if (completed.isSuccess()) return Result::success(true);
        if (completed.error() == result::ServiceErrorType::CommitUncertain) return Result::failure(completed.error(), completed.message());
        return failed(completed.error(), completed.message());
    } catch (...) {
        return failed(result::ServiceErrorType::InternalError, "producer_failed");
    }
}
} // namespace orchestrator::modules::crawler::application::use_cases
