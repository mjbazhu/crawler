#pragma once

#include "crawler/application/ports/persistence/MetadataCoverCloudQueue.h"
#include "crawler/application/ports/storage/CloudFileStore.h"
#include "crawler/infrastructure/storage/cloud/CloudFileSupport.h"
#include "migu/support/MetadataCoverTestSupport.h"

#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace orchestrator::modules::crawler::tests::cover {

// 在既有本地 fixture 上复现云端条件确认与刷新竞态。
class CloudQueue final : public ports::MetadataCoverCloudQueue {
public:
    std::shared_ptr<Queue> local; // 与本地队列共享真实记录
    bool uncertain = false;       // 模拟成功回写无法确认
    bool committed = false;       // 不确定时是否实际写入
    // 保存用于事务边界检查的本地 fixture。
    explicit CloudQueue(std::shared_ptr<Queue> value) : local(std::move(value))
    {}
    // 重读共享记录。
    Result<std::optional<model::MetadataCoverRecord>> find(model::CoverOwner owner, std::uint64_t id) override
    {
        return local->find(owner, id);
    }
    // 云端只认领当前完整文件快照。
    Result<std::optional<model::MetadataCoverRecord>> claimCloud(const model::MetadataCoverRecord &target, const model::CloudFileReceipt &, const std::string &batch) override
    {
        std::lock_guard lock(local->mutex);
        auto &row = local->rows.at({target.owner, target.resource_id});
        if (row.file.status != 2 || row.cloud.status == 1 || row.cloud.claim_generation != target.cloud.claim_generation || row.file.local_path != target.file.local_path || row.file.checksum != target.file.checksum) return Result<std::optional<model::MetadataCoverRecord>>::success(std::nullopt);
        ++row.cloud.claim_generation;
        row.cloud.status = 1;
        row.cloud.running_batch_id = batch;
        return Result<std::optional<model::MetadataCoverRecord>>::success(row);
    }
    // 替换图片或恢复代次后拒绝旧云端 worker。
    Result<bool> succeedCloud(const model::MetadataCoverRecord &target, const model::CloudFileReceipt &receipt) override
    {
        std::lock_guard lock(local->mutex);
        auto &row = local->rows.at({target.owner, target.resource_id});
        if (row.cloud.status != 1 || row.cloud.claim_generation != target.cloud.claim_generation || row.file.status != 2 || row.file.local_path != target.file.local_path || row.file.checksum != target.file.checksum) return Result<bool>::success(false);
        if (!uncertain || committed) {
            row.cloud.status = 2;
            row.cloud.receipt = receipt;
        }
        return uncertain ? Result<bool>::failure(application::result::ServiceErrorType::CommitUncertain, "fixture uncertain") : Result<bool>::success(true);
    }
    // 云端失败不更改本地文件或下载状态。
    Result<bool> failCloud(const model::MetadataCoverRecord &target, const model::CloudFileResult &error) override
    {
        std::lock_guard lock(local->mutex);
        auto &row = local->rows.at({target.owner, target.resource_id});
        if (row.cloud.status != 1 || row.cloud.claim_generation != target.cloud.claim_generation) return Result<bool>::success(false);
        row.cloud.status = error.uncertain ? 4 : 3;
        row.cloud.result_code = error.result_code;
        return Result<bool>::success(true);
    }
    // 测试通过调用明确推进恢复，不依赖睡眠。
    Result<int> recoverCloud(const model::MetadataCoverQuery &query, int) override
    {
        std::lock_guard lock(local->mutex);
        int count = 0;
        for (auto &[key, row] : local->rows)
            if (row.owner == query.owner && row.cloud.status == 1) {
                ++row.cloud.claim_generation;
                row.cloud.status = 4;
                ++count;
            }
        return Result<int>::success(count);
    }
};

// 共用存储 port 替身，测试编排不绑定某个云厂商或网络。
class CloudStore final : public ports::CloudFileStore {
public:
    int calls = 0;                       // 上传调用次数
    std::string provider = "baidupcs";   // 所选后端
    std::string root = "/Media/";        // 所选路径
    bool fail = false;                   // 模拟已知失败
    bool unknown = false;                // 模拟远端结果未知
    bool bad_receipt = false;            // 模拟错误文件回执
    std::function<void()> during_upload; // 在上传期间触发竞态
    std::string backend() const override
    {
        return provider;
    } // 当前后端
    std::string prefix() const override
    {
        return root;
    } // 当前目标根
    // 返回固定结果，或由回调在上传中替换记录。
    model::CloudFileResult upload(model::CloudFileUploadRequest request) override
    {
        ++calls;
        if (during_upload) during_upload();
        if (fail || unknown) return infrastructure::storage::cloud::failure("fixture_cloud_failure", "network", unknown);
        model::CloudFileResult result;
        result.ok = true;
        result.receipt = {provider, root + request.relative_path, request.bytes.size(), bad_receipt ? std::string(64, '0') : request.checksum};
        return result;
    }
};

} // namespace orchestrator::modules::crawler::tests::cover
