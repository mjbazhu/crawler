#include "crawler/api/controllers/CrawlerController.h"

#include "crawler/api/dto/MetadataCoverDto.h"

#include <algorithm>
#include <charconv>
#include <cctype>
#include <string_view>

namespace orchestrator::modules::crawler::api::controllers {
namespace {
using json = nlohmann::json;
namespace model = application::model;

// 用例错误只映射协议状态，不输出基础设施异常。
http::HttpResponse coverError(application::result::ServiceErrorType type, const std::string &message)
{
    using Error = application::result::ServiceErrorType;
    const int status = type == Error::InvalidParam ? 400 : (type == Error::NotFound ? 404 : (type == Error::Conflict ? 409 : 503));
    return http::HttpResponse::json(status, json {{"error", message}}.dump());
}

// 入参错误保持稳定短文本。
http::HttpResponse invalidCoverRequest()
{
    return coverError(application::result::ServiceErrorType::InvalidParam, "invalid metadata cover request");
}

// HTTP header 名称按大小写不敏感比较。
std::string lowerHeader(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    return value;
}
} // namespace

http::HttpResponse CrawlerController::handleListMiguCovers(http::HttpRequest request)
{
    auto query = dto::metadataCoverQuery(request);
    if (!query) return invalidCoverRequest();
    if (!cover_service_) return coverError(application::result::ServiceErrorType::InternalError, "metadata cover service is not configured");
    auto page = cover_service_->list(*query);
    if (!page.isSuccess()) return coverError(page.error(), page.message());
    json items = json::array();
    for (const auto &record : page.data()->items) items.push_back(dto::metadataCoverToJson(record));
    return http::HttpResponse::json(200, json {{"items", std::move(items)}, {"total", page.data()->total}, {"page", page.data()->page}, {"page_size", page.data()->page_size}}.dump());
}

http::HttpResponse CrawlerController::handleProcessMiguCovers(http::HttpRequest request)
{
    const auto input = dto::MetadataCoverRequestDto::fromRequest(request);
    if (!input) return invalidCoverRequest();
    if (!cover_service_) return coverError(application::result::ServiceErrorType::InternalError, "metadata cover service is not configured");
    if (input->async) {
        const auto batch = cover_service_->start(input->options);
        if (!batch.isSuccess()) return coverError(batch.error(), batch.message());
        return http::HttpResponse::json(202, json {{"batch_id", *batch.data()}, {"message", "metadata cover task accepted"}}.dump());
    }
    const auto processed = cover_service_->process(input->options);
    if (!processed.isSuccess()) return coverError(processed.error(), processed.message());
    json items = json::array();
    int success = 0, failed = 0, skipped = 0;
    for (const auto &item : *processed.data()) {
        auto value = dto::metadataCoverToJson(item.record);
        value["success"] = item.success;
        value["skipped"] = item.skipped;
        if (!item.result_code.empty()) {
            value["result_code"] = item.result_code;
            value["error_category"] = item.error_category;
            value["last_error"] = item.last_error;
        }
        items.push_back(std::move(value));
        if (item.skipped)
            ++skipped;
        else if (item.success)
            ++success;
        else
            ++failed;
    }
    return http::HttpResponse::json(200, json {{"items", std::move(items)}, {"success_count", success}, {"failed_count", failed}, {"skipped_count", skipped}}.dump());
}

http::HttpResponse CrawlerController::handleRecoverMiguCovers(http::HttpRequest request)
{
    const auto input = dto::MetadataCoverRequestDto::fromRequest(request);
    if (!input) return invalidCoverRequest();
    if (!cover_service_) return coverError(application::result::ServiceErrorType::InternalError, "metadata cover service is not configured");
    const auto recovered = cover_service_->recover(input->options.query, input->stale_after_seconds);
    if (!recovered.isSuccess()) return coverError(recovered.error(), recovered.message());
    return http::HttpResponse::json(200, json {{"owner_type", model::coverOwnerName(input->options.query.owner)}, {"recovered_count", *recovered.data()}}.dump());
}

http::HttpResponse CrawlerController::handleGetMiguCoverContent(http::HttpRequest request)
{
    constexpr std::string_view prefix = "/api/migu/covers/";
    constexpr std::string_view suffix = "/content";
    const std::string_view path(request.path);
    if (!path.starts_with(prefix) || !path.ends_with(suffix)) return invalidCoverRequest();
    const auto target = path.substr(prefix.size(), path.size() - prefix.size() - suffix.size());
    const auto separator = target.find('/');
    if (separator == std::string_view::npos) return invalidCoverRequest();
    const auto owner = target.substr(0, separator);
    if (owner != "singer" && owner != "album") return invalidCoverRequest();
    const auto id_text = target.substr(separator + 1);
    std::uint64_t id = 0;
    const auto parsed = std::from_chars(id_text.data(), id_text.data() + id_text.size(), id);
    if (id == 0 || parsed.ec != std::errc {} || parsed.ptr != id_text.data() + id_text.size()) return invalidCoverRequest();
    if (!cover_service_) return coverError(application::result::ServiceErrorType::InternalError, "metadata cover service is not configured");
    const auto content = cover_service_->content(owner == "singer" ? model::CoverOwner::Singer : model::CoverOwner::Album, id);
    if (!content.isSuccess()) return coverError(content.error(), content.message());
    const auto &image = *content.data();
    const std::string etag = '"' + image.checksum + '"';
    http::HttpResponse response {200, {{"Content-Type", image.content_type}, {"ETag", etag}, {"Cache-Control", "public, max-age=0, must-revalidate"}, {"X-Content-Type-Options", "nosniff"}}, image.bytes};
    for (const auto &[name, value] : request.headers) {
        if (lowerHeader(name) == "if-none-match" && (value == etag || value == "*")) {
            response.status_code = 304;
            response.body.clear();
        }
    }
    return response;
}

} // namespace orchestrator::modules::crawler::api::controllers
