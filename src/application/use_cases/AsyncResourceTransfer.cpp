#include "crawler/application/use_cases/AsyncSongResourceDownloadWorkflow.h"

#include "ResourceDownloadSupport.h"
#include "ResourceDownloadState.h"
#include <cctype>

namespace orchestrator::modules::crawler::application::use_cases {
using namespace resource_detail; // 本用例内部的纯映射和事件格式规则

application::async::Task<application::model::SongResourceProcessItem> AsyncSongResourceDownloadWorkflow::downloadAndStoreAttempt(application::model::SongResourceQueueTarget target, std::string remote_url, const std::string &batch_id, const application::model::SongResourceProcessOptions &options, bool write_failure, SongResourceProcessListener *listener)
{
    if (remote_copier_) {
        auto path = path_planner_.plan(toPathRequest(target, remote_url, ""));
        if (!path.ok) {
            const std::string message = path.error.empty() ? "resource remote path planning failed" : path.error;
            co_await notifyItemEvent(listener, makeStartedItem(target, isRelatedResource(target.resource_kind)), "resourcePathPlanFailed", stagePayload(target, "resource_path_plan_failed", "\"result_code\":\"path_plan_failed\",\"message\":\"" + escapeJson(message) + "\""));
            co_return write_failure ? co_await fail(std::move(target), "persistence", "path_plan_failed", message, batch_id, listener) : makeItem(target, false, false, false, false, "persistence", "path_plan_failed", message);
        }
        co_await notifyItemEvent(listener, makeStartedItem(target, isRelatedResource(target.resource_kind)), "resourcePathPlanned", stagePayload(target, "resource_path_planned", "\"local_path\":\"" + escapeJson(path.relative_path) + "\",\"extension\":\"" + escapeJson(path.extension) + "\""));

        application::model::SongResourceRemoteCopyRequest copy_request;
        copy_request.source_url = remote_url;
        copy_request.relative_path_hint = path.relative_path;
        copy_request.timeout_ms = options.resource_download_timeout_ms;
        co_await notifyItemEvent(listener, makeStartedItem(target, isRelatedResource(target.resource_kind)), "resourceRemoteCopyStarted", stagePayload(target, "resource_remote_copy_started", "\"local_path\":\"" + escapeJson(path.relative_path) + "\""));
        auto copied = co_await remote_copier_->copyUrl(std::move(copy_request));
        if (!copied.ok) {
            const std::string category = copied.error_category.empty() ? "persistence" : copied.error_category;
            const std::string code = copied.result_code.empty() ? "remote_copy_failed" : copied.result_code;
            const std::string message = copied.message.empty() ? "resource remote copy failed" : trimMessage(copied.message);
            co_await notifyItemEvent(listener, makeStartedItem(target, isRelatedResource(target.resource_kind)), "resourceRemoteCopyFailed", stagePayload(target, "resource_remote_copy_failed", "\"result_code\":\"" + escapeJson(code) + "\",\"error_category\":\"" + escapeJson(category) + "\",\"message\":\"" + escapeJson(message) + "\""));
            co_return write_failure ? co_await fail(std::move(target), category, code, message, batch_id, listener) : makeItem(target, false, false, false, false, category, code, message);
        }
        if (copied.local_path.empty() || copied.size_bytes == 0 || copied.checksum.empty()) {
            co_await notifyItemEvent(listener, makeStartedItem(target, isRelatedResource(target.resource_kind)), "resourceBlobValidationFailed", stagePayload(target, "resource_blob_validation_failed", "\"result_code\":\"persist_validation_failed\"" + std::string(",\"local_path\":\"") + escapeJson(copied.local_path) + "\"" + ",\"saved_size\":" + std::to_string(copied.size_bytes)));
            co_return write_failure ? co_await fail(std::move(target), "persistence", "persist_validation_failed", "resource remote copy result did not include a complete object identity", batch_id, listener) : makeItem(target, false, false, false, false, "persistence", "persist_validation_failed", "resource remote copy result did not include a complete object identity");
        }
        co_await notifyItemEvent(listener, makeStartedItem(target, isRelatedResource(target.resource_kind)), "resourceRemoteCopyCompleted", stagePayload(target, "resource_remote_copy_completed", "\"local_path\":\"" + escapeJson(copied.local_path) + "\"" + ",\"size_bytes\":" + std::to_string(copied.size_bytes) + ",\"checksum\":\"" + escapeJson(copied.checksum) + "\""));
        co_await notifyItemEvent(listener, makeStartedItem(target, isRelatedResource(target.resource_kind)), "resourceBlobSaved", stagePayload(target, "resource_blob_saved", "\"local_path\":\"" + escapeJson(copied.local_path) + "\"" + ",\"size_bytes\":" + std::to_string(copied.size_bytes) + ",\"checksum\":\"" + escapeJson(copied.checksum) + "\""));

        const std::string saved_path = copied.local_path;
        const std::uint64_t saved_size = copied.size_bytes;
        std::string saved_checksum = copied.checksum;
        application::model::SongResourceSuccessUpdate update;
        update.resource_id = target.resource_id;
        update.claim_generation = target.claim_generation;
        update.local_path = saved_path;
        update.size_bytes = saved_size;
        update.checksum = saved_checksum;
        update.result_code = "ok";
        update.batch_id = batch_id;
        if (!resource_queue_ || !(co_await resource_queue_->markSucceeded(std::move(update)))) {
            co_await notifyItemEvent(listener, makeStartedItem(target, isRelatedResource(target.resource_kind)), "resourceDbWritebackFailed", stagePayload(target, "resource_db_writeback_failed", "\"result_code\":\"writeback_failed\",\"local_path\":\"" + escapeJson(saved_path) + "\""));
            co_return write_failure ? co_await fail(std::move(target), "persistence", "writeback_failed", "resource success writeback failed", batch_id, listener) : makeItem(target, false, false, false, false, "persistence", "writeback_failed", "resource success writeback failed");
        }
        co_await notifyItemEvent(listener, makeStartedItem(target, isRelatedResource(target.resource_kind)), "resourceDbWritebackCompleted", stagePayload(target, "resource_db_writeback_completed", "\"local_path\":\"" + escapeJson(saved_path) + "\",\"size_bytes\":" + std::to_string(saved_size)));

        auto item = makeItem(target, true, false, false, false, {}, "ok", {});
        item.local_path = saved_path;
        item.size_bytes = saved_size;
        item.checksum = std::move(saved_checksum);
        co_return item;
    }

    if (!content_fetcher_ || !blob_store_) {
        co_return write_failure ? co_await fail(std::move(target), "invalid_target", "missing_dependency", "content fetcher or blob store is not configured", batch_id, listener) : makeItem(target, false, false, false, false, "invalid_target", "missing_dependency", "content fetcher or blob store is not configured");
    }

    application::model::SongResourceContentFetchRequest fetch_request;
    fetch_request.url = remote_url;
    fetch_request.timeout_ms = options.resource_download_timeout_ms;
    co_await notifyItemEvent(listener, makeStartedItem(target, isRelatedResource(target.resource_kind)), "resourceContentDownloadStarted", stagePayload(target, "resource_content_download_started"));

    struct WriterState {
        std::unique_ptr<application::ports::AsyncBlobWriteSession> writer; // 异步写入会话
        application::model::SongResourcePathResult path;                   // 规划后的路径
        std::string content_type;                                          // 响应 Content-Type
        std::string failure_category;                                      // 下游失败分类
        std::string failure_code;                                          // 下游失败编码
        std::string failure_message;                                       // 下游失败说明
        std::chrono::steady_clock::time_point last_cancel_check {};        // 每秒检查一次取消，避免每块查询数据库
    };
    auto writer_state = std::make_shared<WriterState>();

    application::ports::AsyncSongResourceContentStreamCallbacks callbacks;
    callbacks.on_response = [this, writer_state, target, remote_url, listener](const application::model::SongResourceContentStreamInfo &info) mutable -> application::async::Task<bool> {
        if (info.status_code < 200 || info.status_code >= 300) {
            writer_state->failure_category = "resource";
            writer_state->failure_code = "http_status_" + std::to_string(info.status_code);
            writer_state->failure_message = "resource download returned non-success status";
            co_return false;
        }
        auto media_type = info.content_type.substr(0, info.content_type.find(';'));
        std::transform(media_type.begin(), media_type.end(), media_type.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        if (media_type == "text/html" || media_type == "application/json") {
            writer_state->failure_category = "resource";
            writer_state->failure_code = "invalid_resource_content_type";
            writer_state->failure_message = "resource endpoint returned an error document";
            co_return false;
        }
        writer_state->content_type = info.content_type;
        writer_state->path = path_planner_.plan(toPathRequest(target, remote_url, writer_state->content_type));
        if (!writer_state->path.ok) {
            writer_state->failure_category = "persistence";
            writer_state->failure_code = "path_plan_failed";
            writer_state->failure_message = writer_state->path.error.empty() ? "resource local path planning failed" : writer_state->path.error;
            co_await notifyItemEvent(listener, makeStartedItem(target, isRelatedResource(target.resource_kind)), "resourcePathPlanFailed", stagePayload(target, "resource_path_plan_failed", "\"result_code\":\"path_plan_failed\",\"message\":\"" + escapeJson(writer_state->failure_message) + "\""));
            co_return false;
        }
        co_await notifyItemEvent(listener, makeStartedItem(target, isRelatedResource(target.resource_kind)), "resourcePathPlanned", stagePayload(target, "resource_path_planned", "\"local_path\":\"" + escapeJson(writer_state->path.relative_path) + "\",\"extension\":\"" + escapeJson(writer_state->path.extension) + "\""));

        application::model::SongResourceBlobStreamSaveRequest save_request;
        save_request.resource_id = target.resource_id;
        save_request.claim_generation = target.claim_generation;
        save_request.previous_local_path = target.local_path;
        save_request.require_database_ack = true;
        save_request.relative_path_hint = writer_state->path.relative_path;
        save_request.content_type = writer_state->content_type;
        writer_state->writer = co_await blob_store_->open(std::move(save_request));
        if (!writer_state->writer) {
            writer_state->failure_category = "persistence";
            writer_state->failure_code = "persist_failed";
            writer_state->failure_message = "resource blob stream open failed";
            co_await notifyItemEvent(listener, makeStartedItem(target, isRelatedResource(target.resource_kind)), "resourceBlobSaveFailed", stagePayload(target, "resource_blob_save_failed", "\"result_code\":\"persist_failed\",\"message\":\"" + escapeJson(writer_state->failure_message) + "\""));
            co_return false;
        }
        co_await notifyItemEvent(listener, makeStartedItem(target, isRelatedResource(target.resource_kind)), "resourceBlobSaveStarted", stagePayload(target, "resource_blob_save_started", "\"local_path\":\"" + escapeJson(writer_state->path.relative_path) + "\""));
        co_return true;
    };
    callbacks.on_chunk = [this, writer_state, listener](std::span<const char> chunk) mutable -> application::async::Task<bool> {
        const auto now = std::chrono::steady_clock::now();
        if (listener && now - writer_state->last_cancel_check >= std::chrono::seconds(1)) {
            writer_state->last_cancel_check = now;
            if (co_await runtime_->postBlocking<bool>([listener] { return listener->isCancellationRequested(); })) {
                writer_state->failure_category = "cancelled";
                writer_state->failure_code = "cancelled";
                writer_state->failure_message = "resource batch cancelled";
                co_return false;
            }
        }
        if (!writer_state->writer) {
            writer_state->failure_category = "persistence";
            writer_state->failure_code = "persist_failed";
            writer_state->failure_message = "resource blob stream was not opened";
            co_return false;
        }
        auto written = co_await writer_state->writer->write(chunk);
        if (!written.ok) {
            writer_state->failure_category = "persistence";
            writer_state->failure_code = "persist_failed";
            writer_state->failure_message = written.error.empty() ? "resource blob stream write failed" : written.error;
            co_return false;
        }
        co_return true;
    };

    auto content = co_await content_fetcher_->streamSongResourceContent(std::move(fetch_request), std::move(callbacks));
    if (!content.ok) {
        if (writer_state->writer) {
            co_await writer_state->writer->abort();
            co_await notifyItemEvent(listener, makeStartedItem(target, isRelatedResource(target.resource_kind)), "resourceBlobAbortCompleted", stagePayload(target, "resource_blob_abort_completed"));
        }
        const std::string category = !writer_state->failure_category.empty() ? writer_state->failure_category : (content.error_category.empty() ? "network" : content.error_category);
        const std::string code = !writer_state->failure_code.empty() ? writer_state->failure_code : (content.result_code.empty() ? "download_failed" : content.result_code);
        const std::string message = !writer_state->failure_message.empty() ? writer_state->failure_message : (content.message.empty() ? "resource content download failed" : content.message);
        co_await notifyItemEvent(listener, makeStartedItem(target, isRelatedResource(target.resource_kind)), "resourceContentDownloadFailed", stagePayload(target, "resource_content_download_failed", "\"result_code\":\"" + escapeJson(code) + "\",\"error_category\":\"" + escapeJson(category) + "\""));
        co_return write_failure ? co_await fail(std::move(target), category, code, message, batch_id, listener) : makeItem(target, false, false, false, false, category, code, message);
    }
    co_await notifyItemEvent(listener, makeStartedItem(target, isRelatedResource(target.resource_kind)), "resourceContentDownloaded", stagePayload(target, "resource_content_downloaded", "\"status_code\":" + std::to_string(content.status_code) + ",\"content_type\":\"" + escapeJson(content.content_type) + "\"" + ",\"body_size\":" + std::to_string(content.bytes_received)));

    if (!writer_state->writer) {
        const std::string message = "resource blob stream was not opened";
        co_await notifyItemEvent(listener, makeStartedItem(target, isRelatedResource(target.resource_kind)), "resourceBlobSaveFailed", stagePayload(target, "resource_blob_save_failed", "\"result_code\":\"persist_failed\",\"message\":\"" + escapeJson(message) + "\""));
        co_return write_failure ? co_await fail(std::move(target), "persistence", "persist_failed", message, batch_id, listener) : makeItem(target, false, false, false, false, "persistence", "persist_failed", message);
    }

    co_await notifyItemEvent(listener, makeStartedItem(target, isRelatedResource(target.resource_kind)), "resourceBlobCommitStarted", stagePayload(target, "resource_blob_commit_started", "\"local_path\":\"" + escapeJson(writer_state->path.relative_path) + "\""));
    auto saved = co_await writer_state->writer->commit();
    if (!saved.ok) {
        const std::string message = saved.error.empty() ? "resource blob save failed" : saved.error;
        co_await notifyItemEvent(listener, makeStartedItem(target, isRelatedResource(target.resource_kind)), "resourceBlobSaveFailed", stagePayload(target, "resource_blob_save_failed", "\"result_code\":\"persist_failed\",\"message\":\"" + escapeJson(message) + "\""));
        co_return write_failure ? co_await fail(std::move(target), "persistence", "persist_failed", message, batch_id, listener) : makeItem(target, false, false, false, false, "persistence", "persist_failed", message);
    }
    if (saved.local_path.empty() || saved.size_bytes == 0 || saved.size_bytes != content.bytes_received || saved.checksum.empty()) {
        co_await writer_state->writer->abort();
        co_await notifyItemEvent(listener, makeStartedItem(target, isRelatedResource(target.resource_kind)), "resourceBlobValidationFailed", stagePayload(target, "resource_blob_validation_failed", "\"result_code\":\"persist_validation_failed\"" + std::string(",\"local_path\":\"") + escapeJson(saved.local_path) + "\"" + ",\"downloaded_size\":" + std::to_string(content.bytes_received) + ",\"saved_size\":" + std::to_string(saved.size_bytes)));
        co_return write_failure ? co_await fail(std::move(target), "persistence", "persist_validation_failed", "resource blob save result did not match downloaded content", batch_id, listener) : makeItem(target, false, false, false, false, "persistence", "persist_validation_failed", "resource blob save result did not match downloaded content");
    }
    co_await notifyItemEvent(listener, makeStartedItem(target, isRelatedResource(target.resource_kind)), "resourceBlobSaved", stagePayload(target, "resource_blob_saved", "\"local_path\":\"" + escapeJson(saved.local_path) + "\"" + ",\"size_bytes\":" + std::to_string(saved.size_bytes) + ",\"checksum\":\"" + escapeJson(saved.checksum) + "\""));

    std::string cloud_backend;
    std::string cloud_path;
    if (cloud_uploader_ && !batch_cloud_uploader_) {
        application::model::SongResourceCloudUploadRequest upload_request;
        upload_request.local_path = saved.local_path;
        upload_request.size_bytes = saved.size_bytes;
        upload_request.checksum = saved.checksum;
        upload_request.timeout_ms = options.resource_download_timeout_ms;
        co_await notifyItemEvent(listener, makeStartedItem(target, isRelatedResource(target.resource_kind)), "resourceCloudUploadStarted", stagePayload(target, "resource_cloud_upload_started", "\"local_path\":\"" + escapeJson(saved.local_path) + "\""));
        auto uploaded = co_await cloud_uploader_->upload(std::move(upload_request));
        if (!uploaded.ok) {
            const std::string category = uploaded.error_category.empty() ? "persistence" : uploaded.error_category;
            const std::string code = uploaded.result_code.empty() ? "cloud_upload_failed" : uploaded.result_code;
            const std::string message = uploaded.message.empty() ? "resource cloud upload failed" : trimMessage(uploaded.message);
            co_await notifyItemEvent(listener, makeStartedItem(target, isRelatedResource(target.resource_kind)), "resourceCloudUploadFailed", stagePayload(target, "resource_cloud_upload_failed", "\"result_code\":\"" + escapeJson(code) + "\",\"error_category\":\"" + escapeJson(category) + "\",\"message\":\"" + escapeJson(message) + "\""));
            co_return write_failure ? co_await fail(std::move(target), category, code, message, batch_id, listener) : makeItem(target, false, false, false, false, category, code, message);
        }
        if (uploaded.backend.empty() || uploaded.cloud_path.empty()) {
            co_await notifyItemEvent(listener, makeStartedItem(target, isRelatedResource(target.resource_kind)), "resourceCloudUploadValidationFailed", stagePayload(target, "resource_cloud_upload_validation_failed", "\"result_code\":\"cloud_upload_validation_failed\""));
            co_return write_failure ? co_await fail(std::move(target), "persistence", "cloud_upload_validation_failed", "resource cloud upload result did not include backend and path", batch_id, listener) : makeItem(target, false, false, false, false, "persistence", "cloud_upload_validation_failed", "resource cloud upload result did not include backend and path");
        }
        cloud_backend = std::move(uploaded.backend);
        cloud_path = std::move(uploaded.cloud_path);
        co_await notifyItemEvent(listener, makeStartedItem(target, isRelatedResource(target.resource_kind)), "resourceCloudUploadCompleted", stagePayload(target, "resource_cloud_upload_completed", "\"cloud_backend\":\"" + escapeJson(cloud_backend) + "\",\"cloud_path\":\"" + escapeJson(cloud_path) + "\""));
    }

    application::model::SongResourceSuccessUpdate update;
    update.resource_id = target.resource_id;
    update.claim_generation = target.claim_generation;
    update.local_path = saved.local_path;
    update.cloud_backend = cloud_backend;
    update.cloud_path = cloud_path;
    update.size_bytes = saved.size_bytes;
    update.checksum = saved.checksum;
    update.result_code = "ok";
    update.batch_id = batch_id;
    const auto receipt = co_await resource_queue_->confirmSucceeded(std::move(update));
    if (!receipt.isSuccess()) {
        co_await writer_state->writer->defer();
        co_return makeItem(target, false, false, false, false, "persistence", "writeback_uncertain", "resource writeback needs reconciliation");
    }
    if (!receipt.data() || !*receipt.data()) {
        co_await writer_state->writer->abort();
        co_await notifyItemEvent(listener, makeStartedItem(target, isRelatedResource(target.resource_kind)), "resourceDbWritebackFailed", stagePayload(target, "resource_db_writeback_failed", "\"result_code\":\"writeback_failed\",\"local_path\":\"" + escapeJson(saved.local_path) + "\""));
        co_return write_failure ? co_await fail(std::move(target), "persistence", "writeback_failed", "resource success writeback failed", batch_id, listener) : makeItem(target, false, false, false, false, "persistence", "writeback_failed", "resource success writeback failed");
    }
    co_await writer_state->writer->accept();
    co_await notifyItemEvent(listener, makeStartedItem(target, isRelatedResource(target.resource_kind)), "resourceDbWritebackCompleted", stagePayload(target, "resource_db_writeback_completed", "\"local_path\":\"" + escapeJson(saved.local_path) + "\",\"size_bytes\":" + std::to_string(saved.size_bytes)));

    auto item = makeItem(target, true, false, false, false, {}, "ok", {});
    item.local_path = std::move(saved.local_path);
    item.cloud_backend = std::move(cloud_backend);
    item.cloud_path = std::move(cloud_path);
    item.size_bytes = saved.size_bytes;
    item.checksum = std::move(saved.checksum);
    co_return item;
}

} // namespace orchestrator::modules::crawler::application::use_cases
