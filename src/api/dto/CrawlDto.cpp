#include "crawler/api/dto/CrawlDto.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <charconv>
#include <cstddef>
#include <cstdint>
#include <initializer_list>
#include <string>

using json = nlohmann::json;

namespace orchestrator::modules::crawler::api {
namespace dto {

namespace {

constexpr int kDefaultLimit = 20;
constexpr int kMaxLimit = 500;
constexpr std::size_t kMaxTargets = 1000;

// 解析 limit，未提供时使用调用方默认值。
int parseLimit(const json &body, int fallback = kDefaultLimit)
{
    if (!body.contains("limit") || !body["limit"].is_number_integer()) {
        return fallback;
    }
    return body["limit"].get<int>();
}

// 解析整数区间字段。
int parseIntField(const json &body, std::initializer_list<const char *> names, int fallback)
{
    for (const char *name : names) {
        if (body.contains(name) && body[name].is_number_integer()) {
            return body[name].get<int>();
        }
    }
    return fallback;
}

// 解析可选整数区间字段。
std::optional<int> parseOptionalIntField(const json &body, std::initializer_list<const char *> names)
{
    for (const char *name : names) {
        if (body.contains(name) && body[name].is_number_integer()) {
            return body[name].get<int>();
        }
    }
    return std::nullopt;
}

// 解析布尔字段。
bool parseBoolField(const json &body, std::initializer_list<const char *> names, bool fallback)
{
    for (const char *name : names) {
        if (body.contains(name) && body[name].is_boolean()) {
            return body[name].get<bool>();
        }
    }
    return fallback;
}

// 解析可选布尔字段。
std::optional<bool> parseOptionalBoolField(const json &body, std::initializer_list<const char *> names)
{
    for (const char *name : names) {
        if (body.contains(name) && body[name].is_boolean()) {
            return body[name].get<bool>();
        }
    }
    return std::nullopt;
}

// 读取第一个字符串字段。
std::optional<std::string> firstStringField(const json &body, std::initializer_list<const char *> names)
{
    for (const char *name : names) {
        if (body.contains(name) && body[name].is_string()) {
            return body[name].get<std::string>();
        }
    }
    return std::nullopt;
}

// 读取第一个字符串数组字段。
std::vector<std::string> firstStringArrayField(const json &body, std::initializer_list<const char *> names)
{
    for (const char *name : names) {
        if (body.contains(name) && body[name].is_array()) {
            std::vector<std::string> values;
            for (const auto &item : body[name]) {
                if (item.is_string()) {
                    values.push_back(item.get<std::string>());
                }
            }
            return values;
        }
    }
    return {};
}

// 读取第一个整数或整数数组字段。
std::vector<int> firstIntListField(const json &body, std::initializer_list<const char *> names)
{
    for (const char *name : names) {
        if (!body.contains(name)) {
            continue;
        }
        if (body[name].is_number_integer()) {
            return {body[name].get<int>()};
        }
        if (body[name].is_array()) {
            std::vector<int> values;
            for (const auto &item : body[name]) {
                if (item.is_number_integer()) {
                    values.push_back(item.get<int>());
                }
            }
            return values;
        }
    }
    return {};
}

// 判断是否是当前支持的 Migu 歌手分类。
bool isValidSingerTab(const std::string &tab)
{
    static constexpr const char *kTabs[] = {
        "huayu-nan",
        "huayu-nv",
        "huayu-group",
        "oumei-nan",
        "oumei-nv",
        "oumei-group",
        "rihan-nan",
        "rihan-nv",
        "rihan-group",
    };
    return std::find(std::begin(kTabs), std::end(kTabs), tab) != std::end(kTabs);
}

// 校验目标数量和区间边界。
std::optional<std::string> validateSelectionRange(std::size_t size, int start_index, std::optional<int> end_index)
{
    if (size == 0) {
        return "targets cannot be empty";
    }
    if (size > kMaxTargets) {
        return "targets cannot exceed 1000 items";
    }
    if (start_index < 0 || start_index >= static_cast<int>(size)) {
        return "start_index must point to an existing target";
    }
    if (end_index && (*end_index <= start_index || *end_index > static_cast<int>(size))) {
        return "end_index must be greater than start_index and at most targets.size";
    }
    return std::nullopt;
}

// 校验通用 limit 和区间。
std::optional<std::string> validateSelection(std::size_t size, int limit, int start_index, std::optional<int> end_index)
{
    if (auto error = validateSelectionRange(size, start_index, end_index)) {
        return error;
    }
    if (limit <= 0 || limit > kMaxLimit) {
        return "limit must be between 1 and 500";
    }
    return std::nullopt;
}

// 校验歌手分类入口，limit=0 表示当前分类全部歌手。
std::optional<std::string> validateSingerIndexSelection(std::size_t size, int limit, int start_index, std::optional<int> end_index)
{
    if (auto error = validateSelectionRange(size, start_index, end_index)) {
        return error;
    }
    if (limit < 0) {
        return "limit must be 0 or greater";
    }
    return std::nullopt;
}

// 校验歌手 ID 采集入口，limit=0 表示每个歌手全部歌曲或专辑。

// 选择字符串区间。
std::vector<std::string> selectedStrings(const std::vector<std::string> &values, int start_index, std::optional<int> end_index)
{
    const int end = end_index.value_or(static_cast<int>(values.size()));
    return std::vector<std::string>(
        values.begin() + static_cast<std::ptrdiff_t>(start_index),
        values.begin() + static_cast<std::ptrdiff_t>(end)
    );
}

// 选择 ID 区间。

} // namespace

std::optional<MiguSingerProfileRefreshRequestDto> MiguSingerProfileRefreshRequestDto::fromRequest(const http::HttpRequest &req)
{
    try {
        const auto body = req.body.empty() ? json::object() : json::parse(req.body);
        if (!body.is_object()) return std::nullopt;
        for (const auto &entry : body.items()) {
            const auto &key = entry.key();
            if (key != "source" && key != "force" && key != "limit" && key != "source_ids" && key != "filter") return std::nullopt;
        }
        MiguSingerProfileRefreshRequestDto dto;
        if (body.contains("source")) dto.query.source = body.at("source").get<std::string>();
        if (body.contains("force")) dto.query.force = body.at("force").get<bool>();
        if (body.contains("limit")) {
            if (!body.at("limit").is_number_integer()) return std::nullopt;
            const auto limit = body.at("limit").get<std::int64_t>();
            if (limit < 0 || limit > kMaxLimit) return std::nullopt;
            dto.query.limit = static_cast<int>(limit);
        }
        if (body.contains("source_ids")) {
            dto.query.source_ids = body.at("source_ids").get<std::vector<std::string>>();
            if (dto.query.source_ids.empty()) return std::nullopt;
        }
        const auto filter = body.value("filter", json::object());
        if (!filter.is_object()) return std::nullopt;
        for (const auto &entry : filter.items()) {
            if (entry.key() != "q" && entry.key() != "country" && entry.key() != "gender") return std::nullopt;
        }
        if (filter.contains("q")) dto.query.keyword = filter.at("q").get<std::string>();
        if (filter.contains("country")) dto.query.country = filter.at("country").get<std::string>();
        if (filter.contains("gender")) {
            if (!filter.at("gender").is_number_integer()) return std::nullopt;
            const auto gender = filter.at("gender").get<std::int64_t>();
            if (gender < 0 || gender > 3) return std::nullopt;
            dto.query.gender = static_cast<int>(gender);
        }
        return dto;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::string> MiguSingerProfileRefreshRequestDto::validate() const
{
    if (query.source != "migu") return "source must be migu";
    if (query.source_ids.size() > 500) return "source_ids must contain at most 500 IDs";
    if (query.keyword.size() > 512 || query.country.size() > 128) return "singer profile filter is too long";
    for (const auto &id : query.source_ids) {
        std::uint64_t value = 0;
        const auto parsed = std::from_chars(id.data(), id.data() + id.size(), value);
        if (id.empty() || id.size() > 20 || parsed.ec != std::errc {} || parsed.ptr != id.data() + id.size() || value == 0 || id.front() == '0') return "source_ids must contain positive numeric singer IDs without leading zeros";
    }
    return std::nullopt;
}

std::optional<MiguSingerIndexCrawlRequestDto> MiguSingerIndexCrawlRequestDto::fromRequest(const http::HttpRequest &req)
{
    try {
        if (req.body.empty()) {
            return std::nullopt;
        }

        auto body = json::parse(req.body);
        MiguSingerIndexCrawlRequestDto dto;
        dto.limit = parseLimit(body);
        dto.start_index = parseIntField(body, {"start_index", "startPosition", "start"}, 0);
        dto.end_index = parseOptionalIntField(body, {"end_index", "endPosition", "end"});
        dto.tabs = firstStringArrayField(body, {"tabs", "singer_tabs", "singerTabs"});
        if (dto.tabs.empty()) {
            if (auto tab = firstStringField(body, {"tab", "singer_tab", "singerTab"})) {
                dto.tabs.push_back(*tab);
            }
        }
        return dto;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::string> MiguSingerIndexCrawlRequestDto::validate() const
{
    if (auto error = validateSingerIndexSelection(tabs.size(), limit, start_index, end_index)) {
        return error;
    }
    const auto invalid = std::find_if(tabs.begin(), tabs.end(), [](const std::string &tab) {
        return !isValidSingerTab(tab);
    });
    if (invalid != tabs.end()) {
        return "each tab must be a supported Migu singer category";
    }
    return std::nullopt;
}

std::vector<std::string> MiguSingerIndexCrawlRequestDto::selectedTargets() const
{
    auto selected = selectedStrings(tabs, start_index, end_index);
    for (auto &tab : selected) {
        tab = "singer-index:" + tab;
    }
    return selected;
}

std::optional<MiguSingerFromDbCrawlRequestDto> MiguSingerFromDbCrawlRequestDto::fromRequest(const http::HttpRequest &req)
{
    try {
        MiguSingerFromDbCrawlRequestDto dto;
        dto.query.source = "migu";
        dto.query.limit = 20;
        dto.limit_per_singer = 0;
        dto.query.sync_statuses = {0, 3, 4};

        if (req.body.empty()) {
            return dto;
        }

        auto body = json::parse(req.body);
        if (auto source = firstStringField(body, {"source"})) {
            dto.query.source = *source;
        }
        dto.query.limit = static_cast<std::size_t>(parseIntField(body, {"max_targets", "maxTargets", "target_limit", "targetLimit"}, 20));
        dto.limit_per_singer = parseIntField(body, {"limit_per_singer", "limitPerSinger", "per_singer_limit", "perSingerLimit"}, 0);
        if (body.contains("force") && !body["force"].is_boolean()) return std::nullopt;
        dto.query.force = body.value("force", false);
        dto.query.include_stale_running = parseBoolField(body, {"include_stale_running", "includeStaleRunning"}, false);
        dto.query.stale_after_seconds = parseIntField(body, {"stale_after_seconds", "staleAfterSeconds"}, 3600);

        const json *filter = &body;
        if (body.contains("filter") && body["filter"].is_object()) {
            filter = &body["filter"];
        }
        if (auto country = firstStringField(*filter, {"country", "source_country", "sourceCountry"})) {
            dto.query.country = *country;
        }
        if (auto keyword = firstStringField(*filter, {"q", "keyword", "name"})) {
            dto.query.keyword = *keyword;
        }
        dto.query.sync_statuses = firstIntListField(*filter, {"sync_status", "syncStatus", "sync_statuses", "syncStatuses", "status"});
        if (dto.query.sync_statuses.empty()) {
            dto.query.sync_statuses = {0, 3, 4};
        }
        if (auto gender = parseOptionalIntField(*filter, {"gender"})) {
            dto.query.gender = *gender;
        }
        return dto;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::string> MiguSingerFromDbCrawlRequestDto::validate() const
{
    if (query.source != "migu") {
        return "source must be migu";
    }
    if (query.limit == 0 || query.limit > kMaxTargets) {
        return "max_targets must be between 1 and 1000";
    }
    if (limit_per_singer < 0 || limit_per_singer > kMaxLimit) {
        return "limit_per_singer must be between 0 and 500";
    }
    if (query.country.size() > 128) {
        return "country is too long";
    }
    if (query.keyword.size() > 128) {
        return "q is too long";
    }
    if (query.gender && (*query.gender < 0 || *query.gender > 3)) {
        return "gender must be between 0 and 3 when provided";
    }
    if (query.stale_after_seconds < 60 || query.stale_after_seconds > 86400) {
        return "stale_after_seconds must be between 60 and 86400";
    }
    for (int status : query.sync_statuses) {
        if (status < 0 || status > 4) {
            return "sync_status values must be between 0 and 4";
        }
    }
    return std::nullopt;
}

std::optional<BatchControlRequestDto> BatchControlRequestDto::fromRequest(const http::HttpRequest &req)
{
    if (auto it = req.query_params.find("batch_id"); it != req.query_params.end()) {
        BatchControlRequestDto dto;
        dto.batch_id = it->second;
        return dto;
    }

    try {
        if (req.body.empty()) {
            return std::nullopt;
        }

        auto body = json::parse(req.body);
        if (!body.contains("batch_id") || !body["batch_id"].is_string()) {
            return std::nullopt;
        }

        BatchControlRequestDto dto;
        dto.batch_id = body["batch_id"].get<std::string>();
        return dto;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::string> BatchControlRequestDto::validate() const
{
    if (batch_id.empty()) {
        return "batch_id is required";
    }
    if (batch_id.size() > 128) {
        return "batch_id is too long";
    }
    return std::nullopt;
}

std::optional<MiguSettingsUpdateDto> MiguSettingsUpdateDto::fromRequest(const http::HttpRequest &req)
{
    try {
        if (req.body.empty()) {
            return std::nullopt;
        }

        auto body = json::parse(req.body);
        MiguSettingsUpdateDto dto;
        dto.update.max_batch_concurrency = parseOptionalIntField(body, {"max_batch_concurrency", "maxBatchConcurrency"});
        dto.update.max_page_concurrency = parseOptionalIntField(body, {"max_page_concurrency", "maxPageConcurrency"});
        dto.update.max_retries = parseOptionalIntField(body, {"max_retries", "maxRetries"});
        dto.update.task_timeout_ms = parseOptionalIntField(body, {"task_timeout_ms", "taskTimeoutMs"});
        dto.update.resource_download_concurrency = parseOptionalIntField(body, {"resource_download_concurrency", "resourceDownloadConcurrency"});
        dto.update.resource_download_retries = parseOptionalIntField(body, {"resource_download_retries", "resourceDownloadRetries"});
        dto.update.resource_download_timeout_ms = parseOptionalIntField(body, {"resource_download_timeout_ms", "resourceDownloadTimeoutMs"});
        dto.update.pacmtoken_refresh_interval_seconds = parseOptionalIntField(body, {"pacmtoken_refresh_interval_seconds", "pacmtokenRefreshIntervalSeconds"});
        dto.update.auto_refresh_pacmtoken = parseOptionalBoolField(body, {"auto_refresh_pacmtoken", "autoRefreshPacmtoken"});
        dto.update.refresh_pacmtoken_on_440013 = parseOptionalBoolField(body, {"refresh_pacmtoken_on_440013", "refreshPacmtokenOn440013"});
        return dto;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::string> MiguSettingsUpdateDto::validate() const
{
    if (!update.max_batch_concurrency && !update.max_page_concurrency && !update.max_retries && !update.task_timeout_ms && !update.resource_download_concurrency && !update.resource_download_retries && !update.resource_download_timeout_ms && !update.pacmtoken_refresh_interval_seconds && !update.auto_refresh_pacmtoken && !update.refresh_pacmtoken_on_440013) {
        return "at least one mutable setting is required";
    }
    return std::nullopt;
}

std::optional<MiguPacmtokenRefreshDto> MiguPacmtokenRefreshDto::fromRequest(const http::HttpRequest &req)
{
    try {
        MiguPacmtokenRefreshDto dto;
        if (req.body.empty()) {
            return dto;
        }

        auto body = json::parse(req.body);
        if (auto pacmtoken = firstStringField(body, {"pacmtoken", "pacmToken", "pacm_token"})) {
            dto.request.pacmtoken = *pacmtoken;
        } else if (auto cookie = firstStringField(body, {"cookie", "Cookie"})) {
            dto.request.pacmtoken = *cookie;
        }
        dto.request.store = parseBoolField(body, {"store", "update_runtime", "updateRuntime"}, true);
        return dto;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::string> MiguPacmtokenRefreshDto::validate() const
{
    constexpr std::size_t kMaxPacmtokenInput = 8192;
    if (request.pacmtoken.size() > kMaxPacmtokenInput) {
        return "pacmtoken is too long";
    }
    if (request.pacmtoken.find('\r') != std::string::npos || request.pacmtoken.find('\n') != std::string::npos) {
        return "pacmtoken contains invalid characters";
    }
    return std::nullopt;
}

}
} // namespace orchestrator::modules::crawler::api::dto
