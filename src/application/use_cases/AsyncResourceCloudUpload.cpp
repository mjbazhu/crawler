#include "crawler/application/use_cases/AsyncSongResourceDownloadWorkflow.h"

#include "ResourceDownloadSupport.h"
#include "ResourceDownloadState.h"

namespace orchestrator::modules::crawler::application::use_cases {
using namespace resource_detail; // 本用例内部的纯映射和事件格式规则

application::async::Task<void> AsyncSongResourceDownloadWorkflow::uploadBatchCloudObjects(std::shared_ptr<ProcessRunState> state, const std::string &batch_id, SongResourceProcessListener *listener)
{
    if (!state || !batch_cloud_uploader_ || !resource_queue_) {
        co_return;
    }

    std::vector<application::model::SongResourceProcessItem> items;
    {
        std::lock_guard<std::mutex> lock(state->result_mutex);
        for (const auto &item : state->result.items) {
            if (item.ok && !item.local_path.empty() && item.cloud_path.empty()) {
                items.push_back(item);
            }
        }
    }
    if (items.empty()) {
        co_return;
    }

    application::model::SongResourceBatchCloudUploadRequest request;
    request.batch_id = batch_id;
    request.timeout_ms = state->options.resource_download_timeout_ms;
    request.objects.reserve(items.size());
    for (const auto &item : items) {
        application::model::SongResourceBatchCloudUploadObject object;
        object.resource_id = item.resource_id;
        object.local_path = item.local_path;
        object.size_bytes = item.size_bytes;
        object.checksum = item.checksum;
        request.objects.push_back(std::move(object));
    }

    co_await notifyBatchEvent(listener, "resourceBatchCloudUploadStarted", "{\"stage\":\"resource_batch_cloud_upload_started\",\"object_count\":" + std::to_string(request.objects.size()) + "}");
    auto uploaded = co_await batch_cloud_uploader_->uploadBatch(std::move(request));
    if (!uploaded.ok) {
        const std::string category = uploaded.error_category.empty() ? "persistence" : uploaded.error_category;
        const std::string code = uploaded.result_code.empty() ? "cloud_upload_failed" : uploaded.result_code;
        const std::string message = uploaded.message.empty() ? "resource batch cloud upload failed" : trimMessage(uploaded.message);
        co_await notifyBatchEvent(listener, "resourceBatchCloudUploadFailed", "{\"stage\":\"resource_batch_cloud_upload_failed\",\"result_code\":\"" + escapeJson(code) + "\",\"error_category\":\"" + escapeJson(category) + "\",\"message\":\"" + escapeJson(message) + "\"}");
        for (const auto &item : items) {
            application::model::SongResourceFailureUpdate update;
            update.resource_id = item.resource_id;
            update.error_category = category;
            update.result_code = code;
            update.message = message;
            update.batch_id = batch_id;
            (void)co_await resource_queue_->markFailed(std::move(update));
        }
        {
            std::lock_guard<std::mutex> lock(state->result_mutex);
            for (auto &item : state->result.items) {
                if (item.ok && !item.local_path.empty() && item.cloud_path.empty()) {
                    item.ok = false;
                    item.error_category = category;
                    item.result_code = code;
                    item.message = message;
                }
            }
        }
        co_return;
    }

    int writeback_count = 0;
    for (const auto &uploaded_object : uploaded.objects) {
        auto it = std::find_if(items.begin(), items.end(), [&uploaded_object](const application::model::SongResourceProcessItem &item) {
            return item.resource_id == uploaded_object.resource_id && item.local_path == uploaded_object.local_path;
        });
        if (it == items.end()) {
            continue;
        }
        application::model::SongResourceSuccessUpdate update;
        update.resource_id = it->resource_id;
        update.local_path = it->local_path;
        update.cloud_backend = uploaded.backend;
        update.cloud_path = uploaded_object.cloud_path;
        update.size_bytes = it->size_bytes;
        update.checksum = it->checksum;
        update.result_code = "ok";
        update.batch_id = batch_id;
        if (co_await resource_queue_->markSucceeded(std::move(update))) {
            ++writeback_count;
            std::lock_guard<std::mutex> lock(state->result_mutex);
            for (auto &item : state->result.items) {
                if (item.resource_id == uploaded_object.resource_id && item.local_path == uploaded_object.local_path) {
                    item.cloud_backend = uploaded.backend;
                    item.cloud_path = uploaded_object.cloud_path;
                    break;
                }
            }
        }
    }
    co_await notifyBatchEvent(listener, "resourceBatchCloudUploadCompleted", "{\"stage\":\"resource_batch_cloud_upload_completed\",\"object_count\":" + std::to_string(uploaded.objects.size()) + ",\"writeback_count\":" + std::to_string(writeback_count) + ",\"cloud_backend\":\"" + escapeJson(uploaded.backend) + "\"}");
}

} // namespace orchestrator::modules::crawler::application::use_cases
