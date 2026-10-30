#include "crawler/application/use_cases/AsyncSongResourceDownloadWorkflow.h"

#include <algorithm>
#include <string>
#include <string_view>

namespace orchestrator::modules::crawler::application::use_cases {
namespace {

using Result = application::result::ServiceResult<bool>;

// 统一隔离处理器异常；下载确认状态不能被后处理故障撤销。
Result processResource(application::ports::ResourcePostProcessor &processor, const application::model::SongResourceProcessItem &item)
{
    try {
        return processor.process(item);
    } catch (...) {
        return Result::failure(application::result::ServiceErrorType::InternalError, "resource_post_processing_failed");
    }
}

// 阶段事件只允许短错误码，避免将 adapter 的异常正文或路径写入批次响应。
std::string eventPayload(const Result &processed)
{
    std::string_view code = processed.isSuccess() ? std::string_view {"ok"} : processed.message();
    const bool stable = !code.empty() && code.size() <= 64 && std::all_of(code.begin(), code.end(), [](unsigned char value) {
        return (value >= 'a' && value <= 'z') || (value >= '0' && value <= '9') || value == '_';
    });
    return "{\"result_code\":\"" + std::string(stable ? code : "resource_post_processing_failed") + "\"}";
}

} // namespace

application::async::Task<void> AsyncSongResourceDownloadWorkflow::postProcessCompleted(const application::model::SongResourceProcessItem &item, SongResourceProcessListener *listener)
{
    if (!item.ok || !extensions_.post_processor) co_return;
    if (extensions_.transfer_access) {
        const auto allowed = co_await runtime_->postBlocking<Result>([access = extensions_.transfer_access] { return access->writesAllowed(); });
        if (!allowed.isSuccess() || !*allowed.data()) {
            co_await notifyItemEvent(listener, item, "resourcePostProcessingFailed", "{\"result_code\":\"resource_maintenance\"}");
            co_return;
        }
    }
    Result processed = Result::failure(application::result::ServiceErrorType::InternalError, "resource_post_processing_failed");
    try {
        processed = co_await runtime_->postBlocking<Result>([processor = extensions_.post_processor, item] { return processResource(*processor, item); });
    } catch (...) {
        // executor 投递失败同样只报告独立阶段失败。
    }
    if (processed.isSuccess() && processed.data() && !*processed.data()) co_return;
    co_await notifyItemEvent(listener, item, processed.isSuccess() ? "resourcePostProcessingCompleted" : "resourcePostProcessingFailed", eventPayload(processed));
}

} // namespace orchestrator::modules::crawler::application::use_cases
