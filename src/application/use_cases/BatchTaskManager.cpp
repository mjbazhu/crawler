#include "crawler/application/use_cases/BatchTaskManager.h"

#include <algorithm>
#include <atomic>
#include <ctime>
#include <sstream>
#include <string_view>

namespace orchestrator::modules::crawler::application::use_cases {

namespace {

constexpr int kMaxErrorLogs = 50;

// 生成简单批量任务 ID。
std::string generateBatchId()
{
    static std::atomic<std::uint64_t> sequence {0};
    auto now = std::chrono::system_clock::now();
    auto now_ms = std::chrono::time_point_cast<std::chrono::milliseconds>(now);
    std::stringstream ss;
    ss << "batch_" << now_ms.time_since_epoch().count() << '_' << sequence.fetch_add(1);
    return ss.str();
}

// 将任务状态转换为 API 快照中的稳定字符串。
std::string toStateString(application::model::BatchState state)
{
    switch (state) {
    case application::model::BatchState::Pending: return "Pending";
    case application::model::BatchState::Running: return "Running";
    case application::model::BatchState::Completed: return "Completed";
    case application::model::BatchState::Failed: return "Failed";
    case application::model::BatchState::Cancelled: return "Cancelled";
    }
    return "Failed";
}

// 判断状态是否表示任务已结束。
bool isFinished(application::model::BatchState state)
{
    return state == application::model::BatchState::Completed || state == application::model::BatchState::Failed || state == application::model::BatchState::Cancelled;
}

// 当前 Unix 秒时间戳。
std::uint64_t nowSeconds()
{
    return static_cast<std::uint64_t>(std::time(nullptr));
}

// 最小 JSON 字符串转义，仅用于批次事件 payload。
std::string escapeJson(std::string_view value)
{
    std::string escaped;
    escaped.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
        case '\\': escaped += "\\\\"; break;
        case '"': escaped += "\\\""; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped.push_back(ch); break;
        }
    }
    return escaped;
}

// 生成批次 item 事件的最小上下文，不记录外部 URL、签名或敏感 header。
std::string itemEventPayload(const application::model::CrawlBatchItemRecord &item, std::string_view stage, int written_count, const std::string &error, const std::string &error_category, const std::string &result_code)
{
    const std::string target_id = !item.source_singer_id.empty() ? item.source_singer_id : (!item.source_album_id.empty() ? item.source_album_id : item.item_key);
    std::stringstream payload;
    payload << "{\"target_id\":\"" << escapeJson(target_id)
            << "\",\"stage\":\"" << escapeJson(stage)
            << "\",\"written_count\":" << (std::max)(0, written_count);
    if (!error.empty()) {
        payload << ",\"error\":\"" << escapeJson(error) << "\"";
    }
    if (!error_category.empty()) {
        payload << ",\"error_category\":\"" << escapeJson(error_category) << "\"";
    }
    if (!result_code.empty()) {
        payload << ",\"result_code\":\"" << escapeJson(result_code) << "\"";
    }
    payload << '}';
    return payload.str();
}

} // namespace

BatchTaskManager::BatchTaskManager(
    std::shared_ptr<application::ports::TaskExecutor> batch_executor,
    std::shared_ptr<application::ports::DiagnosticsSink> diagnostics,
    std::shared_ptr<application::ports::BatchTaskRepository> batch_repository,
    BatchConcurrencyReader max_batch_concurrency,
    TimeoutReader task_timeout
) : batch_runner_(std::move(batch_executor), std::move(diagnostics)),
    batch_repository_(std::move(batch_repository)),
    max_batch_concurrency_(std::move(max_batch_concurrency)),
    task_timeout_(std::move(task_timeout))
{}

