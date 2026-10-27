#include "crawler/application/use_cases/LyricProcessingWorkflow.h"

#include <utility>

namespace orchestrator::modules::crawler::application::use_cases {

LyricProcessingWorkflow::LyricProcessingWorkflow(LyricProcessingDependencies dependencies, std::string source) :
    dependencies_(std::move(dependencies)), source_(std::move(source))
{}

result::ServiceResult<bool> LyricProcessingWorkflow::process(const model::SongResourceProcessItem &item) const
{
    if (!item.ok || item.resource_kind != "mrc" || item.source != source_) {
        return result::ServiceResult<bool>::success(false);
    }
    model::LyricProcessingRequest request {item.resource_id, item.local_path, item.checksum};
    const auto processed = processFile(request);
    if (!processed.isSuccess()) {
        return result::ServiceResult<bool>::failure(processed.error(), processed.message());
    }
    return result::ServiceResult<bool>::success(true);
}

result::ServiceResult<model::LyricArtifactBundle> LyricProcessingWorkflow::processFile(const model::LyricProcessingRequest &request) const
{
    using Result = result::ServiceResult<model::LyricArtifactBundle>;
    if (!dependencies_.decoder || !dependencies_.encoder || !dependencies_.store) {
        return Result::failure(result::ServiceErrorType::InternalError, "lyric_missing_dependency");
    }
    if (request.local_path.empty()) {
        return Result::failure(result::ServiceErrorType::InvalidParam, "lyric_missing_source_path");
    }
    const std::string version = std::string(dependencies_.decoder->version()) + "-" + std::string(dependencies_.encoder->version());
    // 数据库失败优先保留，避免把索引故障误报为成功或撤销原始下载。
    const auto failed = [&](result::ServiceErrorType type, const std::string &code) {
        if (dependencies_.repository && request.source_resource_id != 0) {
            const auto recorded = dependencies_.repository->recordFailure(request, version, dependencies_.encoder->schemaVersion(), code);
            if (!recorded.isSuccess()) return Result::failure(recorded.error(), recorded.message());
            if (!recorded.data() || !*recorded.data()) return Result::failure(result::ServiceErrorType::Conflict, "lyric_source_changed");
        }
        return Result::failure(type, code);
    };
    const auto source = dependencies_.store->readSource(request.local_path, 1024 * 1024);
    if (!source.isSuccess()) return failed(source.error(), source.message());
    if (!request.expected_checksum.empty() && source.data()->checksum != request.expected_checksum) {
        return failed(result::ServiceErrorType::Conflict, "lyric_source_changed");
    }
    const auto decoded = dependencies_.decoder->decode(source.data()->content);
    if (!decoded.isSuccess()) return failed(decoded.error(), decoded.message());
    const auto encoded = dependencies_.encoder->encode(*decoded.data());
    if (!encoded.isSuccess()) return failed(encoded.error(), encoded.message());
    const auto published = dependencies_.store->publish(request, source.data()->checksum, version, dependencies_.encoder->schemaVersion(), *encoded.data());
    if (!published.isSuccess()) return failed(published.error(), published.message());
    if (dependencies_.repository && request.source_resource_id != 0) {
        const auto recorded = dependencies_.repository->record(*published.data());
        if (!recorded.isSuccess()) return Result::failure(recorded.error(), recorded.message());
        if (!recorded.data() || !*recorded.data()) return Result::failure(result::ServiceErrorType::Conflict, "lyric_source_changed");
    }
    // 索引失败时保留完整文件，后续重试校验并复用，不重新下载来源。
    return published;
}

} // namespace orchestrator::modules::crawler::application::use_cases
