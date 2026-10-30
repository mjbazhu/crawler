#include "ResourceSnapshotSourceSupport.h"

#include "crawler/application/ports/persistence/SongResourceQueue.h"
#include "crawler/application/use_cases/SongResourceDownloadWorkflow.h"
#include "crawler/application/model/SongResourcePolicy.h"
#include "crawler/infrastructure/storage/filesystem/ResourceTokenRefresh.h"

#include <algorithm>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::storage::filesystem::snapshot_source {
namespace {
namespace model = application::model;
namespace result = application::result;

// 仅保留合法音频响应中的关联种类，临时URL随解析结果释放。
class DiscoveryParser final : public application::ports::SongResourceParser {
public:
    // 每项独占workspace，不与兄弟目标共享可变解析状态。
    DiscoveryParser(std::shared_ptr<application::ports::SongResourceParser> inner, std::shared_ptr<Workspace> workspace) : inner_(std::move(inner)), workspace_(std::move(workspace))
    {}
    // 失败响应不得制造关联下载计划。
    model::SongResourceFetchResult parseSongResource(const std::vector<char> &raw) const override
    {
        auto value = inner_->parseSongResource(raw);
        workspace_->related_parsed = value.ok || model::isPermanentSongResourceSkip(value.code);
        workspace_->related_mask = workspace_->related_parsed ? static_cast<std::uint8_t>((value.cover_url.empty() ? 0 : 1) | (value.lyric_url.empty() ? 0 : 2) | (value.mrc_url.empty() ? 0 : 4) | (value.trc_url.empty() ? 0 : 8)) : 0;
        return value;
    }

private:
    std::shared_ptr<application::ports::SongResourceParser> inner_; // 既有合法解析器
    std::shared_ptr<Workspace> workspace_;                          // 本项发现事实
};

// 此队列只确认独占临时快照，不连接数据库，不改写正式 local 或 cloud。
class SnapshotCapture final : public application::ports::SongResourceQueue {
public:
    // 来源获取只处理已经冻结的一项，绝不再领正式任务。
    std::vector<model::SongResourceQueueTarget> claimPending(model::SongResourceClaimQuery) override
    {
        return {};
    }
    // 来源获取不扩展同歌曲关联资源。
    std::optional<model::SongResourceQueueTarget> claimRelated(model::SongResourceRelatedClaimRequest) override
    {
        return std::nullopt;
    }
    // 私有文件由来源 workspace 租约持有，正式确认仍由持久任务负责。
    bool markSucceeded(model::SongResourceSuccessUpdate) override
    {
        return true;
    }
    // 失败只通过返回项传递，不污染本地资源状态。
    bool markFailed(model::SongResourceFailureUpdate) override
    {
        return true;
    }
    // 永久限制由外层当前版本的任务原子登记。
    bool markSkipped(model::SongResourceSkipUpdate) override
    {
        return true;
    }
};

// 装饰现有流式 fetcher，以冻结上限和当前目标取消限制私有快照。
class BoundedContent final : public application::ports::SongResourceContentFetcher {
public:
    // 每次来源调用独占计数，不共享正文或临时 URL。
    BoundedContent(std::shared_ptr<application::ports::SongResourceContentFetcher> inner, const model::ResourceTransferJob &job, const model::ResourceTransferContext &context) : inner_(std::move(inner)), job_(job), context_(context)
    {}
    // 正式下载链必须走分片接口，拒绝意外退回整首歌聚合。
    model::SongResourceContentFetchResult fetchSongResourceContent(model::SongResourceContentFetchRequest) override
    {
        model::SongResourceContentFetchResult value;
        value.result_code = "stream_required";
        value.error_category = "invalid_target";
        return value;
    }
    // 依赖实现必须真正覆盖流式读取，生产装配使用 HttpSongResourceContentFetcher。
    model::SongResourceContentStreamResult streamSongResourceContent(model::SongResourceContentFetchRequest request, model::SongResourceContentStreamCallbacks callbacks) override
    {
        std::uint64_t total = 0;
        bool limit = false;
        request.timeout_ms = (std::min)(request.timeout_ms > 0 ? request.timeout_ms : context_.remaining(), context_.remaining());
        model::SongResourceContentStreamCallbacks bounded;
        bounded.on_response = [&](const auto &info) { return !context_.stopped() && (!callbacks.on_response || callbacks.on_response(info)); };
        bounded.on_chunk = [&](std::string_view chunk) {
            if (context_.stopped()) return false;
            if (chunk.size() > job_.policy.max_bytes - total) {
                limit = true;
                return false;
            }
            total += chunk.size();
            return !callbacks.on_chunk || callbacks.on_chunk(chunk);
        };
        auto value = inner_->streamSongResourceContent(std::move(request), std::move(bounded));
        if (limit) {
            value.ok = false;
            value.error_category = "resource";
            value.result_code = "snapshot_too_large";
        } else if (context_.stopped()) {
            value.ok = false;
            value.error_category = "resource";
            value.result_code = context_.remaining() == 0 ? "timeout" : "cancelled";
        }
        return value;
    }

private:
    std::shared_ptr<application::ports::SongResourceContentFetcher> inner_; // 线程安全真实流式端口
    model::ResourceTransferJob job_;                                        // 冻结预算
    model::ResourceTransferContext context_;                                // 独立停止和截止时间
};
} // namespace

