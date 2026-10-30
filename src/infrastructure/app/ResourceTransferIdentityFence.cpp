#include "ResourceTransferIdentityFence.h"

#include <utility>

namespace orchestrator::modules::crawler::infrastructure::app {
namespace {
namespace model = application::model;
namespace ports = application::ports;
namespace result = application::result;
// 目标变化只拒绝本端，不把新位置冒充已经登记的对象身份。
bool matches(const ResourceTargetProbe &probe, const model::ResourceTransferJob &job, const model::ResourceTransferContext &context)
{
    if (context.stopped()) return false;
    try {
        const auto observed = probe(context);
        return observed.profile.available && observed.profile.destination == job.destination && observed.profile.adapter == job.adapter && observed.profile.fingerprint == job.target_fingerprint;
    } catch (...) {
        return false;
    }
}
// 来源取得之前再次只读确认目标，避免花费来源流量后才发现配置变动。
class Source final : public ports::ResourceTransferSource {
public:
    Source(std::shared_ptr<ports::ResourceTransferSource> source, ResourceTargetProbe probe) : source_(std::move(source)), probe_(std::move(probe))
    {}
    result::ServiceResult<std::shared_ptr<const model::ResourceContentSnapshot>> acquire(const model::ResourceTransferJob &job, const model::ResourceTransferContext &context) override
    {
        if (!matches(probe_, job, context)) return result::ServiceResult<std::shared_ptr<const model::ResourceContentSnapshot>>::failure(result::ServiceErrorType::Conflict, "target_identity_changed");
        return source_->acquire(job, context);
    }

private:
    std::shared_ptr<ports::ResourceTransferSource> source_; // 独立来源/配额
    ResourceTargetProbe probe_;                             // 只读身份探测
};
// 在正式副作用前最后一次比较实际身份，并遵守所配置核验强度。
class Write final : public ports::ResourceTransferWrite {
public:
    Write(std::shared_ptr<ports::ResourceTransferWrite> write, ResourceTargetProbe probe, model::ResourceTransferJob job) : write_(std::move(write)), probe_(std::move(probe)), job_(std::move(job))
    {}
    model::ResourceTransferAttempt attempt() const override
    {
        return write_->attempt();
    }
    result::ServiceResult<model::ResourceTransferReceipt> publish(const model::ResourceTransferContext &context) override
    {
        using Result = result::ServiceResult<model::ResourceTransferReceipt>;
        if (!matches(probe_, job_, context)) return Result::failure(result::ServiceErrorType::Conflict, "target_identity_changed");
        const auto value = write_->publish(context);
        if (value.isSuccess() && !matches(probe_, job_, context)) return Result::failure(result::ServiceErrorType::CommitUncertain, "target_identity_changed");
        if (value.isSuccess() && job_.policy.strict_sha256 && value.data()->verification != model::ResourceVerification::Sha256Verified) return Result::failure(result::ServiceErrorType::CommitUncertain, "strict_verification_required");
        return value;
    }
    void settle(model::ResourceTransferSettlement settlement) noexcept override
    {
        write_->settle(settlement);
    }

private:
    std::shared_ptr<ports::ResourceTransferWrite> write_; // 独占发布会话
    ResourceTargetProbe probe_;                           // 发布前核对
    model::ResourceTransferJob job_;                      // 冻结身份
};
// inspect 与 prepare 均不能对变化后的目标执行旧意图。
class Destination final : public ports::ResourceTransferDestination {
public:
    Destination(std::shared_ptr<ports::ResourceTransferDestination> destination, ResourceTargetProbe probe) : destination_(std::move(destination)), probe_(std::move(probe))
    {}
    result::ServiceResult<model::ResourceAttemptInspection> inspect(const model::ResourceTransferJob &job, const model::ResourceTransferContext &context) override
    {
        using Result = result::ServiceResult<model::ResourceAttemptInspection>;
        if (!matches(probe_, job, context)) return Result::failure(result::ServiceErrorType::Conflict, "target_identity_changed");
        auto value = destination_->inspect(job, context);
        if (value.isSuccess() && job.policy.strict_sha256 && value.data()->observation == model::ResourceAttemptObservation::Matched && value.data()->receipt.verification != model::ResourceVerification::Sha256Verified) return Result::success({model::ResourceAttemptObservation::Unknown, {}});
        return value;
    }
    result::ServiceResult<std::shared_ptr<ports::ResourceTransferWrite>> prepare(const model::ResourceTransferJob &job, std::shared_ptr<const model::ResourceContentSnapshot> snapshot, const model::ResourceTransferContext &context) override
    {
        using Result = result::ServiceResult<std::shared_ptr<ports::ResourceTransferWrite>>;
        if (!matches(probe_, job, context)) return Result::failure(result::ServiceErrorType::Conflict, "target_identity_changed");
        const auto value = destination_->prepare(job, std::move(snapshot), context);
        if (!value.isSuccess()) return value;
        return Result::success(std::make_shared<Write>(*value.data(), probe_, job));
    }

private:
    std::shared_ptr<ports::ResourceTransferDestination> destination_; // 本端独立策略
    ResourceTargetProbe probe_;                                       // 每边界实际身份
};
} // namespace

std::shared_ptr<application::ports::ResourceTransferSource> fenceResourceSource(std::shared_ptr<application::ports::ResourceTransferSource> source, ResourceTargetProbe probe)
{
    return std::make_shared<Source>(std::move(source), std::move(probe));
}
std::shared_ptr<application::ports::ResourceTransferDestination> fenceResourceDestination(std::shared_ptr<application::ports::ResourceTransferDestination> destination, ResourceTargetProbe probe)
{
    return std::make_shared<Destination>(std::move(destination), std::move(probe));
}
} // namespace orchestrator::modules::crawler::infrastructure::app
