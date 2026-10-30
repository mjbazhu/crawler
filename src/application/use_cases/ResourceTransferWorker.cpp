#include "crawler/application/use_cases/ResourceTransferWorker.h"

#include <algorithm>
#include <atomic>
#include <stdexcept>
#include <utility>

namespace orchestrator::modules::crawler::application::use_cases {
namespace {
// 并发回调只让一个线程轮询；数据库 I/O 不在任何 application 互斥锁内。
struct StopProbe {
    std::shared_ptr<ports::ResourceTransferJobRepository> jobs; // 持有到回调结束
    model::ResourceTransferJob job;                             // 本次唯一认领
    std::function<bool()> stopping;                             // 工厂/进程停止
    std::atomic<std::int64_t> last_check {0};                   // 最多每 200 ms 查一次持久状态
    std::atomic<bool> stopped {false};                          // 失败关闭并锁存
    std::atomic<bool> checking {false};                         // 同时最多一个持久状态查询
    // 不取消父操作或兄弟目标。
    bool check()
    {
        if (stopped.load()) return true;
        if (stopping && stopping()) {
            stopped.store(true);
            return true;
        }
        const auto now = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now().time_since_epoch()).count();
        const auto previous = last_check.load();
        if (now - previous >= 200 && !checking.exchange(true)) {
            last_check.store(now);
            const auto value = jobs->shouldStop(job);
            if (!value.isSuccess() || *value.data()) stopped.store(true);
            checking.store(false);
        }
        return stopped.load();
    }
};

// 防止任意错误正文进入任务稳定结果码。
std::string stableCode(const std::string &message, const char *fallback)
{
    return !message.empty() && message.size() <= 64 && std::all_of(message.begin(), message.end(), [](char ch) { return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_'; }) ? message : fallback;
}

// 已登记任务的单端失败；未知副作用不自动清除占用。
result::ServiceResult<bool> failed(ports::ResourceTransferJobRepository &jobs, const model::ResourceTransferJob &job, result::ServiceErrorType type, const std::string &message, bool uncertain = false, std::optional<std::uint8_t> related_mask = {})
{
    model::ResourceTransferJobFailure failure;
    failure.result_code = stableCode(message, "transfer_failed");
    failure.error_category = type == result::ServiceErrorType::Conflict ? "content" : type == result::ServiceErrorType::InvalidParam                                                                                                                         ? "invalid_target" :
                                                                                  type == result::ServiceErrorType::ExternalApiError || type == result::ServiceErrorType::TransientExternalApiError || type == result::ServiceErrorType::ExternalRateLimited ? "network" :
                                                                                                                                                                                                                                                               "persistence";
    failure.uncertain = uncertain || type == result::ServiceErrorType::CommitUncertain;
    failure.source_restricted = type == result::ServiceErrorType::ResourceRestricted;
    if (failure.source_restricted) failure.related_mask = related_mask;
    failure.retryable = !failure.uncertain && (type == result::ServiceErrorType::TransientExternalApiError || type == result::ServiceErrorType::ExternalRateLimited);
    const auto recorded = jobs.fail(job, std::move(failure));
    if (!recorded.isSuccess()) return result::ServiceResult<bool>::failure(recorded.error(), recorded.message());
    return result::ServiceResult<bool>::success(true);
}

// 已发布但尚未确认时默认保留补偿；异常不能由析构误删对象。
struct WriteGuard {
    std::shared_ptr<ports::ResourceTransferWrite> write; // 独占尝试的共享生命周期
    bool publishing = false;                             // 已进入可能有副作用的边界
    bool settled = false;                                // 已明确处理一次
    // 副作用前可清理 staging，之后默认等待核对。
    ~WriteGuard()
    {
        if (write && !settled) write->settle(publishing ? model::ResourceTransferSettlement::Defer : model::ResourceTransferSettlement::Abort);
    }
    // 只调用一次底层确认。
    void settle(model::ResourceTransferSettlement value)
    {
        if (!settled && write) {
            write->settle(value);
            settled = true;
        }
    }
};
} // namespace

ResourceTransferWorker::ResourceTransferWorker(std::shared_ptr<ports::ResourceTransferJobRepository> jobs, std::shared_ptr<ports::ResourceTransferSource> source, std::shared_ptr<ports::ResourceTransferDestination> destination, model::ResourceTransferTargetProfile profile, std::shared_ptr<ResourceTransferBudget> source_budget, std::shared_ptr<ResourceTransferBudget> transfer_budget) : jobs_(std::move(jobs)), source_(std::move(source)), destination_(std::move(destination)), profile_(std::move(profile)), source_budget_(std::move(source_budget)), transfer_budget_(std::move(transfer_budget))
{
    if (!jobs_ || !source_ || !destination_ || !source_budget_ || !transfer_budget_) throw std::invalid_argument("resource transfer dependencies required");
}

result::ServiceResult<bool> ResourceTransferWorker::runOne(std::function<bool()> stopping)
{
    if (!profile_.available) return result::ServiceResult<bool>::success(false);
    if (stopping && stopping()) return result::ServiceResult<bool>::success(false);
    const auto claimed = jobs_->claim(profile_.destination, profile_.fingerprint, profile_.adapter);
    if (!claimed.isSuccess()) return result::ServiceResult<bool>::failure(claimed.error(), claimed.message());
    if (!claimed.data()->has_value()) return result::ServiceResult<bool>::success(false);
    return execute(**claimed.data(), std::move(stopping));
}

result::ServiceResult<bool> ResourceTransferWorker::execute(model::ResourceTransferJob job, std::function<bool()> stopping)
{
    auto probe = std::make_shared<StopProbe>();
    probe->jobs = jobs_;
    probe->job = job;
    probe->stopping = std::move(stopping);
    auto related_mask = std::make_shared<std::optional<std::uint8_t>>();
    model::ResourceTransferContext context {std::chrono::steady_clock::now() + std::chrono::milliseconds(job.policy.timeout_ms), [probe] { return probe->check(); }, [related_mask](std::uint8_t mask) { *related_mask = mask; }};
    WriteGuard guard;
    try {
        if (!job.attempt_path.empty()) {
            auto permit = transfer_budget_->acquire(job.destination, context);
            if (!permit) return failed(*jobs_, job, result::ServiceErrorType::CommitUncertain, "reconcile_interrupted", true);
            const auto inspected = destination_->inspect(job, context);
            if (!inspected.isSuccess()) return failed(*jobs_, job, inspected.error(), inspected.message(), true);
            if (context.stopped()) return failed(*jobs_, job, result::ServiceErrorType::CommitUncertain, "reconcile_interrupted", true);
            const auto &value = *inspected.data();
            if (value.observation == model::ResourceAttemptObservation::Matched) {
                const auto confirmed = jobs_->complete(job, value.receipt);
                if (!confirmed.isSuccess()) return result::ServiceResult<bool>::failure(confirmed.error(), confirmed.message());
                return result::ServiceResult<bool>::success(true);
            }
            if (value.observation != model::ResourceAttemptObservation::Absent)
                return failed(*jobs_, job, result::ServiceErrorType::CommitUncertain, value.observation == model::ResourceAttemptObservation::Conflict ? "content_conflict" : "reconcile_required", true);
        }
        std::shared_ptr<const model::ResourceContentSnapshot> snapshot;
        {
            auto permit = source_budget_->acquire(job.destination, context);
            if (!permit) return failed(*jobs_, job, result::ServiceErrorType::ExternalApiError, context.remaining() == 0 ? "timeout" : "cancelled");
            const auto acquired = source_->acquire(job, context);
            if (!acquired.isSuccess()) return failed(*jobs_, job, acquired.error(), acquired.message(), false, *related_mask);
            snapshot = *acquired.data();
        }
        if (!snapshot || snapshot->sizeBytes() == 0 || snapshot->sizeBytes() > job.policy.max_bytes) return failed(*jobs_, job, result::ServiceErrorType::InvalidParam, "invalid_source_snapshot");
        auto permit = transfer_budget_->acquire(job.destination, context);
        if (!permit) return failed(*jobs_, job, result::ServiceErrorType::ExternalApiError, context.remaining() == 0 ? "timeout" : "cancelled");
        const auto prepared = destination_->prepare(job, snapshot, context);
        if (!prepared.isSuccess()) return failed(*jobs_, job, prepared.error(), prepared.message());
        guard.write = *prepared.data();
        if (!guard.write) return failed(*jobs_, job, result::ServiceErrorType::InternalError, "missing_transfer_session");
        auto intent = guard.write->attempt();
        if (job.process_related_resources) intent.related_mask = snapshot->relatedMask();
        if (intent.checksum != snapshot->checksum() || intent.size_bytes != snapshot->sizeBytes()) return failed(*jobs_, job, result::ServiceErrorType::Conflict, "content_conflict");
        const auto registered = jobs_->beginAttempt(job, intent);
        if (!registered.isSuccess()) return result::ServiceResult<bool>::failure(registered.error(), registered.message());
        if (!*registered.data()) {
            const auto rejected = jobs_->fail(job, {"attempt_rejected", "persistence", false, false});
            if (!rejected.isSuccess()) return result::ServiceResult<bool>::failure(rejected.error(), rejected.message());
            return result::ServiceResult<bool>::success(true);
        }
        if (context.stopped()) return failed(*jobs_, job, result::ServiceErrorType::ExternalApiError, context.remaining() == 0 ? "timeout" : "cancelled");
        guard.publishing = true;
        const auto published = guard.write->publish(context);
        if (!published.isSuccess()) {
            const bool uncertain = published.error() == result::ServiceErrorType::CommitUncertain || published.error() == result::ServiceErrorType::Conflict;
            guard.settle(uncertain ? model::ResourceTransferSettlement::Defer : model::ResourceTransferSettlement::Abort);
            return failed(*jobs_, job, published.error(), published.message(), uncertain);
        }
        // 同步适配器可能在预算到期后才返回；已存在对象保留待核对，不晚报成功。
        if (context.stopped()) {
            guard.settle(model::ResourceTransferSettlement::Defer);
            return failed(*jobs_, job, result::ServiceErrorType::CommitUncertain, context.remaining() == 0 ? "timeout_reconcile_required" : "cancelled_reconcile_required", true);
        }
        const auto confirmed = jobs_->complete(job, *published.data());
        if (!confirmed.isSuccess()) return result::ServiceResult<bool>::failure(confirmed.error(), confirmed.message());
        guard.settle(*confirmed.data() ? model::ResourceTransferSettlement::Accept : model::ResourceTransferSettlement::Defer);
        return result::ServiceResult<bool>::success(true);
    } catch (const std::exception &) {
        return failed(*jobs_, job, result::ServiceErrorType::InternalError, "worker_failed", guard.publishing);
    } catch (...) {
        return failed(*jobs_, job, result::ServiceErrorType::InternalError, "worker_failed", guard.publishing);
    }
}

} // namespace orchestrator::modules::crawler::application::use_cases