application::result::ServiceResult<std::string> song(const ResourceSnapshotDependencies &dependencies, const model::ResourceTransferJob &job, model::SongResourceQueueTarget target, const std::shared_ptr<Workspace> &workspace, const model::ResourceTransferContext &context)
{
    using Result = result::ServiceResult<std::string>;
    if (!dependencies.songs || !dependencies.parser || !dependencies.content) return Result::failure(result::ServiceErrorType::InvalidParam, "missing_source_dependency");
    auto files = std::make_shared<FileStorage>(workspace->root, false);
    auto queue = std::make_shared<SnapshotCapture>();
    auto content = std::make_shared<BoundedContent>(dependencies.content, job, context);
    target.local_path.clear(); // 正式旧文件不属于这个私有 workspace。
    auto parser = dependencies.parser;
    if (job.process_related_resources && target.resource_kind == "audio") parser = std::make_shared<DiscoveryParser>(parser, workspace);
    auto refresher = dependencies.token_refresher;
    if (dependencies.token_refresh && target.resource_kind == "audio") {
        refresher = dependencies.token_refresh->bind(job, refresher, dependencies.token_store, context);
        const bool valid = !target.source_content_id.empty() && !target.copyright_id.empty() && !target.format_type.empty() && (!target.audio_resource_type.empty() || !target.source_resource_type.empty()) && target.source_album_kind != 2;
        if (valid && job.source_options.auto_refresh_pacmtoken && dependencies.token_store) dependencies.token_refresh->initial(refresher);
    }
    application::use_cases::SongResourceDownloadWorkflow workflow(queue, dependencies.songs, parser, content, files, {}, nullptr, refresher, dependencies.token_store);
    model::SongResourceProcessOptions options;
    options.process_related_resources = false;
    options.auto_refresh_pacmtoken = job.source_options.auto_refresh_pacmtoken;
    options.refresh_pacmtoken_on_440013 = job.source_options.refresh_pacmtoken_on_440013;
    options.pacmtoken_refresh_interval_seconds = job.source_options.pacmtoken_refresh_interval_seconds;
    options.resource_download_retries = 0;
    options.resource_download_timeout_ms = context.remaining();
    const auto item = workflow.processClaimed(std::move(target), "snapshot:" + std::to_string(job.task_id), options);
    if (item.skipped) return Result::failure(result::ServiceErrorType::ResourceRestricted, item.result_code);
    if (!item.ok) {
        const auto type = item.error_category == "network" ? result::ServiceErrorType::TransientExternalApiError : item.error_category == "invalid_target" ? result::ServiceErrorType::InvalidParam :
                                                                                                                                                             result::ServiceErrorType::ExternalApiError;
        return Result::failure(type, item.result_code);
    }
    return Result::success(item.local_path);
}

} // namespace orchestrator::modules::crawler::infrastructure::storage::filesystem::snapshot_source
