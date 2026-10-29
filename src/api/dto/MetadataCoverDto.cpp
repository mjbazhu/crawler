#include "crawler/api/dto/MetadataCoverDto.h"

#include <charconv>
#include <map>
#include <set>
#include <string_view>

namespace orchestrator::modules::crawler::api::dto {
namespace {
using json = nlohmann::json;
namespace model = application::model;

// 所有者是协议枚举，不允许通过输入选择任意表。
std::optional<model::CoverOwner> ownerType(const std::string &value)
{
    if (value == "singer") return model::CoverOwner::Singer;
    if (value == "album") return model::CoverOwner::Album;
    return std::nullopt;
}

// JSON 数字必须为整数并在声明边界内，拒绝浮点、负数和溢出。
bool integer(const json &object, const char *name, int &value, int minimum, int maximum)
{
    if (!object.contains(name)) return true;
    const auto &field = object.at(name);
    if (!field.is_number_integer()) return false;
    if (field.is_number_unsigned() && field.get<std::uint64_t>() > static_cast<std::uint64_t>(maximum)) return false;
    const auto parsed = field.get<std::int64_t>();
    if (parsed < minimum || parsed > maximum) return false;
    value = static_cast<int>(parsed);
    return true;
}

// 限制受理的数组长度，正整数 ID 保持 uint64 精度。
bool ids(const json &value, std::vector<std::uint64_t> &result)
{
    if (!value.is_array() || value.size() > 200) return false;
    for (const auto &id : value) {
        if (!id.is_number_integer() || (!id.is_number_unsigned() && id.get<std::int64_t>() <= 0)) return false;
        const auto parsed = id.get<std::uint64_t>();
        if (parsed == 0) return false;
        result.push_back(parsed);
    }
    return true;
}

// 查询字符串仅接受完整的十进制数字，不容忍尾随字符。
std::optional<std::uint64_t> number(std::string_view text)
{
    std::uint64_t value = 0;
    const auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (text.empty() || parsed.ec != std::errc {} || parsed.ptr != text.data() + text.size()) return std::nullopt;
    return value;
}

// 从有界 CSV 参数解析 ID 或状态。
bool csv(std::string_view text, std::vector<std::uint64_t> &values)
{
    while (!text.empty()) {
        const auto comma = text.find(',');
        auto value = number(text.substr(0, comma));
        if (!value || values.size() >= 200) return false;
        values.push_back(*value);
        if (comma == std::string_view::npos) return true;
        text.remove_prefix(comma + 1);
        if (text.empty()) return false;
    }
    return false;
}
} // namespace

std::optional<MetadataCoverRequestDto> MetadataCoverRequestDto::fromRequest(const http::HttpRequest &request)
{
    try {
        const auto body = json::parse(request.body);
        if (!body.is_object() || !body.contains("owner_type") || !body.at("owner_type").is_string()) return std::nullopt;
        const std::set<std::string> allowed {"owner_type", "source", "resource_ids", "status", "cloud_status", "limit", "async", "refresh", "concurrency", "retries", "timeout_ms", "stale_after_seconds"};
        for (const auto &[key, value] : body.items())
            if (!allowed.contains(key)) return std::nullopt;
        const auto owner = ownerType(body.at("owner_type").get<std::string>());
        if (!owner) return std::nullopt;
        MetadataCoverRequestDto dto;
        dto.options.query.owner = *owner;
        if (body.contains("source") && (!body.at("source").is_string() || body.at("source") != "migu")) return std::nullopt;
        if (body.contains("resource_ids") && !ids(body.at("resource_ids"), dto.options.query.resource_ids)) return std::nullopt;
        if (body.contains("status")) {
            if (!body.at("status").is_array() || body.at("status").size() > 4) return std::nullopt;
            for (const auto &value : body.at("status")) {
                if (!value.is_number_integer() || value < 0 || value > 3) return std::nullopt;
                dto.options.query.statuses.push_back(value.get<int>());
            }
        }
        if (body.contains("cloud_status")) {
            const auto &statuses = body.at("cloud_status");
            if (!statuses.is_array() || statuses.size() > 5) return std::nullopt;
            for (const auto &value : statuses) {
                if (!value.is_number_integer() || value < 0 || value > 4) return std::nullopt;
                dto.options.query.cloud_statuses.push_back(value.get<int>());
            }
        }
        if (!integer(body, "limit", dto.options.limit, 1, 200) || !integer(body, "concurrency", dto.options.concurrency, 1, 32) || !integer(body, "retries", dto.options.retries, 0, 5) || !integer(body, "timeout_ms", dto.options.timeout_ms, 100, 600000) || !integer(body, "stale_after_seconds", dto.stale_after_seconds, 1, 604800)) return std::nullopt;
        if (body.contains("async")) {
            if (!body.at("async").is_boolean()) return std::nullopt;
            dto.async = body.at("async").get<bool>();
        }
        if (body.contains("refresh")) {
            if (!body.at("refresh").is_boolean()) return std::nullopt;
            dto.options.refresh = body.at("refresh").get<bool>();
        }
        if (dto.options.refresh && dto.options.query.resource_ids.empty()) return std::nullopt;
        return dto;
    } catch (const json::exception &) {
        return std::nullopt;
    }
}

std::optional<model::MetadataCoverQuery> metadataCoverQuery(const http::HttpRequest &request)
{
    model::MetadataCoverQuery query;
    const auto owner = request.query_params.find("owner_type");
    if (owner == request.query_params.end()) return std::nullopt;
    const auto type = ownerType(owner->second);
    if (!type) return std::nullopt;
    query.owner = *type;
    const std::set<std::string> allowed {"owner_type", "source", "resource_ids", "status", "cloud_status", "page", "page_size"};
    for (const auto &[key, value] : request.query_params) {
        if (!allowed.contains(key)) return std::nullopt;
        if (key == "source" && value != "migu") return std::nullopt;
        if (key == "resource_ids") {
            if (!csv(value, query.resource_ids)) return std::nullopt;
            for (const auto id : query.resource_ids)
                if (id == 0) return std::nullopt;
        }
        if (key == "status" || key == "cloud_status") {
            std::vector<std::uint64_t> statuses;
            const bool cloud = key == "cloud_status";
            if (!csv(value, statuses) || statuses.size() > (cloud ? 5 : 4)) return std::nullopt;
            for (const auto status : statuses) {
                if (status > (cloud ? 4U : 3U)) return std::nullopt;
                (cloud ? query.cloud_statuses : query.statuses).push_back(static_cast<int>(status));
            }
        }
        if (key == "page" || key == "page_size") {
            const auto parsed = number(value);
            if (!parsed || *parsed < 1 || *parsed > (key == "page" ? 1000000U : 200U)) return std::nullopt;
            if (key == "page")
                query.page = static_cast<int>(*parsed);
            else
                query.page_size = static_cast<int>(*parsed);
        }
    }
    return query;
}

json metadataCoverToJson(const model::MetadataCoverRecord &record)
{
    const std::string url = "/api/migu/covers/" + std::string(model::coverOwnerName(record.owner)) + '/' + std::to_string(record.resource_id) + "/content";
    const auto &cloud = record.cloud;
    return {{"owner_type", model::coverOwnerName(record.owner)}, {"resource_id", record.resource_id}, {"source_ref_id", record.source_ref_id}, {"source", record.source}, {"source_id", record.source_id}, {"status", record.file.status}, {"available", record.available}, {"local_url", record.available ? json(url) : json(nullptr)}, {"local_path", record.file.local_path}, {"size_bytes", record.file.size_bytes}, {"checksum", record.file.checksum}, {"error_category", record.file.error_category}, {"result_code", record.file.result_code}, {"last_error", record.file.last_error}, {"cloud", {{"enabled", cloud.enabled}, {"status", cloud.status}, {"current", cloud.current}, {"backend", cloud.receipt.backend}, {"path", cloud.receipt.path}, {"size_bytes", cloud.receipt.size_bytes}, {"checksum", cloud.receipt.checksum}, {"uploaded_at", cloud.uploaded_at}, {"error_category", cloud.error_category}, {"result_code", cloud.result_code}, {"last_error", cloud.last_error}}}};
}

bool attachMetadataCovers(json &body, model::CoverOwner owner, const std::shared_ptr<application::use_cases::MetadataCoverService> &service)
{
    std::vector<json *> records;
    if (body.contains("items") && body["items"].is_array()) {
        for (auto &item : body["items"]) records.push_back(&item);
    } else
        records.push_back(&body);
    model::MetadataCoverQuery query;
    query.owner = owner;
    query.page_size = 200;
    for (auto *record : records) {
        (*record)["cover"] = nullptr;
        if (record->contains("source_ref_id")) query.source_ref_ids.push_back((*record)["source_ref_id"].get<std::uint64_t>());
    }
    if (!service || query.source_ref_ids.empty()) return true;
    const auto page = service->list(query);
    if (!page.isSuccess()) return false;
    std::map<std::uint64_t, json> covers;
    for (const auto &cover : page.data()->items) covers.emplace(cover.source_ref_id, metadataCoverToJson(cover));
    for (auto *record : records) {
        const auto found = covers.find((*record)["source_ref_id"].get<std::uint64_t>());
        if (found != covers.end()) (*record)["cover"] = found->second;
    }
    return true;
}

} // namespace orchestrator::modules::crawler::api::dto
