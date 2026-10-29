#include "crawler/application/use_cases/MetadataCoverDownloadWorkflow.h"

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

} // namespace

MetadataCoverDownloadWorkflow::MetadataCoverDownloadWorkflow(std::shared_ptr<ports::MetadataCoverQueue> queue, std::shared_ptr<ports::MetadataCoverStore> store, std::shared_ptr<ports::MetadataCoverReader> reader) : queue_(std::move(queue)), store_(std::move(store)), reader_(std::move(reader))
{}

model::MetadataCoverItem MetadataCoverDownloadWorkflow::process(model::MetadataCoverRecord target, const model::MetadataCoverOptions &options, const std::string &batch_id, std::function<bool()> cancelled)
{
    model::MetadataCoverItem item;
    item.record = target;
    const auto stopped = [&] { return cancelled && cancelled(); };
    if (stopped()) {
        item.record.file = failed("cancelled", "resource");
        return item;
    }
    if (!options.refresh && target.file.status == 2) {
        item.skipped = true;
        item.success = reader_ && reader_->available(target.file);
        if (!item.success) item.record.file = failed("local_file_missing");
        return item;
    }
    auto claimed = queue_->claim(target, options.refresh, batch_id);
    if (!claimed.isSuccess()) {
        item.record.file = failed("cover_claim_failed");
        return item;
    }
    if (!*claimed.data()) {
        item.skipped = true;
        item.record.file.result_code = "cover_not_claimed";
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
                item.record.file.result_code = "writeback_uncertain";
                item.record.file.error_category = "persistence";
                item.record.file.last_error = "cover writeback could not be confirmed";
                return item;
            }
            if (*confirmation.data()) {
                store_->accept(saved.local_path);
                item.success = true;
                item.record.file = std::move(saved);
                item.record.available = true;
                return item;
            }
            store_->discard(saved.local_path);
            saved = failed("cover_claim_changed");
        }
    } catch (const std::exception &) {
        if (saved.status == 2 && !saved.local_path.empty()) {
            store_->defer(saved.local_path);
            item.record.file = failed("writeback_uncertain");
            return item;
        }
        saved = failed("cover_processing_failed");
    }
    const auto ended = queue_->fail(target, saved);
    // 新文件失败时只更新错误，旧成功回执继续用于读取。
    item.record.file = target.file;
    item.record.file.status = target.file.local_path.empty() ? 3 : 2;
    item.record.file.error_category = saved.error_category;
    item.record.file.result_code = ended.isSuccess() ? saved.result_code : "cover_failure_writeback_failed";
    item.record.file.last_error = saved.last_error;
    item.record.available = reader_ && reader_->available(item.record.file);
    return item;
}

} // namespace orchestrator::modules::crawler::application::use_cases
