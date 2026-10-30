#include "crawler/application/use_cases/LegacyResourceTransferService.h"

#include <algorithm>
#include <chrono>
#include <stdexcept>
#include <thread>
#include <utility>

namespace orchestrator::modules::crawler::application::use_cases {
namespace {
using Batch = model::LegacyResourceTransferBatch;
using Result = result::ServiceResult<Batch>;
// 失败不包含存储配置或原始请求。
Result unavailable(const char *code)
{
    return Result::failure(result::ServiceErrorType::ExternalApiError, code);
}
} // namespace

LegacyResourceTransferService::LegacyResourceTransferService(std::shared_ptr<ports::LegacyResourceTransferRepository> repository, std::shared_ptr<ports::ResourceTransferRuntimeControl> runtime, std::shared_ptr<ports::ResourceTransferAccess> access, std::optional<model::ResourceDestination> provider, bool filesystem, std::shared_ptr<ports::MetadataCoverReader> reader, int batch_timeout_ms) : repository_(std::move(repository)), runtime_(std::move(runtime)), access_(std::move(access)), cover_reader_(std::move(reader)), cloud_provider_(provider), filesystem_(filesystem), batch_timeout_ms_(batch_timeout_ms)
{
    if (!repository_ || !runtime_ || !access_ || (provider && *provider == model::ResourceDestination::Local)) throw std::invalid_argument("legacy resource transfer dependencies required");
}

Result LegacyResourceTransferService::songs(model::SongResourceClaimQuery query, bool sync, int batch_timeout_ms)
{
    if (!filesystem_) return unavailable("legacy_copyurl_parallel_unsupported");
    model::LegacyResourceTransferRequest request;
    request.songs = std::move(query);
    request.batch_timeout_ms = batch_timeout_ms > 0 ? batch_timeout_ms : batch_timeout_ms_;
    return start(std::move(request), sync);
}

Result LegacyResourceTransferService::covers(model::MetadataCoverOptions options, bool sync)
{
    if (options.query.source != "migu" || (options.refresh && options.query.resource_ids.empty())) return Result::failure(result::ServiceErrorType::InvalidParam, "invalid legacy cover request");
    model::LegacyResourceTransferRequest request;
    request.owner = options.query.owner == model::CoverOwner::Singer ? model::ResourceOwner::Singer : model::ResourceOwner::Album;
    request.covers = std::move(options);
    request.batch_timeout_ms = batch_timeout_ms_;
    auto processed = start(std::move(request), sync);
    if (!processed.isSuccess() || !cover_reader_) return processed;
    auto batch = *processed.data();
    for (auto &item : batch.covers) item.record.available = cover_reader_->available(item.record.file);
    return Result::success(std::move(batch));
}

Result LegacyResourceTransferService::start(model::LegacyResourceTransferRequest request, bool sync)
{
    if (!runtime_->parallelEnabled()) return unavailable("resource_parallel_required");
    const auto allowed = access_->writesAllowed();
    if (!allowed.isSuccess() || !*allowed.data()) return unavailable("resource_maintenance");
    const auto local = access_->destinationReady(model::ResourceDestination::Local);
    if (!local.isSuccess() || !*local.data()) return unavailable("resource_maintenance");
    request.cloud_provider = cloud_provider_;
    request.profiles = runtime_->profiles();
    const auto enabled = [&](model::ResourceDestination destination) { return std::any_of(request.profiles.begin(), request.profiles.end(), [&](const auto &profile) { return profile.destination == destination; }); };
    if (!enabled(model::ResourceDestination::Local) || (request.owner != model::ResourceOwner::Song && cloud_provider_ && !enabled(*cloud_provider_))) return unavailable("legacy_required_destination_disabled");
    const bool song = request.owner == model::ResourceOwner::Song;
    const int timeout = song ? request.songs.options.resource_download_timeout_ms : request.covers.timeout_ms;
    const int retries = song ? request.songs.options.resource_download_retries : request.covers.retries;
    for (auto &profile : request.profiles) {
        if (cloud_provider_ == profile.destination) request.strict_cloud = profile.policy.strict_sha256;
        if (profile.destination == model::ResourceDestination::Local || (!song && cloud_provider_ == profile.destination)) {
            profile.policy.timeout_ms = timeout;
            profile.policy.retries = retries;
        }
    }
    // 旧refresh只重做旧范围；不会因增加平台扩大原请求的上传范围。
    if (!song && request.covers.refresh) std::erase_if(request.profiles, [&](const auto &profile) { return profile.destination != model::ResourceDestination::Local && cloud_provider_ != profile.destination; });
    const auto budget = std::chrono::milliseconds(request.batch_timeout_ms + 1000);
    auto started = repository_->start(std::move(request));
    if (!started.isSuccess() || !sync || started.data()->terminal) return started;
    auto batch = *started.data();
    const auto deadline = std::chrono::steady_clock::now() + budget;
    while (!batch.terminal && runtime_->parallelEnabled() && std::chrono::steady_clock::now() < deadline) {
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
        const auto next = find(batch.batch_id);
        if (!next.isSuccess()) return Result::failure(next.error(), next.message());
        if (!*next.data()) return Result::failure(result::ServiceErrorType::NotFound, "legacy_batch_missing");
        batch = **next.data();
    }
    // 仍在运行时由API返回不可用和恢复ID，不能把等待超时当作同步完成。
    return Result::success(std::move(batch));
}

result::ServiceResult<std::optional<Batch>> LegacyResourceTransferService::find(std::string id)
{
    const auto found = repository_->find(std::move(id));
    if (!found.isSuccess() || !*found.data() || !cover_reader_) return found;
    auto batch = **found.data();
    for (auto &item : batch.covers) item.record.available = cover_reader_->available(item.record.file);
    return result::ServiceResult<std::optional<Batch>>::success(std::move(batch));
}

result::ServiceResult<bool> LegacyResourceTransferService::cancel(std::string id)
{
    return repository_->cancel(std::move(id));
}

bool LegacyResourceTransferService::pollNext()
{
    const auto refreshed = repository_->pollNext();
    return refreshed.isSuccess() && *refreshed.data();
}

result::ServiceResult<model::MetadataCoverPage> LegacyResourceTransferService::list(const model::MetadataCoverQuery &query)
{
    model::LegacyResourceTransferRequest request;
    request.owner = query.owner == model::CoverOwner::Singer ? model::ResourceOwner::Singer : model::ResourceOwner::Album;
    request.covers.query = query;
    request.covers.limit = query.page_size;
    request.cloud_provider = cloud_provider_;
    for (const auto &profile : runtime_->profiles())
        if (cloud_provider_ == profile.destination) request.strict_cloud = profile.policy.strict_sha256;
    const auto found = repository_->listCovers(std::move(request));
    if (!found.isSuccess() || !cover_reader_) return found;
    auto page = *found.data();
    for (auto &item : page.items) item.available = cover_reader_->available(item.file);
    return result::ServiceResult<model::MetadataCoverPage>::success(std::move(page));
}

} // namespace orchestrator::modules::crawler::application::use_cases
