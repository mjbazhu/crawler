#include "crawler/application/use_cases/MetadataCoverCloudWorkflow.h"

#include <algorithm>
#include <chrono>
#include <exception>
#include <stdexcept>
#include <utility>

namespace orchestrator::modules::crawler::application::use_cases {

MetadataCoverCloudWorkflow::MetadataCoverCloudWorkflow(std::shared_ptr<ports::MetadataCoverCloudQueue> queue, std::shared_ptr<ports::MetadataCoverReader> reader, std::shared_ptr<ports::CloudFileStore> store, int timeout_ms) : queue_(std::move(queue)), reader_(std::move(reader)), store_(std::move(store)), timeout_ms_((std::clamp)(timeout_ms, 100, 600000))
{
    if (!queue_ || !reader_ || !store_) throw std::invalid_argument("cover cloud dependencies are required");
}

bool MetadataCoverCloudWorkflow::matches(const model::MetadataCoverRecord &target) const
{
    const auto &cloud = target.cloud;
    return !target.file.local_path.empty() && target.file.size_bytes > 0 && target.file.checksum.size() == 64 && cloud.status == 2 && cloud.receipt.backend == store_->backend() && cloud.receipt.path == store_->prefix() + target.file.local_path && cloud.receipt.size_bytes == target.file.size_bytes && cloud.receipt.checksum == target.file.checksum;
}

void MetadataCoverCloudWorkflow::candidates(model::MetadataCoverQuery &query) const
{
    query.cloud_candidates = true;
    query.cloud_backend = store_->backend();
    query.cloud_prefix = store_->prefix();
}

model::MetadataCoverItem MetadataCoverCloudWorkflow::process(model::MetadataCoverRecord target, int retries, const std::string &batch_id, std::function<bool()> cancelled)
{
    model::MetadataCoverItem item;
    item.record = target;
    item.record.cloud.enabled = true;
    const auto fail = [&](std::string code, std::string category = "persistence") {
        item.result_code = std::move(code);
        item.error_category = std::move(category);
        item.last_error = "metadata cover cloud processing failed";
        return item;
    };
    const auto current = queue_->find(target.owner, target.resource_id);
    if (!current.isSuccess()) return fail("cloud_queue_read_failed");
    if (!*current.data()) return fail("cloud_resource_missing");
    target = **current.data();
    target.cloud.enabled = true;
    target.available = reader_->available(target.file);
    item.record = target;
    if (cancelled && cancelled()) return fail("cancelled", "resource");
    if (target.file.status != 2) return fail("cloud_local_not_ready");
    auto content = reader_->read(target.file);
    item.record.available = content.has_value();
    if (!content) return fail("local_file_missing");
    if (matches(target)) {
        item.success = item.skipped = true;
        item.record.cloud.current = true;
        return item;
    }
    const model::CloudFileReceipt planned {store_->backend(), store_->prefix() + target.file.local_path, target.file.size_bytes, target.file.checksum};
    auto claimed = queue_->claimCloud(target, planned, batch_id);
    if (!claimed.isSuccess()) return fail("cloud_claim_failed");
    if (!*claimed.data()) {
        item.skipped = true;
        return fail("cover_not_claimed", "resource");
    }
    target = **claimed.data();
    target.cloud.enabled = true;
    item.record = target;
    model::CloudFileResult uploaded;
    try {
        for (int attempt = 0; attempt <= (std::clamp)(retries, 0, 5); ++attempt) {
            if (cancelled && cancelled()) {
                uploaded.result_code = "cancelled";
                uploaded.error_category = "resource";
                break;
            }
            uploaded = store_->upload({target.file.local_path, content->bytes, target.file.checksum, timeout_ms_, cancelled});
            if (uploaded.ok || (!uploaded.uncertain && uploaded.error_category != "network" && uploaded.error_category != "timeout")) break;
        }
    } catch (const std::exception &) {
        uploaded.uncertain = true;
        uploaded.error_category = "persistence";
        uploaded.result_code = "cloud_processing_uncertain";
    }
    if (uploaded.ok && (uploaded.receipt.backend != planned.backend || uploaded.receipt.path != planned.path || uploaded.receipt.size_bytes != planned.size_bytes || uploaded.receipt.checksum != planned.checksum)) {
        uploaded.ok = false;
        uploaded.result_code = "cloud_receipt_mismatch";
        uploaded.error_category = "persistence";
    }
    if (uploaded.ok) {
        const auto written = queue_->succeedCloud(target, uploaded.receipt);
        if (written.isSuccess() && *written.data()) {
            item.success = true;
            item.record.cloud.receipt = std::move(uploaded.receipt);
            item.record.cloud.status = 2;
            item.record.cloud.uploaded_at = static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
            item.record.cloud.claimed_at = 0;
            item.record.cloud.running_batch_id.clear();
            item.record.cloud.error_category.clear();
            item.record.cloud.last_error.clear();
            item.record.cloud.current = true;
            item.record.cloud.result_code = "ok";
            item.result_code = "ok";
            item.record.available = true;
            return item;
        }
        uploaded.uncertain = !written.isSuccess();
        uploaded.result_code = written.isSuccess() ? "cloud_claim_changed" : "cloud_writeback_uncertain";
        uploaded.error_category = "persistence";
    }
    const auto ended = queue_->failCloud(target, uploaded);
    item.record.cloud.status = uploaded.uncertain ? 4 : 3;
    item.record.cloud.error_category = uploaded.error_category;
    item.record.cloud.result_code = uploaded.result_code;
    item.record.cloud.last_error = "cover cloud operation was not confirmed";
    item.record.available = reader_->available(target.file);
    // 条件回写可能被刷新或新 worker 拒绝，响应应反映数据库当前快照。
    const auto latest = queue_->find(target.owner, target.resource_id);
    if (latest.isSuccess() && latest.data()->has_value()) {
        item.record = **latest.data();
        item.record.cloud.enabled = true;
        item.record.cloud.current = matches(item.record);
        item.record.available = reader_->available(item.record.file);
    }
    return fail(ended.isSuccess() ? uploaded.result_code : "cloud_failure_writeback_failed", uploaded.error_category);
}

result::ServiceResult<int> MetadataCoverCloudWorkflow::recover(const model::MetadataCoverQuery &query, int stale_seconds)
{
    return queue_->recoverCloud(query, stale_seconds);
}

} // namespace orchestrator::modules::crawler::application::use_cases