std::string BatchTaskManager::start(
    const std::vector<application::model::CrawlBatchItemRecord> &items,
    int normalized_limit,
    std::string task_type,
    std::string request_params,
    std::string settings_snapshot,
    std::string parent_batch_id,
    TargetHandler handler,
    bool allow_empty
)
{
    cleanupFinishedTasks();

    const std::string batch_id = generateBatchId();
    auto progress = std::make_shared<application::model::BatchProgress>();
    progress->batch_id = batch_id;
    progress->total_items = static_cast<int>(items.size());
    const bool empty_success = items.empty() && allow_empty;
    progress->state = items.empty() ? (empty_success ? application::model::BatchState::Completed : application::model::BatchState::Failed) : application::model::BatchState::Running;

    std::vector<application::model::CrawlBatchItemRecord> persisted_items = items;
    const std::uint64_t now = nowSeconds();
    for (auto &item : persisted_items) {
        item.batch_id = batch_id;
        item.state = "Pending";
        item.created_at = now;
        item.updated_at = now;
    }

    if (batch_repository_) {
        application::model::CrawlBatchRecord batch;
        batch.batch_id = batch_id;
        batch.source = "migu";
        batch.task_type = std::move(task_type);
        batch.state = items.empty() ? (empty_success ? "Completed" : "Failed") : "Running";
        batch.request_params = request_params.empty() ? "{}" : std::move(request_params);
        batch.settings_snapshot = settings_snapshot.empty() ? "{}" : std::move(settings_snapshot);
        batch.parent_batch_id = std::move(parent_batch_id);
        batch.limit_count = normalized_limit;
        batch.target_count = static_cast<int>(persisted_items.size());
        batch.created_at = now;
        batch.started_at = now;
        batch.updated_at = now;
        if (persisted_items.empty()) {
            batch.finished_at = now;
            if (!empty_success) batch.last_error = "targets cannot be empty";
        }
        (void)batch_repository_->createBatch(batch, persisted_items);
        (void)batch_repository_->appendEvent(application::model::CrawlBatchEventRecord {batch_id, std::nullopt, empty_success ? "batchCompleted" : "batchStarted", empty_success ? "No pending targets" : "Batch task started", "{}", now});
    }

    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        batch_tasks_[batch_id] = progress;
    }

    if (persisted_items.empty()) {
        return batch_id;
    }

    if (!batch_runner_.configured()) {
        progress->state = application::model::BatchState::Failed;
        appendError(progress, "Batch executor is not configured");
        if (batch_repository_) {
            const std::uint64_t failed_at = nowSeconds();
            application::model::CrawlBatchRecord batch;
            batch.batch_id = batch_id;
            batch.state = "Failed";
            batch.target_count = progress->total_items;
            batch.last_error = "Batch executor is not configured";
            batch.finished_at = failed_at;
            batch.updated_at = failed_at;
            (void)batch_repository_->updateBatchProgress(batch);
            (void)batch_repository_->appendEvent(application::model::CrawlBatchEventRecord {batch_id, std::nullopt, "batchFailed", "Batch executor is not configured", "{}", failed_at});
        }
        return batch_id;
    }

    auto item_list = std::make_shared<std::vector<application::model::CrawlBatchItemRecord>>(std::move(persisted_items));
    auto next_index = std::make_shared<std::atomic<std::size_t>>(0);
    const int configured_concurrency = max_batch_concurrency_ ? max_batch_concurrency_() : 1;
    const int worker_count = (std::min)((std::max)(1, configured_concurrency), static_cast<int>(item_list->size()));

    for (int worker = 0; worker < worker_count; ++worker) {
        const bool submitted = batch_runner_.submitDetached(
            [this, progress, item_list, next_index, normalized_limit, handler]() {
                for (;;) {
                    if (progress->cancel_requested.load()) {
                        progress->state = application::model::BatchState::Cancelled;
                        if (batch_repository_) {
                            application::model::CrawlBatchRecord batch;
                            batch.batch_id = progress->batch_id;
                            batch.state = "Cancelled";
                            batch.target_count = progress->total_items;
                            batch.processed_count = progress->processed_count;
                            batch.success_count = progress->success_count;
                            batch.failure_count = progress->failure_count;
                            batch.cancel_requested = true;
                            batch.finished_at = nowSeconds();
                            batch.updated_at = batch.finished_at;
                            (void)batch_repository_->updateBatchProgress(batch);
                        }
                        return;
                    }

                    if (isTaskTimedOut(progress)) {
                        progress->cancel_requested = true;
                        progress->state = application::model::BatchState::Failed;
                        appendError(progress, "Task timed out");
                        if (batch_repository_) {
                            const std::uint64_t event_time = nowSeconds();
                            application::model::CrawlBatchRecord batch;
                            batch.batch_id = progress->batch_id;
                            batch.state = "Failed";
                            batch.target_count = progress->total_items;
                            batch.processed_count = progress->processed_count;
                            batch.success_count = progress->success_count;
                            batch.failure_count = progress->failure_count;
                            batch.cancel_requested = true;
                            batch.last_error = "Task timed out";
                            batch.finished_at = event_time;
                            batch.updated_at = event_time;
                            (void)batch_repository_->updateBatchProgress(batch);
                            (void)batch_repository_->appendEvent(application::model::CrawlBatchEventRecord {progress->batch_id, std::nullopt, "timeout", "Task timed out", "{}", event_time});
                        }
                        return;
                    }

                    const std::size_t index = next_index->fetch_add(1);
                    if (index >= item_list->size()) {
                        return;
                    }

                    application::model::CrawlBatchItemRecord item = (*item_list)[index];
                    {
                        std::lock_guard<std::mutex> lock(progress->mutex_);
                        progress->current_item_name = item.item_key;
                    }

                    const std::uint64_t item_start = nowSeconds();
                    if (batch_repository_) {
                        item.state = "Running";
                        item.started_at = item_start;
                        item.updated_at = item_start;
                        (void)batch_repository_->updateItem(item);
                        (void)batch_repository_->appendEvent(application::model::CrawlBatchEventRecord {progress->batch_id, static_cast<int>(index), "itemStarted", item.item_key, itemEventPayload(item, "started", 0, {}, {}, {}), item_start});
                    }

                    TargetResult target_result;
                    try {
                        target_result = handler ? handler(item.item_key, normalized_limit) : TargetResult {};
                    } catch (const std::exception &e) {
                        appendError(progress, "Exception processing " + item.item_key + ": " + e.what());
                        item.last_error = e.what();
                    } catch (...) {
                        appendError(progress, "Unknown exception processing " + item.item_key);
                        item.last_error = "Unknown exception";
                    }

                    const std::uint64_t item_finish = nowSeconds();
                    if (target_result.success) {
                        progress->success_count++;
                    } else {
                        progress->failure_count++;
                        if (item.last_error.empty()) {
                            item.last_error = target_result.error.empty() ? "Failed to process: " + item.item_key : target_result.error;
                        }
                        if (target_result.error_category.empty()) {
                            target_result.error_category = "unknown";
                        }
                        if (target_result.result_code.empty()) {
                            target_result.result_code = "failed";
                        }
                        appendError(progress, item.last_error);
                    }
                    if (target_result.success && target_result.result_code.empty()) {
                        target_result.result_code = "ok";
                    }

                    if (batch_repository_) {
                        item.state = target_result.success ? "Completed" : "Failed";
                        item.written_count = (std::max)(0, target_result.written_count);
                        item.error_category = target_result.success ? "" : target_result.error_category;
                        item.result_code = target_result.result_code;
                        item.finished_at = item_finish;
                        item.updated_at = item_finish;
                        (void)batch_repository_->updateItem(item);
                        (void)batch_repository_->appendEvent(application::model::CrawlBatchEventRecord {progress->batch_id, static_cast<int>(index), target_result.success ? "itemCompleted" : "itemFailed", item.item_key, itemEventPayload(item, target_result.success ? "completed" : "failed", target_result.written_count, item.last_error, item.error_category, item.result_code), item_finish});
                    }

                    const int processed = ++progress->processed_count;
                    if (batch_repository_) {
                        application::model::CrawlBatchRecord batch;
                        batch.batch_id = progress->batch_id;
                        batch.state = toStateString(progress->state.load());
                        batch.target_count = progress->total_items;
                        batch.processed_count = processed;
                        batch.success_count = progress->success_count;
                        batch.failure_count = progress->failure_count;
                        if (!target_result.success) {
                            batch.last_error = item.last_error;
                        }
                        batch.updated_at = item_finish;
                        (void)batch_repository_->updateBatchProgress(batch);
                    }

                    if (processed >= progress->total_items.load()) {
                        if (progress->cancel_requested.load()) {
                            progress->state = application::model::BatchState::Cancelled;
                        } else {
                            progress->state = progress->failure_count.load() == 0 ? application::model::BatchState::Completed : application::model::BatchState::Failed;
                        }
                        if (batch_repository_) {
                            const std::uint64_t finished = nowSeconds();
                            application::model::CrawlBatchRecord batch;
                            batch.batch_id = progress->batch_id;
                            batch.state = toStateString(progress->state.load());
                            batch.target_count = progress->total_items;
                            batch.processed_count = progress->processed_count;
                            batch.success_count = progress->success_count;
                            batch.failure_count = progress->failure_count;
                            batch.finished_at = finished;
                            batch.updated_at = finished;
                            (void)batch_repository_->updateBatchProgress(batch);
                            (void)batch_repository_->appendEvent(application::model::CrawlBatchEventRecord {progress->batch_id, std::nullopt, "batchFinished", batch.state, "{}", finished});
                        }
                        return;
                    }
                }
            },
            "batch worker"
        );

        if (!submitted) {
            progress->state = application::model::BatchState::Failed;
            appendError(progress, "Failed to submit batch worker");
            if (batch_repository_) {
                const std::uint64_t failed_at = nowSeconds();
                application::model::CrawlBatchRecord batch;
                batch.batch_id = batch_id;
                batch.state = "Failed";
                batch.target_count = progress->total_items;
                batch.last_error = "Failed to submit batch worker";
                batch.finished_at = failed_at;
                batch.updated_at = failed_at;
                (void)batch_repository_->updateBatchProgress(batch);
            }
            break;
        }
    }

    return batch_id;
}

