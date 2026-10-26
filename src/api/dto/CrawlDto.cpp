#include "crawler/api/dto/CrawlDto.h"

#include <nlohmann/json.hpp>

#include <algorithm>
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

// 尝试把 JSON 值解析为正整数 ID。
std::optional<std::uint64_t> parseUnsignedId(const json &value)
{
    if (value.is_number_unsigned()) {
        const auto id = value.get<std::uint64_t>();
        return id == 0 ? std::nullopt : std::optional<std::uint64_t>(id);
    }
    if (value.is_number_integer()) {
        const auto id = value.get<long long>();
        return id <= 0 ? std::nullopt : std::optional<std::uint64_t>(static_cast<std::uint64_t>(id));
    }
    if (value.is_string()) {
        try {
            std::size_t parsed = 0;
            const std::string text = value.get<std::string>();
            const auto id = std::stoull(text, &parsed);
            if (parsed == text.size() && id > 0) {
                return id;
            }
        } catch (...) {
            return std::nullopt;
        }
    }
    return std::nullopt;
}

// 读取第一个 ID 字段。
std::optional<std::uint64_t> firstIdField(const json &body, std::initializer_list<const char *> names)
{
    for (const char *name : names) {
        if (body.contains(name)) {
            return parseUnsignedId(body[name]);
        }
    }
    return std::nullopt;
}

// 读取第一个 ID 数组字段。
std::vector<std::uint64_t> firstIdArrayField(const json &body, std::initializer_list<const char *> names)
{
    for (const char *name : names) {
        if (body.contains(name) && body[name].is_array()) {
            std::vector<std::uint64_t> ids;
            for (const auto &item : body[name]) {
                if (auto id = parseUnsignedId(item)) {
                    ids.push_back(*id);
                }
            }
            return ids;
        }
    }
    return {};
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
std::optional<std::string> validateSingerIdsSelection(std::size_t size, int limit, int start_index, std::optional<int> end_index)
{
    if (auto error = validateSelectionRange(size, start_index, end_index)) {
        return error;
    }
    if (limit < 0 || limit > kMaxLimit) {
        return "limit must be between 0 and 500";
    }
    return std::nullopt;
}

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
std::vector<std::uint64_t> selectedIds(const std::vector<std::uint64_t> &values, int start_index, std::optional<int> end_index)
{
    const int end = end_index.value_or(static_cast<int>(values.size()));
    return std::vector<std::uint64_t>(
        values.begin() + static_cast<std::ptrdiff_t>(start_index),
        values.begin() + static_cast<std::ptrdiff_t>(end)
    );
}

} // namespace

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

std::optional<MiguSingerIdsCrawlRequestDto> MiguSingerIdsCrawlRequestDto::fromRequest(const http::HttpRequest &req)
{
    try {
        if (req.body.empty()) {
            return std::nullopt;
        }

        auto body = json::parse(req.body);
        MiguSingerIdsCrawlRequestDto dto;
        dto.limit = parseLimit(body, 0);
        dto.start_index = parseIntField(body, {"start_index", "startPosition", "start"}, 0);
        dto.end_index = parseOptionalIntField(body, {"end_index", "endPosition", "end"});
        dto.singer_ids = firstIdArrayField(body, {"singer_ids", "singerIds"});
        if (dto.singer_ids.empty()) {
            if (auto id = firstIdField(body, {"singer_id", "singerId"})) {
                dto.singer_ids.push_back(*id);
            }
        }
        return dto;
    } catch (...) {
        return std::nullopt;
    }
}

std::optional<std::string> MiguSingerIdsCrawlRequestDto::validate() const
{
    return validateSingerIdsSelection(singer_ids.size(), limit, start_index, end_index);
}

std::vector<std::string> MiguSingerIdsCrawlRequestDto::selectedSongTargets() const
{
    auto ids = selectedIds(singer_ids, start_index, end_index);
    std::vector<std::string> targets;
    targets.reserve(ids.size());
    for (std::uint64_t id : ids) {
        targets.push_back("singer:" + std::to_string(id));
    }
    return targets;
}

std::vector<std::string> MiguSingerIdsCrawlRequestDto::selectedAlbumTargets() const
{
    auto ids = selectedIds(singer_ids, start_index, end_index);
    std::vector<std::string> targets;
    targets.reserve(ids.size());
    for (std::uint64_t id : ids) {
        targets.push_back("singer-albums:" + std::to_string(id));
    }
    return targets;
}

std::optional<MiguAlbumBackfillRequestDto> MiguAlbumBackfillRequestDto::fromRequest(const http::HttpRequest &req)
{
    try {
        MiguAlbumBackfillRequestDto dto;
        if (req.body.empty()) {
            return dto;
        }

        auto body = json::parse(req.body);
        dto.limit = parseLimit(body);
        if (auto source = firstStringField(body, {"source"})) {
            dto.source = *source;
        }
        return dto;
    } catch (...) {
        return std::nullopt;
    }
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

std::optional<std::string> MiguAlbumBackfillRequestDto::validate() const
{
    if (source != "migu") {
        return "source must be migu";
    }
    if (limit <= 0 || limit > kMaxLimit) {
        return "limit must be between 1 and 500";
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
