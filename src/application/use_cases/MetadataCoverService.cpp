#include "crawler/application/use_cases/MetadataCoverService.h"

#include <map>
#include <stdexcept>
#include <utility>

namespace orchestrator::modules::crawler::application::use_cases {
namespace {

// 同步和后台处理共用同一条本地确认后自动上传的流程。
model::MetadataCoverItem processTarget(MetadataCoverDownloadWorkflow &local, MetadataCoverCloudWorkflow *cloud, model::MetadataCoverRecord target, const model::MetadataCoverOptions &options, const std::string &batch = {}, std::function<bool()> cancelled = {})
{
    auto item = local.process(std::move(target), options, batch, cancelled);
    // 声明本地跳过也须检查旧契约要求的固定云端；不能只凭 local skip 结束整个旧视图。
    if (cloud && (item.success || (item.skipped && item.result_code == "skip_existing"))) item = cloud->process(item.record, options.retries, batch, std::move(cancelled));
    item.record.cloud.enabled = cloud != nullptr;
    if (cloud) item.record.cloud.current = cloud->matches(item.record);
    return item;
}
} // namespace

MetadataCoverService::MetadataCoverService(std::shared_ptr<ports::MetadataCoverQueue> queue, std::shared_ptr<ports::MetadataCoverReader> reader, std::shared_ptr<MetadataCoverDownloadWorkflow> workflow, std::shared_ptr<ports::TaskExecutor> executor, std::shared_ptr<ports::DiagnosticsSink> diagnostics, std::shared_ptr<ports::BatchTaskRepository> batches, std::chrono::milliseconds timeout, std::shared_ptr<MetadataCoverCloudWorkflow> cloud) :
    queue_(std::move(queue)), reader_(std::move(reader)), workflow_(std::move(workflow)), cloud_(std::move(cloud)),
    tasks_(std::move(executor), std::move(diagnostics), std::move(batches), [] { return 3; }, [timeout] { return timeout; })
{
    if (!queue_ || !reader_ || !workflow_) throw std::invalid_argument("metadata cover dependencies are required");
}

result::ServiceResult<model::MetadataCoverPage> MetadataCoverService::list(const model::MetadataCoverQuery &query)
{
    if (projection_) return projection_->list(query);
    auto page = queue_->list(query);
    if (!page.isSuccess()) return page;
    auto value = *page.data();
    for (auto &record : value.items) {
        record.available = reader_->available(record.file);
        record.cloud.enabled = cloud_ != nullptr;
        record.cloud.current = cloud_ ? cloud_->matches(record) : (record.cloud.status == 2 && record.cloud.receipt.checksum == record.file.checksum && record.cloud.receipt.size_bytes == record.file.size_bytes && !record.file.local_path.empty());
    }
    return result::ServiceResult<model::MetadataCoverPage>::success(std::move(value));
}

result::ServiceResult<model::MetadataCoverPage> MetadataCoverService::select(const model::MetadataCoverOptions &options)
{
    using Result = result::ServiceResult<model::MetadataCoverPage>;
    if (options.limit < 1 || options.limit > 200 || options.concurrency < 1 || options.concurrency > 32 || options.retries < 0 || options.retries > 5 || options.timeout_ms < 100 || options.timeout_ms > 600000 || options.query.source != "migu") {
        return Result::failure(result::ServiceErrorType::InvalidParam, "invalid metadata cover options");
    }
    if (options.refresh && options.query.resource_ids.empty()) return Result::failure(result::ServiceErrorType::InvalidParam, "refresh requires explicit resource_ids");
    auto query = options.query;
    query.page = 1;
    query.page_size = options.limit;
    if (cloud_ && !options.refresh)
        cloud_->candidates(query);
    else if (query.statuses.empty())
        query.statuses = options.refresh ? std::vector<int> {0, 2, 3} : std::vector<int> {0, 3};
    for (int status : query.statuses) {
        if (status != 0 && status != 3 && !(status == 2 && (options.refresh || cloud_))) return Result::failure(result::ServiceErrorType::InvalidParam, "invalid cover process status");
    }
    return queue_->list(query);
}

result::ServiceResult<std::vector<model::MetadataCoverItem>> MetadataCoverService::process(const model::MetadataCoverOptions &options)
{
    using Result = result::ServiceResult<std::vector<model::MetadataCoverItem>>;
    auto page = select(options);
    if (!page.isSuccess()) return Result::failure(page.error(), page.message());
    std::vector<model::MetadataCoverItem> items;
    for (const auto &target : page.data()->items) items.push_back(processTarget(*workflow_, cloud_.get(), target, options));
    return Result::success(std::move(items));
}

result::ServiceResult<std::string> MetadataCoverService::start(const model::MetadataCoverOptions &options)
{
    using Result = result::ServiceResult<std::string>;
    auto selected = select(options);
    if (!selected.isSuccess()) return Result::failure(selected.error(), selected.message());
    auto targets = std::make_shared<std::map<std::string, model::MetadataCoverRecord>>();
    std::vector<model::CrawlBatchItemRecord> items;
    for (const auto &record : selected.data()->items) {
        model::CrawlBatchItemRecord item;
        item.item_index = static_cast<int>(items.size());
        item.item_key = std::string(model::coverOwnerName(record.owner)) + ':' + std::to_string(record.resource_id);
        item.item_type = record.owner == model::CoverOwner::Singer ? "singerCover" : "albumCover";
        item.source = record.source;
        if (record.owner == model::CoverOwner::Singer)
            item.source_singer_id = record.source_id;
        else
            item.source_album_id = record.source_id;
        item.payload = "{\"owner_type\":\"" + std::string(model::coverOwnerName(record.owner)) + "\",\"resource_id\":" + std::to_string(record.resource_id) + ",\"source_ref_id\":" + std::to_string(record.source_ref_id) + '}';
        targets->emplace(item.item_key, record);
        items.push_back(std::move(item));
    }
    // 只保存已校验的策略与身份，不持久化原始请求正文或远端 URL。
    const std::string params = "{\"owner_type\":\"" + std::string(model::coverOwnerName(options.query.owner)) + "\",\"limit\":" + std::to_string(options.limit) + ",\"refresh\":" + (options.refresh ? "true" : "false") + '}';
    const std::string settings = "{\"concurrency\":" + std::to_string(options.concurrency) + ",\"retries\":" + std::to_string(options.retries) + ",\"timeout_ms\":" + std::to_string(options.timeout_ms) + '}';
    const auto batch = tasks_.startWithContext(items, options.limit, "metadataCovers", params, settings, [workflow = workflow_, cloud = cloud_, targets, options](std::string key, int, const std::string &batch_id, const std::function<bool()> &cancelled) {
            const auto item = processTarget(*workflow, cloud.get(), targets->at(key), options, batch_id, cancelled);
            BatchTaskManager::TargetResult result;
            const auto code = item.result_code.empty() ? item.record.file.result_code : item.result_code;
            result.success = item.success || (item.skipped && (code == "cover_not_claimed" || code == "skip_existing" || code == "permanent_resource_restriction"));
            result.written_count = item.success && !item.skipped ? 1 : 0;
            result.error = item.last_error.empty() ? item.record.file.last_error : item.last_error;
            result.error_category = item.error_category.empty() ? item.record.file.error_category : item.error_category;
            result.result_code = item.result_code.empty() ? item.record.file.result_code : item.result_code;
            return result; }, options.concurrency);
    if (batch.empty()) return Result::failure(result::ServiceErrorType::InternalError, "cover batch could not be persisted");
    return Result::success(batch);
}

result::ServiceResult<model::MetadataCoverContent> MetadataCoverService::content(model::CoverOwner owner, std::uint64_t id)
{
    using Result = result::ServiceResult<model::MetadataCoverContent>;
    const auto stored = queue_->find(owner, id);
    if (!stored.isSuccess()) return Result::failure(stored.error(), stored.message());
    if (*stored.data()) {
        if (auto content = reader_->read((**stored.data()).file)) return Result::success(std::move(*content));
    }
    return Result::failure(result::ServiceErrorType::NotFound, "local cover is unavailable");
}

result::ServiceResult<int> MetadataCoverService::recover(const model::MetadataCoverQuery &query, int stale_seconds)
{
    const auto local = queue_->recover(query, stale_seconds);
    if (!local.isSuccess() || !cloud_) return local;
    const auto remote = cloud_->recover(query, stale_seconds);
    if (!remote.isSuccess()) return remote;
    return result::ServiceResult<int>::success(*local.data() + *remote.data());
}

std::optional<model::BatchProgressSnapshot> MetadataCoverService::progress(const std::string &batch_id)
{
    return tasks_.progress(batch_id);
}

bool MetadataCoverService::cancel(const std::string &batch_id)
{
    return tasks_.cancel(batch_id);
}

} // namespace orchestrator::modules::crawler::application::use_cases
