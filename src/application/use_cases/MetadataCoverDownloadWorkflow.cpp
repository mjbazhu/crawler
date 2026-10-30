#include "crawler/application/use_cases/MetadataCoverDownloadWorkflow.h"
#include "crawler/application/use_cases/ResourceTransferPreflight.h"

#include <algorithm>
#include <exception>
#include <utility>

namespace orchestrator::modules::crawler::application::use_cases {
namespace {

// 错误固定为脱敏的稳定结果，不能转发 HTTP 或数据库异常正文。
model::MetadataResourceFile failed(std::string code, std::string category = "persistence")
{
    model::MetadataResourceFile file;
    file.status = 3;
    file.error_category = std::move(category);
    file.result_code = std::move(code);
    file.last_error = "metadata cover processing failed";
    return file;
}

// 只重试传输暂态失败，内容错误与持久化错误直接结束。
bool retryable(const model::MetadataResourceFile &file)
{
    return file.result_code == "network_failed" || file.result_code == "timeout" || file.result_code == "http_429" || file.result_code.starts_with("http_5");
}

// 本次失败与持久化快照分开；旧 worker 不以自己的旧记录伪造当前资源状态。
void reloadRecord(ports::MetadataCoverQueue &queue, ports::MetadataCoverReader *reader, model::MetadataCoverItem &item)
{
    const auto current = queue.find(item.record.owner, item.record.resource_id);
    if (current.isSuccess() && current.data()->has_value()) item.record = **current.data();
    item.record.available = reader && reader->available(item.record.file);
}

// 错误描述属于本次操作，不替换仍可用于读取的最后成功文件回执。
void setFailure(model::MetadataCoverItem &item, std::string code, std::string category = "persistence")
{
    item.result_code = std::move(code);
    item.error_category = std::move(category);
    item.last_error = "metadata cover processing failed";
}

} // namespace

MetadataCoverDownloadWorkflow::MetadataCoverDownloadWorkflow(std::shared_ptr<ports::MetadataCoverQueue> queue, std::shared_ptr<ports::MetadataCoverStore> store, std::shared_ptr<ports::MetadataCoverReader> reader, std::shared_ptr<ports::ResourceTransferAccess> access) : queue_(std::move(queue)), store_(std::move(store)), reader_(std::move(reader)), access_(std::move(access))
{}

model::MetadataCoverItem MetadataCoverDownloadWorkflow::process(model::MetadataCoverRecord target, const model::MetadataCoverOptions &options, const std::string &batch_id, std::function<bool()> cancelled)
{
    model::MetadataCoverItem item;
    item.record = target;
    const auto stopped = [&] { return cancelled && cancelled(); };
    const auto checked = preflightResourceTransfer(access_.get(), {target.owner == model::CoverOwner::Singer ? model::ResourceOwner::Singer : model::ResourceOwner::Album, target.resource_id}, model::ResourceDestination::Local, stopped());
    if (!checked.isSuccess()) {
        setFailure(item, "resource_admission_failed");
        return item;
    }
    if (checked.data()->decision != ResourceWorkDecision::Transfer) {
        item.skipped = checked.data()->decision == ResourceWorkDecision::SkipPolicy || checked.data()->decision == ResourceWorkDecision::Restricted;
        item.result_code = checked.data()->result_code;
        if (!item.skipped) setFailure(item, item.result_code, "resource");
        return item;
    }
    if (!options.refresh && target.file.status == 2) {
        // 复用属于处理成功，必须验证内容与摘要，不能仅凭同大小文件存在。
        item.record.available = reader_ && reader_->read(target.file).has_value();
        item.success = item.skipped = item.record.available;
        if (!item.success) setFailure(item, "local_file_missing");
        return item;
    }
    auto claimed = queue_->claim(target, options.refresh, batch_id);
    if (!claimed.isSuccess()) {
        setFailure(item, "cover_claim_failed");
        reloadRecord(*queue_, reader_.get(), item);
        return item;
    }
    if (!*claimed.data()) {
        item.skipped = true;
        setFailure(item, "cover_not_claimed", "resource");
        reloadRecord(*queue_, reader_.get(), item);
        return item;
    }
    target = **claimed.data();
    item.record = target;
    model::MetadataResourceFile saved;
    try {
        const int retries = (std::clamp)(options.retries, 0, 5);
        for (int attempt = 0; attempt <= retries; ++attempt) {
            if (stopped()) {
                saved = failed("cancelled", "resource");
                break;
            }
            saved = store_->save(model::MetadataCoverSaveRequest {target, options.timeout_ms, cancelled});
            if (saved.status == 2 || !retryable(saved)) break;
        }
        if (saved.status == 2 && stopped()) {
            store_->discard(saved.local_path);
            saved = failed("cancelled", "resource");
        }
        if (saved.status == 2) {
            const auto confirmation = queue_->succeed(target, saved);
            if (!confirmation.isSuccess()) {
                store_->defer(saved.local_path);
                setFailure(item, "writeback_uncertain");
                reloadRecord(*queue_, reader_.get(), item);
                return item;
            }
            if (*confirmation.data()) {
                store_->accept(saved.local_path);
                item.success = true;
                item.record.file = std::move(saved);
                // 本地换图使旧云端运行态失效，但保留最后确认的对象回执。
                auto previous_cloud = std::move(item.record.cloud);
                item.record.cloud = {};
                item.record.cloud.receipt = std::move(previous_cloud.receipt);
                item.record.cloud.uploaded_at = previous_cloud.uploaded_at;
                item.record.cloud.claim_generation = previous_cloud.claim_generation + 1;
                reloadRecord(*queue_, reader_.get(), item);
                item.result_code = "ok";
                return item;
            }
            store_->discard(saved.local_path);
            saved = failed("cover_claim_changed");
        }
    } catch (const std::exception &) {
        if (saved.status == 2 && !saved.local_path.empty()) {
            store_->defer(saved.local_path);
            setFailure(item, "writeback_uncertain");
            reloadRecord(*queue_, reader_.get(), item);
            return item;
        }
        saved = failed("cover_processing_failed");
    }
    const auto ended = queue_->fail(target, saved);
    setFailure(item, ended.isSuccess() ? saved.result_code : "cover_failure_writeback_failed", saved.error_category);
    reloadRecord(*queue_, reader_.get(), item);
    return item;
}

} // namespace orchestrator::modules::crawler::application::use_cases
