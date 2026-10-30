#pragma once
#include "crawler/application/use_cases/ResourceTransferWorker.h"
#include "migu/support/MetadataCoverTestSupport.h"
#include <chrono>
#include <thread>
namespace orchestrator::modules::crawler::tests::transfer_timeout {
namespace model = application::model;
namespace ports = application::ports;
namespace result = application::result;
// 只供超时语义测试的完整快照替身，不进行文件或网络读取。
class Snapshot final : public model::ResourceContentSnapshot {
public:
    // 无物理 I/O，目标替身只核对冻结意图。
    const std::string &filePath() const override
    {
        return unused_;
    }
    // 固定相对名。
    const std::string &relativePath() const override
    {
        return unused_;
    }
    // 固定测试 SHA-256。
    const std::string &checksum() const override
    {
        return checksum_;
    }
    // 固定测试大小。
    std::uint64_t sizeBytes() const override
    {
        return 3;
    }

private:
    std::string unused_ = "timeout-fixture.bin";  // 不会打开的路径
    std::string checksum_ = std::string(64, 'a'); // 替身证据
};
// 无延迟来源，只验证超时发生在 publish 以后。
class Source final : public ports::ResourceTransferSource {
public:
    int acquisitions = 0; // 修复核对不得重新取得来源
    // 不调用源站。
    result::ServiceResult<std::shared_ptr<const model::ResourceContentSnapshot>> acquire(const model::ResourceTransferJob &, const model::ResourceTransferContext &) override
    {
        ++acquisitions;
        return result::ServiceResult<std::shared_ptr<const model::ResourceContentSnapshot>>::success(std::make_shared<Snapshot>());
    }
};
// 模拟同步操作不能及时中断、在截止时间后仍报告对象成功。
class LateWrite final : public ports::ResourceTransferWrite {
public:
    // 冻结实际任务身份。
    explicit LateWrite(model::ResourceTransferJob job) : job_(std::move(job))
    {}
    // 发布前的精确对象。
    model::ResourceTransferAttempt attempt() const override
    {
        return {"timeout-fixture/" + std::to_string(job_.task_id), std::string(64, 'a'), 3};
    }
    // 故意忽略上下文，验证上层独立预算仍拒绝晚到成功。
    result::ServiceResult<model::ResourceTransferReceipt> publish(const model::ResourceTransferContext &) override
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        model::ResourceTransferReceipt receipt;
        receipt.path = attempt().path;
        receipt.checksum = attempt().checksum;
        receipt.size_bytes = 3;
        receipt.adapter = "rclone";
        receipt.verification = model::ResourceVerification::Sha256Verified;
        receipt.verification_method = "remote_sha256";
        receipt.remote_hash_algorithm = "sha256";
        receipt.remote_hash_value = receipt.checksum;
        receipt.uploaded = true;
        return result::ServiceResult<model::ResourceTransferReceipt>::success(std::move(receipt));
    }
    // 替身没有需要回收的物理对象。
    void settle(model::ResourceTransferSettlement) noexcept override
    {}

private:
    model::ResourceTransferJob job_; // 当前认领
};
// 只准备当前写入，不在这里确认回执。
class Destination final : public ports::ResourceTransferDestination {
public:
    // 核对也故意晚到，不能利用匹配结果绕过修复预算。
    result::ServiceResult<model::ResourceAttemptInspection> inspect(const model::ResourceTransferJob &job, const model::ResourceTransferContext &) override
    {
        std::this_thread::sleep_for(std::chrono::milliseconds(2000));
        model::ResourceTransferReceipt receipt;
        receipt.path = job.attempt_path;
        receipt.checksum = job.attempt_checksum;
        receipt.size_bytes = job.attempt_size_bytes;
        receipt.adapter = "rclone";
        receipt.verification = model::ResourceVerification::Sha256Verified;
        receipt.verification_method = "remote_sha256";
        receipt.remote_hash_algorithm = "sha256";
        receipt.remote_hash_value = receipt.checksum;
        return result::ServiceResult<model::ResourceAttemptInspection>::success({model::ResourceAttemptObservation::Matched, std::move(receipt)});
    }
    // 返回会晚到的同步上传替身。
    result::ServiceResult<std::shared_ptr<ports::ResourceTransferWrite>> prepare(const model::ResourceTransferJob &job, std::shared_ptr<const model::ResourceContentSnapshot>, const model::ResourceTransferContext &) override
    {
        return result::ServiceResult<std::shared_ptr<ports::ResourceTransferWrite>>::success(std::make_shared<LateWrite>(job));
    }
};
// 使用真实任务仓储，确保晚到结果留在 uncertain，不自动重试或覆盖已有回执。
inline void verify(std::shared_ptr<ports::ResourceTransferJobRepository> jobs, model::ResourceIdentity identity, std::vector<model::ResourceTransferTargetProfile> profiles)
{
    profiles[2].policy.timeout_ms = 1500;
    model::ResourceTransferRequest request;
    request.resources = {identity};
    request.destinations = {model::ResourceDestination::Gdrive};
    const auto operation = jobs->enqueue(request, profiles);
    tests::cover::require(operation.isSuccess(), "timeout registration failed");
    auto source_budget = std::make_shared<application::use_cases::ResourceTransferBudget>(1), transfer_budget = std::make_shared<application::use_cases::ResourceTransferBudget>(1);
    auto source = std::make_shared<Source>();
    application::use_cases::ResourceTransferWorker worker(jobs, source, std::make_shared<Destination>(), profiles[2], source_budget, transfer_budget);
    tests::cover::require(worker.runOne().isSuccess(), "late return was not recorded");
    const auto state = jobs->find(operation.data()->operation_id);
    tests::cover::require(state.isSuccess() && state.data()->tasks.front().state == "uncertain" && state.data()->tasks.front().result_code == "timeout_reconcile_required", "late success bypassed deadline or lost intent");
    const auto blocked = jobs->enqueue(request, profiles);
    tests::cover::require(blocked.isSuccess() && blocked.data()->tasks.front().result_code == "reconcile_required", "late success triggered another source fetch");
    request.action = model::ResourceTransferAction::Repair;
    const auto slow_repair = jobs->enqueue(request, profiles);
    tests::cover::require(slow_repair.isSuccess() && worker.runOne().isSuccess(), "slow inspection was not recorded");
    const auto inspected = jobs->find(slow_repair.data()->operation_id);
    tests::cover::require(inspected.isSuccess() && inspected.data()->tasks.front().state == "uncertain" && inspected.data()->tasks.front().result_code == "reconcile_interrupted" && source->acquisitions == 1, "late inspection confirmed or fetched the source again");
    tests::cover::require(jobs->enqueue(request, profiles).isSuccess(), "late result repair failed");
    const auto claimed = jobs->claim(model::ResourceDestination::Gdrive, profiles[2].fingerprint, profiles[2].adapter);
    tests::cover::require(claimed.isSuccess() && claimed.data()->has_value(), "late object could not be reclaimed for inspection");
    model::ResourceTransferReceipt receipt;
    receipt.path = (**claimed.data()).attempt_path;
    receipt.checksum = std::string(64, 'a');
    receipt.size_bytes = 3;
    receipt.adapter = "rclone";
    receipt.verification = model::ResourceVerification::Sha256Verified;
    receipt.verification_method = "remote_sha256";
    receipt.remote_hash_algorithm = "sha256";
    receipt.remote_hash_value = receipt.checksum;
    const auto complete = jobs->complete(**claimed.data(), std::move(receipt));
    tests::cover::require(complete.isSuccess() && *complete.data(), "explicit inspection could not settle late object");
}
} // namespace orchestrator::modules::crawler::tests::transfer_timeout
