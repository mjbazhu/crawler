#include "crawler/application/use_cases/AsyncSongResourceDownloadWorkflow.h"

#include "ResourceDownloadSupport.h"
#include "ResourceDownloadState.h"

namespace orchestrator::modules::crawler::application::use_cases {
using namespace resource_detail; // 本用例内部的纯映射和事件格式规则

application::async::Task<application::model::SongResourceProcessItem> AsyncSongResourceDownloadWorkflow::fail(application::model::SongResourceQueueTarget target, std::string category, std::string code, std::string message, const std::string &batch_id, SongResourceProcessListener *listener)
{
    application::model::SongResourceFailureUpdate update;
    update.resource_id = target.resource_id;
    update.claim_generation = target.claim_generation;
    update.error_category = std::move(category);
    update.result_code = std::move(code);
    update.message = trimMessage(std::move(message));
    update.batch_id = batch_id;
    bool writeback_ok = false;
    if (resource_queue_) {
        writeback_ok = co_await resource_queue_->markFailed(update);
    }
    co_await notifyItemEvent(listener, makeStartedItem(target, isRelatedResource(target.resource_kind)), writeback_ok ? "resourceFailureWritebackCompleted" : "resourceFailureWritebackFailed", stagePayload(target, writeback_ok ? "resource_failure_writeback_completed" : "resource_failure_writeback_failed", "\"result_code\":\"" + escapeJson(update.result_code) + "\",\"error_category\":\"" + escapeJson(update.error_category) + "\""));
    co_return makeItem(target, false, false, false, false, update.error_category, update.result_code, update.message);
}

application::async::Task<application::model::SongResourceProcessItem> AsyncSongResourceDownloadWorkflow::skip(application::model::SongResourceQueueTarget target, std::string category, std::string code, std::string message, const std::string &batch_id, SongResourceProcessListener *listener)
{
    application::model::SongResourceSkipUpdate update;
    update.resource_id = target.resource_id;
    update.claim_generation = target.claim_generation;
    update.song_source_ref_id = target.song_source_ref_id;
    update.resource_kind.clear(); // 平台永久限制作用于整首歌，已完成资源由 SQL 保护
    update.format_type.clear();
    update.error_category = std::move(category);
    update.result_code = std::move(code);
    update.message = trimMessage(std::move(message));
    update.batch_id = batch_id;
    bool writeback_ok = false;
    if (resource_queue_) {
        writeback_ok = co_await resource_queue_->markSkipped(update);
    }
    co_await notifyItemEvent(listener, makeStartedItem(target, isRelatedResource(target.resource_kind)), writeback_ok ? "resourceSkipWritebackCompleted" : "resourceSkipWritebackFailed", stagePayload(target, writeback_ok ? "resource_skip_writeback_completed" : "resource_skip_writeback_failed", "\"result_code\":\"" + escapeJson(update.result_code) + "\",\"error_category\":\"" + escapeJson(update.error_category) + "\""));
    co_return makeItem(target, false, writeback_ok, false, false, writeback_ok ? update.error_category : "persistence", writeback_ok ? update.result_code : "skip_writeback_failed", update.message);
}

application::async::Task<void> AsyncSongResourceDownloadWorkflow::notifyBatchEvent(SongResourceProcessListener *listener, std::string event_type, std::string payload)
{
    if (!listener) {
        co_return;
    }
    try {
        co_await runtime_->postBlocking<void>([listener, event_type = std::move(event_type), payload = std::move(payload)]() mutable { listener->onBatchEvent(std::move(event_type), std::move(payload)); });
    } catch (...) {
    }
}

application::async::Task<void> AsyncSongResourceDownloadWorkflow::notifyItemStarted(SongResourceProcessListener *listener, application::model::SongResourceProcessItem item)
{
    if (!listener) {
        co_return;
    }
    try {
        co_await runtime_->postBlocking<void>([listener, item] { listener->onItemStarted(item); });
    } catch (...) {
    }
}

application::async::Task<void> AsyncSongResourceDownloadWorkflow::notifyItemFinished(SongResourceProcessListener *listener, application::model::SongResourceProcessItem item)
{
    if (!listener) {
        co_return;
    }
    try {
        co_await runtime_->postBlocking<void>([listener, item] { listener->onItemFinished(item); });
    } catch (...) {
    }
}

application::async::Task<void> AsyncSongResourceDownloadWorkflow::notifyItemEvent(SongResourceProcessListener *listener, application::model::SongResourceProcessItem item, std::string event_type, std::string payload)
{
    if (!listener) {
        co_return;
    }
    try {
        co_await runtime_->postBlocking<void>([listener, item, event_type = std::move(event_type), payload = std::move(payload)]() mutable { listener->onItemEvent(item, std::move(event_type), std::move(payload)); });
    } catch (...) {
    }
}

application::async::Task<void> AsyncSongResourceDownloadWorkflow::notifyPacmtokenRefreshed(SongResourceProcessListener *listener, bool refreshed, std::size_t pacmtoken_length, std::uint64_t updated_at)
{
    if (!listener) {
        co_return;
    }
    try {
        co_await runtime_->postBlocking<void>([listener, refreshed, pacmtoken_length, updated_at] { listener->onPacmtokenRefreshed(refreshed, pacmtoken_length, updated_at); });
    } catch (...) {
    }
}

} // namespace orchestrator::modules::crawler::application::use_cases
