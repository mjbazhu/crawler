#include "crawler/api/http/ResourceTransferError.h"

#include <nlohmann/json.hpp>
#include <string>

namespace orchestrator::modules::crawler::api::http {
HttpResponse resourceTransferError(application::result::ServiceErrorType type, std::string_view code)
{
    using Error = application::result::ServiceErrorType;
    int status = type == Error::InvalidParam ? 400 : type == Error::NotFound     ? 404 :
                                                 type == Error::Conflict         ? 409 :
                                                 type == Error::ExternalApiError ? 503 :
                                                                                   500;
    std::string safe = status == 400 ? "invalid_resource_transfer_request" : status == 404 ? "resource_transfer_operation_not_found" :
                                                                         status == 409     ? "resource_transfer_conflict" :
                                                                         status == 503     ? "resource_transfer_unavailable" :
                                                                                             "resource_transfer_failed";
    if (type == Error::CommitUncertain) {
        status = 503;
        safe = "resource_transfer_commit_uncertain";
    }
    if (status == 503 && (code == "resource_queue_full" || code == "resource_maintenance" || code == "resource_parallel_required" || code == "resource_schema_unavailable" || code == "resource_destination_disabled" || code == "legacy_required_destination_disabled" || code == "legacy_completion_pending" || code == "legacy_batch_timeout")) safe = code;
    auto response = HttpResponse::json(status, nlohmann::json {{"error_code", safe}, {"message", safe}, {"error", safe}}.dump());
    response.headers["Access-Control-Allow-Origin"] = "*";
    if (safe == "resource_queue_full") {
        response.headers["Retry-After"] = "2";
        response.headers["Access-Control-Expose-Headers"] = "Retry-After";
    }
    return response;
}
} // namespace orchestrator::modules::crawler::api::http