bool BatchTaskManager::cancel(const std::string &batch_id)
{
    std::shared_ptr<application::model::BatchProgress> progress;
    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        auto it = batch_tasks_.find(batch_id);
        if (it == batch_tasks_.end()) {
            return false;
        }
        progress = it->second;
    }

    const auto state = progress->state.load();
    if (state != application::model::BatchState::Pending && state != application::model::BatchState::Running) {
        return false;
    }

    progress->cancel_requested = true;
    progress->state = application::model::BatchState::Cancelled;
    appendError(progress, "Task cancellation requested");
    if (batch_repository_) {
        const std::uint64_t cancelled_at = nowSeconds();
        application::model::CrawlBatchRecord batch;
        batch.batch_id = batch_id;
        batch.state = "Cancelled";
        batch.target_count = progress->total_items;
        batch.processed_count = progress->processed_count;
        batch.success_count = progress->success_count;
        batch.failure_count = progress->failure_count;
        batch.cancel_requested = true;
        batch.last_error = "Task cancellation requested";
        batch.finished_at = cancelled_at;
        batch.updated_at = cancelled_at;
        (void)batch_repository_->updateBatchProgress(batch);
        (void)batch_repository_->appendEvent(application::model::CrawlBatchEventRecord {batch_id, std::nullopt, "cancelRequested", "Task cancellation requested", "{}", cancelled_at});
    }
    return true;
}

