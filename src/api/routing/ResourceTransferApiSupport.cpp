#include "ResourceTransferApiSupport.h"
#include "crawler/api/http/ResourceTransferError.h"

#include <algorithm>
#include <charconv>
#include <limits>
#include <set>
#include <stdexcept>

namespace orchestrator::modules::crawler::api::routing::transfer_api {

void keys(const Json &value, std::initializer_list<std::string_view> allowed)
{
    if (!value.is_object()) throw std::invalid_argument("object required");
    for (const auto &[key, ignored] : value.items())
        if (std::find(allowed.begin(), allowed.end(), key) == allowed.end()) throw std::invalid_argument("unknown field");
}

Json body(const http::HttpRequest &request)
{
    if (request.body.size() > 262144) throw std::invalid_argument("body too large");
    return Json::parse(request.body);
}

std::uint64_t positive(const Json &value)
{
    if (!value.is_number_unsigned() && !value.is_number_integer()) throw std::invalid_argument("integer required");
    if (value.is_number_integer() && !value.is_number_unsigned() && value.get<std::int64_t>() <= 0) throw std::invalid_argument("positive integer required");
    const auto result = value.get<std::uint64_t>();
    if (!result) throw std::invalid_argument("positive integer required");
    return result;
}

std::vector<model::ResourceDestination> destinations(const Json &value)
{
    if (!value.is_array() || value.size() > 3) throw std::invalid_argument("invalid destinations");
    std::vector<model::ResourceDestination> result;
    for (const auto &name : value) {
        const auto destination = model::parseResourceDestination(name.get<std::string>());
        if (!destination || std::find(result.begin(), result.end(), *destination) != result.end()) throw std::invalid_argument("invalid destination");
        result.push_back(*destination);
    }
    return result;
}

model::ResourceTransferJobPolicy policy(const Json &value)
{
    keys(value, {"timeout_ms", "retries", "max_bytes", "bytes_per_second"});
    if (value.size() != 4) throw std::invalid_argument("complete policy required");
    model::ResourceTransferJobPolicy result;
    const auto timeout = positive(value.at("timeout_ms"));
    const auto zeroAllowed = [](const Json &item) { if (item.is_number_integer() && item == 0) return std::uint64_t {0}; return positive(item); };
    const auto retries = zeroAllowed(value.at("retries"));
    if (timeout < 100 || timeout > 600000 || retries > 5) throw std::invalid_argument("invalid budget");
    result.timeout_ms = static_cast<int>(timeout);
    result.retries = static_cast<int>(retries);
    result.max_bytes = positive(value.at("max_bytes"));
    result.bytes_per_second = zeroAllowed(value.at("bytes_per_second"));
    if (result.max_bytes > std::uint64_t {4} * 1024 * 1024 * 1024 || result.bytes_per_second > std::uint64_t {4} * 1024 * 1024 * 1024) throw std::invalid_argument("invalid budget");
    return result;
}

std::uint64_t number(std::string_view value, bool zero)
{
    std::uint64_t result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (parsed.ec != std::errc {} || parsed.ptr != value.data() + value.size() || (!zero && !result)) throw std::invalid_argument("invalid number");
    return result;
}

Json operation(const model::ResourceTransferOperation &value)
{
    Json tasks = Json::array(), counts = Json::object();
    for (const auto &task : value.tasks) {
        const auto destination = std::string(model::resourceDestinationName(task.destination));
        if (!counts.contains(destination)) counts[destination] = Json::object();
        auto &count = counts[destination][task.state];
        count = count.is_null() ? 1 : count.get<int>() + 1;
        tasks.push_back({{"task_id", task.task_id}, {"owner_type", model::resourceOwnerName(task.resource.owner)}, {"resource_id", task.resource.resource_id}, {"destination", destination}, {"adapter", task.adapter}, {"state", task.state}, {"result_code", task.result_code}, {"content_generation", task.version.content_generation}, {"target_revision", task.version.target_revision}, {"cancel_requested", task.cancel_requested}, {"busy_task_id", task.busy_task_id ? Json(task.busy_task_id) : Json(nullptr)}, {"attempt_count", task.attempt_count}, {"policy", policyJson(task.policy)}});
    }
    for (std::size_t i = 0; i < value.tasks.size(); ++i) tasks[i]["strict_sha256"] = value.tasks[i].policy.strict_sha256;
    return {{"batch_id", value.operation_id}, {"state", value.state}, {"logical_resource_count", value.logical_resource_count}, {"destination_counts", counts}, {"tasks", tasks}, {"replayed", value.replayed}, {"parent_batch_id", value.parent_operation_id.empty() ? Json(nullptr) : Json(value.parent_operation_id)}, {"parent_task_id", value.parent_task_id ? Json(value.parent_task_id) : Json(nullptr)}, {"child_batch_ids", value.child_operation_ids}};
}

Json receipts(const model::ResourceCloudReceiptPage &value)
{
    auto items = Json::array();
    for (const auto &item : value.items)
        items.push_back({{"id", item.id}, {"owner_type", model::resourceOwnerName(item.resource.owner)}, {"resource_id", item.resource.resource_id}, {"provider", model::resourceDestinationName(item.provider)}, {"adapter", item.adapter}, {"task_state", item.task_state}, {"content_generation", item.confirmed.content_generation}, {"target_revision", item.confirmed.target_revision}, {"current_content_generation", item.current_version.content_generation}, {"current_target_revision", item.current_version.target_revision}, {"size_bytes", item.size_bytes}, {"checksum", item.checksum}, {"verification", item.verification}, {"verification_method", item.verification_method}, {"uploaded_at", item.uploaded_at ? Json(*item.uploaded_at) : Json(nullptr)}, {"verified_at", item.verified_at ? Json(*item.verified_at) : Json(nullptr)}, {"result_code", item.result_code}, {"path_resolved", item.path_resolved}, {"current", item.current}, {"matches_local", item.matches_local ? Json(*item.matches_local) : Json(nullptr)}, {"transfer_skipped", item.transfer_skipped}, {"assumed_stored", item.assumed_stored}});
    return {{"items", items}, {"next_after_id", value.next_after_id ? Json(*value.next_after_id) : Json(nullptr)}};
}

Json policyJson(const model::ResourceTransferJobPolicy &value)
{
    return {{"timeout_ms", value.timeout_ms}, {"retries", value.retries}, {"max_bytes", value.max_bytes}, {"bytes_per_second", value.bytes_per_second}};
}

http::HttpResponse error(application::result::ServiceErrorType type, const std::string &code)
{
    return http::resourceTransferError(type, code);
}

http::HttpResponse response(int status, const Json &value)
{
    auto result = http::HttpResponse::json(status, value.dump());
    result.headers["Access-Control-Allow-Origin"] = "*";
    return result;
}

} // namespace orchestrator::modules::crawler::api::routing::transfer_api