std::optional<application::model::BatchProgressSnapshot> BatchTaskManager::progress(const std::string &batch_id)
{
    std::shared_ptr<application::model::BatchProgress> task_progress;
    {
        std::lock_guard<std::mutex> lock(tasks_mutex_);
        auto it = batch_tasks_.find(batch_id);
        if (it == batch_tasks_.end()) {
            return std::nullopt;
        }
        task_progress = it->second;
    }

    application::model::BatchProgressSnapshot snapshot;
    snapshot.batch_id = task_progress->batch_id;
    snapshot.total = task_progress->total_items;
    snapshot.processed = task_progress->processed_count;
    snapshot.success = task_progress->success_count;
    snapshot.failed = task_progress->failure_count;
    snapshot.state_str = toStateString(task_progress->state.load());

    auto now = std::chrono::system_clock::now();
    snapshot.duration_seconds = std::chrono::duration_cast<std::chrono::seconds>(now - task_progress->start_time).count();

    {
        std::lock_guard<std::mutex> lock(task_progress->mutex_);
        snapshot.current_item = task_progress->current_item_name;
        snapshot.recent_errors = task_progress->error_logs;
    }

    return snapshot;
}

bool BatchTaskManager::isTaskTimedOut(const std::shared_ptr<application::model::BatchProgress> &progress) const
{
    const auto timeout = task_timeout_ ? task_timeout_() : std::chrono::milliseconds(0);
    if (!progress || timeout.count() <= 0) {
        return false;
    }

    const auto now = std::chrono::system_clock::now();
    return now - progress->start_time >= timeout;
}

void BatchTaskManager::appendError(const std::shared_ptr<application::model::BatchProgress> &progress, const std::string &message) const
{
    if (!progress || message.empty()) {
        return;
    }

    std::lock_guard<std::mutex> lock(progress->mutex_);
    if (progress->error_logs.size() < kMaxErrorLogs) {
        progress->error_logs.push_back(message);
    }
}

void BatchTaskManager::cleanupFinishedTasks()
{
    std::lock_guard<std::mutex> lock(tasks_mutex_);
    auto now = std::chrono::system_clock::now();
    for (auto it = batch_tasks_.begin(); it != batch_tasks_.end();) {
        auto &progress = it->second;
        auto state = progress->state.load();
        auto age = std::chrono::duration_cast<std::chrono::hours>(now - progress->start_time).count();

        if (isFinished(state) && age >= 1) {
            it = batch_tasks_.erase(it);
        } else {
            ++it;
        }
    }
}

} // namespace orchestrator::modules::crawler::application::use_cases
