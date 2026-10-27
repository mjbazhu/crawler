#include "crawler/application/use_cases/CrawlerService.h"

#include "crawler/application/use_cases/AsyncResourcePortAdapters.h"
#include "CollectionWorkflowAssembly.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cctype>
#include <cstdint>
#include <ctime>
#include <exception>
#include <limits>
#include <mutex>
#include <optional>
#include <sstream>
#include <set>
#include <string_view>
#include <unordered_map>
#include <utility>
#include <vector>

namespace orchestrator::modules::crawler::application::use_cases {

namespace {

constexpr int kDefaultLimit = 20;
constexpr int kDefaultBatchConcurrency = 4;
constexpr int kDefaultPageConcurrency = 4;
constexpr int kDefaultRetries = 1;
constexpr int kDefaultResourceDownloadConcurrency = 3;
constexpr int kDefaultResourceDownloadRetries = 1;
constexpr int kDefaultResourceDownloadTimeoutMs = 120000;
constexpr int kDefaultPacmtokenRefreshIntervalSeconds = 300;

// 统一的失败分类，用于批次 item 和前端失败治理。
struct FailureClassification {
    std::string category; // 批次失败分类
    std::string code;     // 稳定结果编码
};

// 归一化业务 limit，保持旧用例入口行为。
int normalizeLimit(int limit)
{
    if (limit <= 0) {
        return kDefaultLimit;
    }
    return (std::min)(limit, 500);
}

// 歌手分类采集的 limit=0 表示当前分类全部，正数不做 500 上限截断。
int normalizeSingerIndexLimit(int limit)
{
    return limit < 0 ? kDefaultLimit : limit;
}

// 歌手歌曲/专辑采集的 limit=0 表示全部，正数仍保留 500 上限。
int normalizeSingerIdsLimit(int limit)
{
    if (limit < 0) {
        return kDefaultLimit;
    }
    if (limit == 0) {
        return 0;
    }
    return (std::min)(limit, 500);
}

// 判断是否是手动歌手歌曲/专辑批次。
bool isSingerIdsTaskType(std::string_view task_type)
{
    return task_type == "singerSongs"
           || task_type == "singerAlbums"
           || task_type == "singerSongsFromDb"
           || task_type == "singerAlbumsFromDb"
           || task_type == "singerSongsRetry"
           || task_type == "singerAlbumsRetry";
}

// 按批次类型归一化每目标 limit，避免特殊入口被通用规则改成 20。
int normalizeLimitForTask(int limit, std::string_view task_type)
{
    if (task_type == "singerIndex") {
        return normalizeSingerIndexLimit(limit);
    }
    if (isSingerIdsTaskType(task_type)) {
        return normalizeSingerIdsLimit(limit);
    }
    return normalizeLimit(limit);
}

// 按解析后的目标类型归一化 limit。
int normalizeLimitForTarget(int limit, CrawlTargetKind kind)
{
    if (kind == CrawlTargetKind::SingerIndex) {
        return normalizeSingerIndexLimit(limit);
    }
    if (kind == CrawlTargetKind::SingerSongs || kind == CrawlTargetKind::SingerAlbums) {
        return normalizeSingerIdsLimit(limit);
    }
    return normalizeLimit(limit);
}

// 归一化从库认领目标数量。
std::size_t normalizeTargetLimit(std::size_t limit)
{
    if (limit == 0) {
        return static_cast<std::size_t>(kDefaultLimit);
    }
    return (std::min<std::size_t>)(limit, 1000);
}

// 资源下载选项范围校验和归一化。
application::model::SongResourceProcessOptions normalizeResourceProcessOptions(application::model::SongResourceProcessOptions options)
{
    options.resource_download_concurrency = (std::min<std::size_t>)((std::max<std::size_t>)(1, options.resource_download_concurrency), 32);
    options.resource_download_retries = (std::min)((std::max)(0, options.resource_download_retries), 5);
    options.resource_download_timeout_ms = (std::min)((std::max)(10000, options.resource_download_timeout_ms), 600000);
    options.pacmtoken_refresh_interval_seconds = (std::min)((std::max)(0, options.pacmtoken_refresh_interval_seconds), 3600);
    return options;
}

// 将运行期默认设置和单次请求覆盖项合并为资源处理有效选项。
application::model::SongResourceProcessOptions effectiveResourceProcessOptions(const application::model::CrawlerRuntimeSettings &settings, const application::model::SongResourceProcessOptionOverrides &overrides)
{
    application::model::SongResourceProcessOptions options;
    options.resource_download_concurrency = static_cast<std::size_t>(settings.resource_download_concurrency);
    options.resource_download_retries = settings.resource_download_retries;
    options.resource_download_timeout_ms = settings.resource_download_timeout_ms;
    options.pacmtoken_refresh_interval_seconds = settings.pacmtoken_refresh_interval_seconds;
    options.auto_refresh_pacmtoken = settings.auto_refresh_pacmtoken;
    options.refresh_pacmtoken_on_440013 = settings.refresh_pacmtoken_on_440013;
    if (overrides.resource_download_concurrency) {
        options.resource_download_concurrency = static_cast<std::size_t>(*overrides.resource_download_concurrency);
    }
    if (overrides.resource_download_retries) {
        options.resource_download_retries = *overrides.resource_download_retries;
    }
    if (overrides.resource_download_timeout_ms) {
        options.resource_download_timeout_ms = *overrides.resource_download_timeout_ms;
    }
    if (overrides.pacmtoken_refresh_interval_seconds) {
        options.pacmtoken_refresh_interval_seconds = *overrides.pacmtoken_refresh_interval_seconds;
    }
    if (overrides.auto_refresh_pacmtoken) {
        options.auto_refresh_pacmtoken = *overrides.auto_refresh_pacmtoken;
    }
    if (overrides.refresh_pacmtoken_on_440013) {
        options.refresh_pacmtoken_on_440013 = *overrides.refresh_pacmtoken_on_440013;
    }
    if (overrides.process_related_resources) {
        options.process_related_resources = *overrides.process_related_resources;
    }
    return normalizeResourceProcessOptions(options);
}

// 转成小写用于错误文本的保守分类。
std::string lowerCopy(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

// 判断错误文本中是否出现指定关键字。
bool containsAny(const std::string &value, std::initializer_list<const char *> keywords)
{
    return std::any_of(keywords.begin(), keywords.end(), [&value](const char *keyword) {
        return value.find(keyword) != std::string::npos;
    });
}

// 判断恢复请求是否包含指定类型；空类型列表表示全部恢复。
bool includesRecoveryType(const std::vector<std::string> &types, std::string_view type)
{
    return types.empty() || std::find(types.begin(), types.end(), type) != types.end();
}

// 不可命中的状态哨兵，用于用户只选择不可治理状态时避免回退到默认范围。
constexpr int kNoResourceStatus = -999;

// 资源重试默认只治理失败记录；status=4 是永久跳过，不重新排队。
std::vector<int> defaultResourceRetryStatuses(std::vector<int> statuses)
{
    if (statuses.empty()) {
        return {3};
    }
    std::vector<int> values;
    values.reserve(statuses.size());
    for (int status : statuses) {
        if (status == 3 && std::find(values.begin(), values.end(), status) == values.end()) {
            values.push_back(status);
        }
    }
    return values.empty() ? std::vector<int> {kNoResourceStatus} : values;
}

// 资源重置默认覆盖未完成可恢复资源；status=4 是永久跳过，不重新排队。
std::vector<int> defaultResourceResetStatuses(std::vector<int> statuses)
{
    if (statuses.empty()) {
        return {0, 1, 3};
    }
    std::vector<int> values;
    values.reserve(statuses.size());
    for (int status : statuses) {
        if ((status == 0 || status == 1 || status == 3) && std::find(values.begin(), values.end(), status) == values.end()) {
            values.push_back(status);
        }
    }
    return values.empty() ? std::vector<int> {kNoResourceStatus} : values;
}

// 将资源队列记录转换为治理明细。
application::model::MiguResourceActionItem resourceActionItemFromRecord(const application::model::MiguResourceQueueRecord &resource, std::string reason, int status)
{
    application::model::MiguResourceActionItem item;
    item.resource_id = resource.resource_id;
    item.source_id = resource.source_song_id.empty() ? resource.source_content_id : resource.source_song_id;
    item.name = resource.title;
    item.status = status;
    item.reason = std::move(reason);
    return item;
}

// 当前 Unix 秒时间戳。
std::uint64_t nowSeconds()
{
    return static_cast<std::uint64_t>(std::time(nullptr));
}

// 根据脱敏错误文本映射稳定失败分类。
FailureClassification classifyFailure(const std::string &error)
{
    const std::string value = lowerCopy(error);
    if (containsAny(value, {"timeout", "timed out"})) {
        return {"timeout", "timeout"};
    }
    if (containsAny(value, {"network", "http", "connect", "connection", "tls", "ssl", "certificate", "request", "response code"})) {
        return {"network", "network_failed"};
    }
    if (containsAny(value, {"parse", "parser", "json", "invalid response", "malformed"})) {
        return {"parse", "parse_failed"};
    }
    if (containsAny(value, {"mysql", "sql", "database", "persist", "insert", "update", "schema", "foreign key"})) {
        return {"persistence", "persist_failed"};
    }
    if (containsAny(value, {"invalid target", "invalid singer", "invalid album", "not found"})) {
        return {"invalid_target", "invalid_target"};
    }
    return {"unknown", "failed"};
}

// 构造失败批次结果，确保分类和编码不为空。
BatchTaskManager::TargetResult failureTargetResult(std::string error)
{
    auto classification = classifyFailure(error);
    return {false, 0, std::move(error), std::move(classification.category), std::move(classification.code)};
}

// 将 bool 兼容入口转换为批次目标结果。
BatchTaskManager::TargetResult boolTargetResult(bool success)
{
    if (success) {
        return {true, 1, {}, {}, "ok"};
    }
    return failureTargetResult("target processing failed");
}

// 解析平台歌手 ID。
std::uint64_t parseSingerId(const std::string &value)
{
    try {
        std::size_t parsed = 0;
        const auto id = std::stoull(value, &parsed);
        return parsed == value.size() ? id : 0;
    } catch (...) {
        return 0;
    }
}

// 仅用于批次 item payload 的最小 JSON 字符串转义。
std::string escapeJsonString(const std::string &value)
{
    std::string escaped;
    escaped.reserve(value.size());
    for (char ch : value) {
        switch (ch) {
        case '\\': escaped += "\\\\"; break;
        case '"': escaped += "\\\""; break;
        case '\n': escaped += "\\n"; break;
        case '\r': escaped += "\\r"; break;
        case '\t': escaped += "\\t"; break;
        default: escaped.push_back(ch); break;
        }
    }
    return escaped;
}

// 生成资源处理批次 ID，用于关联资源回写记录。
std::string generateResourceProcessBatchId()
{
    static std::atomic<std::uint64_t> sequence {0};
    std::stringstream ss;
    ss << "resource_" << nowSeconds() << '_' << sequence.fetch_add(1);
    return ss.str();
}

// 生成资源处理请求快照，避免把临时 URL 或 Cookie 类敏感信息写入批次表。
std::string resourceProcessRequestJson(const application::model::SongResourceClaimQuery &query)
{
    std::ostringstream out;
    out << "{\"source\":\"" << escapeJsonString(query.source) << '"'
        << ",\"limit\":" << query.limit
        << ",\"offset\":" << query.offset
        << ",\"filter\":{";
    bool first = true;
    const auto append_string = [&out, &first](const char *name, const std::string &value) {
        if (value.empty()) {
            return;
        }
        if (!first) {
            out << ',';
        }
        first = false;
        out << '"' << name << "\":\"" << escapeJsonString(value) << '"';
    };
    append_string("kind", query.resource_kind);
    append_string("format", query.format_type);
    append_string("content_id", query.source_content_id);
    if (!query.resource_ids.empty()) {
        if (!first) {
            out << ',';
        }
        first = false;
        out << "\"resource_ids\":[";
        for (std::size_t index = 0; index < query.resource_ids.size(); ++index) {
            if (index > 0) {
                out << ',';
            }
            out << query.resource_ids[index];
        }
        out << ']';
    }
    if (!query.statuses.empty()) {
        if (!first) {
            out << ',';
        }
        first = false;
        out << "\"status\":[";
        for (std::size_t index = 0; index < query.statuses.size(); ++index) {
            if (index > 0) {
                out << ',';
            }
            out << query.statuses[index];
        }
        out << ']';
    }
    out << "},\"options\":{"
        << "\"resource_download_concurrency\":" << query.options.resource_download_concurrency
        << ",\"resource_download_retries\":" << query.options.resource_download_retries
        << ",\"resource_download_timeout_ms\":" << query.options.resource_download_timeout_ms
        << ",\"pacmtoken_refresh_interval_seconds\":" << query.options.pacmtoken_refresh_interval_seconds
        << ",\"auto_refresh_pacmtoken\":" << (query.options.auto_refresh_pacmtoken ? "true" : "false")
        << ",\"refresh_pacmtoken_on_440013\":" << (query.options.refresh_pacmtoken_on_440013 ? "true" : "false")
        << ",\"process_related_resources\":" << (query.options.process_related_resources ? "true" : "false")
        << "}}";
    return out.str();
}

// 生成资源批次 item payload，不记录真实下载 URL。
std::string resourceProcessItemPayload(const application::model::SongResourceProcessItem &item)
{
    std::ostringstream out;
    out << "{\"resource_id\":" << item.resource_id
        << ",\"song_source_ref_id\":" << item.song_source_ref_id
        << ",\"content_id\":\"" << escapeJsonString(item.source_content_id) << '"'
        << ",\"source_content_id\":\"" << escapeJsonString(item.source_content_id) << '"'
        << ",\"resource_kind\":\"" << escapeJsonString(item.resource_kind) << '"'
        << ",\"format_type\":\"" << escapeJsonString(item.format_type) << '"'
        << ",\"related\":" << (item.related ? "true" : "false")
        << ",\"synthetic\":" << (item.synthetic ? "true" : "false")
        << ",\"skipped\":" << (item.skipped ? "true" : "false")
        << ",\"local_path\":\"" << escapeJsonString(item.local_path) << '"'
        << ",\"cloud_backend\":\"" << escapeJsonString(item.cloud_backend) << '"'
        << ",\"cloud_path\":\"" << escapeJsonString(item.cloud_path) << '"'
        << ",\"size_bytes\":" << item.size_bytes
        << ",\"checksum\":\"" << escapeJsonString(item.checksum) << '"'
        << ",\"result_code\":\"" << escapeJsonString(item.result_code) << "\"}";
    return out.str();
}

// 提取简单 JSON object 的对象体，调用方只处理本模块手写的脱敏 payload。
std::string jsonObjectBody(std::string_view payload)
{
    const auto begin = payload.find_first_not_of(" \t\r\n");
    if (begin == std::string_view::npos) {
        return {};
    }
    const auto end = payload.find_last_not_of(" \t\r\n");
    if (payload[begin] != '{' || payload[end] != '}' || end <= begin) {
        return {};
    }
    return std::string(payload.substr(begin + 1, end - begin - 1));
}

// 判断 payload 顶层是否已经包含字段名，避免 recorder 补 trace 时产生重复 key。
bool jsonObjectContainsField(std::string_view payload, std::string_view key)
{
    const std::string needle = "\"" + std::string(key) + "\"";
    return payload.find(needle) != std::string_view::npos;
}

// 追加 JSON 字段分隔符。
void appendJsonSeparator(std::ostringstream &out, bool &first)
{
    if (!first) {
        out << ',';
    }
    first = false;
}

// 追加字符串字段；字段已存在时不覆盖 workflow 原始 payload。
void appendJsonStringFieldIfMissing(std::ostringstream &out, bool &first, std::string_view original, std::string_view key, std::string_view value)
{
    if (jsonObjectContainsField(original, key)) {
        return;
    }
    appendJsonSeparator(out, first);
    out << '"' << key << "\":\"" << escapeJsonString(std::string(value)) << '"';
}

// 追加原始 JSON 值字段；调用方负责保证 raw_value 是合法 JSON 标量或数组。
void appendJsonRawFieldIfMissing(std::ostringstream &out, bool &first, std::string_view original, std::string_view key, std::string_view raw_value)
{
    if (jsonObjectContainsField(original, key)) {
        return;
    }
    appendJsonSeparator(out, first);
    out << '"' << key << "\":" << raw_value;
}

// 把状态列表渲染为 JSON 数组。
std::string statusesJson(const std::vector<int> &statuses)
{
    std::ostringstream out;
    out << '[';
    for (std::size_t index = 0; index < statuses.size(); ++index) {
        if (index > 0) {
            out << ',';
        }
        out << statuses[index];
    }
    out << ']';
    return out.str();
}

// 在现有 JSON object 上补充 recorder 统一字段。
template <typename AppendFields>
std::string extendJsonObject(std::string_view payload, AppendFields append_fields)
{
    std::ostringstream out;
    out << '{';
    const std::string body = jsonObjectBody(payload);
    bool first = body.empty();
    if (!body.empty()) {
        out << body;
    }
    append_fields(out, first, payload);
    out << '}';
    return out.str();
}

// 计算稳定耗时，统一使用毫秒。
std::int64_t elapsedMillisecondsSince(std::chrono::steady_clock::time_point started_at)
{
    return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started_at).count();
}

// 资源处理批次落库 helper，只依赖批次仓储 port。
class ResourceProcessBatchRecorder : public SongResourceProcessListener {
public:
    ResourceProcessBatchRecorder(
        std::shared_ptr<application::ports::BatchTaskRepository> repository,
        application::model::SongResourceClaimQuery query,
        std::string settings_snapshot,
        std::shared_ptr<application::ports::DiagnosticsSink> diagnostics
    ) : repository_(std::move(repository)),
        query_(std::move(query)),
        settings_snapshot_(std::move(settings_snapshot)),
        diagnostics_(std::move(diagnostics))
    {}

    // 资源批次不使用普通目标队列，取消标志从其持久化批次读取。
    bool isCancellationRequested() override
    {
        if (!repository_ || query_.batch_id.empty()) return false;
        const auto batch = repository_->findBatch(query_.batch_id);
        return batch && batch->cancel_requested;
    }

    // 写入 Running 批次主记录。
    void begin()
    {
        if (query_.batch_id.empty()) {
            return;
        }
        const std::uint64_t now = nowSeconds();
        if (repository_) {
            application::model::CrawlBatchRecord batch;
            batch.batch_id = query_.batch_id;
            batch.source = query_.source.empty() ? "migu" : query_.source;
            batch.task_type = "resourceProcess";
            batch.state = "Running";
            batch.request_params = resourceProcessRequestJson(query_);
            batch.settings_snapshot = settings_snapshot_.empty() ? "{}" : settings_snapshot_;
            batch.limit_count = static_cast<int>(query_.limit);
            batch.created_at = now;
            batch.started_at = now;
            batch.updated_at = now;
            (void)repository_->createBatch(batch, {});
            (void)repository_->appendEvent(application::model::CrawlBatchEventRecord {query_.batch_id, std::nullopt, "batchStarted", "Resource process started", makeBatchEventPayload("{}"), now});
        }
        logEvent(application::ports::DiagnosticsLevel::Info, "batchStarted", "Resource process started", makeBatchEventPayload("{}"));
    }

    // 资源项开始时立即写入 Running item 和开始事件。
    void onItemStarted(const application::model::SongResourceProcessItem &item) override
    {
        if (query_.batch_id.empty()) {
            return;
        }

        const ItemSlot slot = ensureItemSlot(item);
        const std::uint64_t now = nowSeconds();
        application::model::CrawlBatchItemRecord record = makeItemRecord(item, slot, "Running", now);
        record.finished_at = 0;
        record.last_error.clear();
        record.error_category.clear();
        record.result_code.clear();
        record.written_count = 0;
        if (repository_) {
            (void)repository_->upsertItem(record);
            (void)repository_->appendEvent(application::model::CrawlBatchEventRecord {query_.batch_id, slot.item_index, "itemStarted", slot.item_key, makeItemEventPayload(item, slot, "{\"stage\":\"started\"}"), now});
        }
        logEvent(levelForEvent("itemStarted"), "itemStarted", "item started", record.payload.empty() ? "{}" : record.payload);
        updateRunningBatchProgress(now, {});
    }

    // 写入批次阶段事件，不绑定具体 item。
    void onBatchEvent(std::string event_type, std::string payload) override
    {
        if (query_.batch_id.empty()) {
            return;
        }

        const std::uint64_t now = nowSeconds();
        std::string message = event_type;
        const auto level = levelForEvent(event_type);
        const std::string event_payload = makeBatchEventPayload(payload.empty() ? "{}" : payload);
        logEvent(level, event_type, event_type, event_payload);
        if (repository_) {
            (void)repository_->appendEvent(application::model::CrawlBatchEventRecord {query_.batch_id, std::nullopt, std::move(event_type), std::move(message), event_payload, now});
        }
    }

    // 资源项结束时立即写入最终状态、结束事件和批次进度。
    void onItemFinished(const application::model::SongResourceProcessItem &item) override
    {
        if (query_.batch_id.empty()) {
            return;
        }

        const bool failed = !item.ok && !item.skipped;
        const ItemSlot slot = ensureItemSlot(item);
        const std::uint64_t now = nowSeconds();
        application::model::CrawlBatchItemRecord record = makeItemRecord(item, slot, item.result_code == "cancelled" ? "Cancelled" : (failed ? "Failed" : "Completed"), now);
        record.finished_at = now;
        record.last_error = failed ? item.message : "";
        if (repository_) {
            (void)repository_->upsertItem(record);
        }
        const std::string event_type = failed ? "itemFailed" : "itemCompleted";
        const std::string payload = makeItemEventPayload(item, slot, resourceProcessItemPayload(item), elapsedMillisecondsSince(slot.started_at));
        if (repository_) {
            (void)repository_->appendEvent(application::model::CrawlBatchEventRecord {query_.batch_id, slot.item_index, event_type, slot.item_key, payload, now});
        }
        logEvent(levelForEvent(event_type), event_type, failed ? "item failed" : "item completed", payload);

        updateCompletedCounters(failed, item.message, item.ok);
        updateRunningBatchProgress(now, failed ? item.message : std::string {});
    }

    // 写入资源项阶段事件，payload 由 workflow 保证脱敏。
    void onItemEvent(const application::model::SongResourceProcessItem &item, std::string event_type, std::string payload) override
    {
        if (query_.batch_id.empty()) {
            return;
        }

        const ItemSlot slot = ensureItemSlot(item);
        const std::uint64_t now = nowSeconds();
        const auto level = levelForEvent(event_type);
        const std::string event_payload = makeItemEventPayload(item, slot, payload.empty() ? "{}" : payload);
        logEvent(level, event_type, event_type, event_payload);
        if (repository_) {
            (void)repository_->appendEvent(application::model::CrawlBatchEventRecord {query_.batch_id, slot.item_index, std::move(event_type), slot.item_key, event_payload, now});
        }
    }

    // 记录 pacmtoken 刷新事件，只写长度和时间，不写 token 明文。
    void onPacmtokenRefreshed(bool refreshed, std::size_t pacmtoken_length, std::uint64_t updated_at) override
    {
        if (query_.batch_id.empty()) {
            return;
        }

        std::ostringstream payload;
        payload << "{\"refreshed\":" << (refreshed ? "true" : "false")
                << ",\"pacmtoken_length\":" << pacmtoken_length
                << ",\"updated_at\":" << updated_at
                << '}';
        const std::uint64_t now = nowSeconds();
        const std::string payload_text = makeBatchEventPayload(payload.str());
        if (repository_) {
            (void)repository_->appendEvent(application::model::CrawlBatchEventRecord {query_.batch_id, std::nullopt, "pacmtokenRefreshed", refreshed ? "refreshed" : "checked", payload_text, now});
        }
        logEvent(application::ports::DiagnosticsLevel::Info, "pacmtokenRefreshed", refreshed ? "pacmtoken refreshed" : "pacmtoken checked", payload_text);
    }

    // 写入最终批次状态；item 明细已经在 listener 回调中实时写入。
    void finish(const application::model::SongResourceProcessResult &result)
    {
        const std::string batch_id = result.batch_id.empty() ? query_.batch_id : result.batch_id;
        if (batch_id.empty()) {
            return;
        }

        int success_count = 0;
        int failure_count = 0;
        std::string last_error;
        for (const auto &item : result.items) {
            const bool failed = !item.ok && !item.skipped;
            if (item.ok) {
                ++success_count;
            } else if (failed) {
                ++failure_count;
                if (last_error.empty()) {
                    last_error = item.message;
                }
            }
        }

        const std::uint64_t finished = nowSeconds();
        application::model::CrawlBatchRecord batch;
        batch.batch_id = batch_id;
        batch.cancel_requested = isCancellationRequested();
        batch.state = batch.cancel_requested ? "Cancelled" : (failure_count == 0 ? "Completed" : "Failed");
        batch.target_count = static_cast<int>(result.items.size());
        batch.processed_count = static_cast<int>(result.items.size());
        batch.success_count = success_count;
        batch.failure_count = failure_count;
        batch.last_error = last_error;
        batch.finished_at = finished;
        batch.updated_at = finished;
        const std::string payload = makeBatchFinishPayload(batch, elapsedMillisecondsSince(batch_started_at_));
        if (repository_) {
            (void)repository_->updateBatchProgress(batch);
            (void)repository_->appendEvent(application::model::CrawlBatchEventRecord {batch_id, std::nullopt, "batchFinished", batch.state, payload, finished});
        }
        logEvent(batch.state == "Failed" ? application::ports::DiagnosticsLevel::Warn : application::ports::DiagnosticsLevel::Info, "batchFinished", "Resource process finished", payload);
    }

    // 提交后台任务失败时，把已创建批次立即标记为 Failed。
    void failBatch(std::string message)
    {
        if (query_.batch_id.empty()) {
            return;
        }
        const std::uint64_t failed_at = nowSeconds();
        application::model::CrawlBatchRecord batch;
        batch.batch_id = query_.batch_id;
        batch.state = "Failed";
        batch.last_error = std::move(message);
        batch.finished_at = failed_at;
        batch.updated_at = failed_at;
        const std::string payload = makeBatchFinishPayload(batch, elapsedMillisecondsSince(batch_started_at_));
        if (repository_) {
            (void)repository_->updateBatchProgress(batch);
            (void)repository_->appendEvent(application::model::CrawlBatchEventRecord {query_.batch_id, std::nullopt, "batchFailed", batch.last_error, payload, failed_at});
        }
        logEvent(application::ports::DiagnosticsLevel::Error, "batchFailed", "Resource process failed", payload);
    }

private:
    // 根据事件名选择运行日志级别，保持成功路径信息化、失败路径醒目。
    static application::ports::DiagnosticsLevel levelForEvent(std::string_view event_type)
    {
        if (event_type == "batchFailed" || event_type.find("WritebackFailed") != std::string_view::npos || event_type.find("BlobSaveFailed") != std::string_view::npos) {
            return application::ports::DiagnosticsLevel::Error;
        }
        if (event_type.find("Failed") != std::string_view::npos || event_type.find("Rejected") != std::string_view::npos || event_type.find("Restricted") != std::string_view::npos || event_type.find("ValidationFailed") != std::string_view::npos) {
            return application::ports::DiagnosticsLevel::Warn;
        }
        if (event_type == "batchStarted" || event_type == "batchFinished" || event_type == "resourceClaimStarted" || event_type == "resourceClaimCompleted" || event_type == "resourceClaimEmpty" || event_type == "itemCompleted" || event_type == "itemFailed" || event_type == "resourceDbWritebackCompleted" || event_type == "resourceFailureWritebackCompleted" || event_type == "resourceSkipWritebackCompleted" || event_type == "pacmtokenRefreshed") {
            return application::ports::DiagnosticsLevel::Info;
        }
        return application::ports::DiagnosticsLevel::Debug;
    }

    // 把资源批次事件镜像到运行日志；具体 console/file/json 输出由 infrastructure sink 决定。
    void logEvent(application::ports::DiagnosticsLevel level, std::string_view event_type, std::string message, std::string_view payload) const
    {
        if (!diagnostics_) {
            return;
        }
        diagnostics_->event(level, "ResourceProcess", event_type, message, payload.empty() ? std::string_view("{}") : payload);
    }

    // 批次 item 槽位，保证开始和结束事件使用同一 item_index。
    struct ItemSlot {
        int item_index = 0;                                                                  // 批次内序号
        std::string item_key;                                                                // 批次 item_key
        std::chrono::steady_clock::time_point started_at = std::chrono::steady_clock::now(); // item 开始处理时间
    };

    // 追加批次统一 trace 字段，保证所有运行日志可按 batch_id 串起来。
    void appendBatchTraceFields(std::ostringstream &out, bool &first, std::string_view original) const
    {
        appendJsonStringFieldIfMissing(out, first, original, "trace", "batch:" + query_.batch_id);
        appendJsonStringFieldIfMissing(out, first, original, "batch_id", query_.batch_id);
        appendJsonStringFieldIfMissing(out, first, original, "source", query_.source.empty() ? "migu" : query_.source);
        appendJsonStringFieldIfMissing(out, first, original, "resource_kind", query_.resource_kind);
        appendJsonStringFieldIfMissing(out, first, original, "format_type", query_.format_type);
        appendJsonStringFieldIfMissing(out, first, original, "content_id", query_.source_content_id);
        appendJsonRawFieldIfMissing(out, first, original, "limit", std::to_string(query_.limit));
        appendJsonRawFieldIfMissing(out, first, original, "offset", std::to_string(query_.offset));
        appendJsonRawFieldIfMissing(out, first, original, "statuses", statusesJson(query_.statuses));
    }

    // 构造批次事件 payload，统一补齐 trace 字段。
    std::string makeBatchEventPayload(std::string_view payload) const
    {
        return extendJsonObject(payload, [this](std::ostringstream &out, bool &first, std::string_view original) {
            appendBatchTraceFields(out, first, original);
        });
    }

    // 构造批次结束 payload，补齐最终计数和总耗时。
    std::string makeBatchFinishPayload(const application::model::CrawlBatchRecord &batch, std::int64_t duration_ms) const
    {
        return extendJsonObject("{}", [this, &batch, duration_ms](std::ostringstream &out, bool &first, std::string_view original) {
            appendBatchTraceFields(out, first, original);
            appendJsonStringFieldIfMissing(out, first, original, "state", batch.state);
            appendJsonRawFieldIfMissing(out, first, original, "target_count", std::to_string(batch.target_count));
            appendJsonRawFieldIfMissing(out, first, original, "processed_count", std::to_string(batch.processed_count));
            appendJsonRawFieldIfMissing(out, first, original, "success_count", std::to_string(batch.success_count));
            appendJsonRawFieldIfMissing(out, first, original, "failure_count", std::to_string(batch.failure_count));
            appendJsonStringFieldIfMissing(out, first, original, "last_error", batch.last_error);
            appendJsonRawFieldIfMissing(out, first, original, "duration_ms", std::to_string(duration_ms));
        });
    }

    // 构造 item 事件 payload，统一补齐 item trace 字段和可选耗时。
    std::string makeItemEventPayload(const application::model::SongResourceProcessItem &item, const ItemSlot &slot, std::string_view payload, std::int64_t duration_ms = -1) const
    {
        return extendJsonObject(payload, [this, &item, &slot, duration_ms](std::ostringstream &out, bool &first, std::string_view original) {
            appendJsonStringFieldIfMissing(out, first, original, "trace", "batch:" + query_.batch_id + "/item:" + slot.item_key + "/index:" + std::to_string(slot.item_index));
            appendJsonStringFieldIfMissing(out, first, original, "batch_id", query_.batch_id);
            appendJsonRawFieldIfMissing(out, first, original, "item_index", std::to_string(slot.item_index));
            appendJsonStringFieldIfMissing(out, first, original, "item_key", slot.item_key);
            appendJsonRawFieldIfMissing(out, first, original, "resource_id", std::to_string(item.resource_id));
            appendJsonRawFieldIfMissing(out, first, original, "song_source_ref_id", std::to_string(item.song_source_ref_id));
            appendJsonStringFieldIfMissing(out, first, original, "content_id", item.source_content_id);
            appendJsonStringFieldIfMissing(out, first, original, "source_content_id", item.source_content_id);
            appendJsonStringFieldIfMissing(out, first, original, "resource_kind", item.resource_kind);
            appendJsonStringFieldIfMissing(out, first, original, "format_type", item.format_type);
            appendJsonRawFieldIfMissing(out, first, original, "related", item.related ? "true" : "false");
            appendJsonRawFieldIfMissing(out, first, original, "synthetic", item.synthetic ? "true" : "false");
            appendJsonRawFieldIfMissing(out, first, original, "skipped", item.skipped ? "true" : "false");
            if (duration_ms >= 0) {
                appendJsonRawFieldIfMissing(out, first, original, "duration_ms", std::to_string(duration_ms));
            }
        });
    }

    // 返回资源项的稳定逻辑键；只用于 recorder 内部去重，不写入外部响应。
    static std::string logicalItemKey(const application::model::SongResourceProcessItem &item)
    {
        if (item.resource_id != 0) {
            return "resource:" + std::to_string(item.resource_id);
        }
        return "related:" + item.resource_kind + ":" + std::to_string(item.song_source_ref_id) + ":" + item.source_song_id;
    }

    // 分配或读取资源项槽位。
    ItemSlot ensureItemSlot(const application::model::SongResourceProcessItem &item)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        const std::string logical_key = logicalItemKey(item);
        auto it = item_slots_.find(logical_key);
        if (it != item_slots_.end()) {
            return it->second;
        }

        ItemSlot slot;
        slot.item_index = next_item_index_++;
        slot.item_key = item.resource_id == 0 ? "related:" + item.resource_kind + ":" + std::to_string(slot.item_index) : logical_key;
        item_slots_[logical_key] = slot;
        return slot;
    }

    // 根据处理明细构造批次 item 记录，不记录真实临时下载 URL。
    application::model::CrawlBatchItemRecord makeItemRecord(const application::model::SongResourceProcessItem &item, const ItemSlot &slot, std::string state, std::uint64_t now) const
    {
        const bool failed = !item.ok && !item.skipped;
        application::model::CrawlBatchItemRecord record;
        record.batch_id = query_.batch_id;
        record.item_index = slot.item_index;
        record.item_key = slot.item_key;
        record.item_type = "songResource";
        record.item_label = item.title;
        record.source = item.source.empty() ? "migu" : item.source;
        record.source_song_id = item.source_song_id;
        record.state = std::move(state);
        record.written_count = item.ok ? 1 : 0;
        record.error_category = item.ok ? "" : (item.error_category.empty() ? "unknown" : item.error_category);
        record.result_code = item.result_code.empty() ? (failed ? "failed" : "ok") : item.result_code;
        record.started_at = now;
        record.finished_at = record.state == "Running" ? 0 : now;
        record.last_error = failed ? item.message : "";
        record.payload = makeItemEventPayload(item, slot, resourceProcessItemPayload(item));
        record.created_at = now;
        record.updated_at = now;
        return record;
    }

    // 更新已完成计数，只有真正保存成功的资源计入 success_count。
    void updateCompletedCounters(bool failed, const std::string &message, bool ok = false)
    {
        std::lock_guard<std::mutex> lock(mutex_);
        ++processed_count_;
        if (ok) {
            ++success_count_;
        } else if (failed) {
            ++failure_count_;
            if (last_error_.empty()) {
                last_error_ = message;
            }
        }
    }

    // 写入 Running 状态下的批次进度。
    void updateRunningBatchProgress(std::uint64_t now, const std::string &last_error)
    {
        if (!repository_) {
            return;
        }
        application::model::CrawlBatchRecord batch;
        {
            std::lock_guard<std::mutex> lock(mutex_);
            batch.target_count = next_item_index_;
            batch.processed_count = processed_count_;
            batch.success_count = success_count_;
            batch.failure_count = failure_count_;
            batch.last_error = last_error.empty() ? last_error_ : last_error;
        }
        batch.batch_id = query_.batch_id;
        batch.state = "Running";
        batch.updated_at = now;
        (void)repository_->updateBatchProgress(batch);
    }

    std::shared_ptr<application::ports::BatchTaskRepository> repository_;                       // 批次持久化 port
    application::model::SongResourceClaimQuery query_;                                          // 请求快照
    std::string settings_snapshot_;                                                             // 运行设置快照
    std::shared_ptr<application::ports::DiagnosticsSink> diagnostics_;                          // 运行日志诊断 port
    std::mutex mutex_;                                                                          // 保护实时进度计数
    int next_item_index_ = 0;                                                                   // 下一个动态 item 序号
    int processed_count_ = 0;                                                                   // 已完成 item 数
    int success_count_ = 0;                                                                     // 成功或跳过 item 数
    int failure_count_ = 0;                                                                     // 失败 item 数
    std::string last_error_;                                                                    // 最近失败说明
    std::unordered_map<std::string, ItemSlot> item_slots_;                                      // 逻辑键到 item 槽位映射
    std::chrono::steady_clock::time_point batch_started_at_ = std::chrono::steady_clock::now(); // 批次开始处理时间
};

// 运行异步资源批次；使用普通协程函数避免捕获型 coroutine lambda 的闭包悬垂。
application::async::Task<void> runAsyncResourceProcessTask(
    std::shared_ptr<AsyncSongResourceDownloadWorkflow> workflow,
    std::shared_ptr<application::ports::AsyncRuntime> runtime,
    std::shared_ptr<application::ports::BatchTaskRepository> batch_repository,
    std::shared_ptr<application::ports::DiagnosticsSink> diagnostics,
    application::model::SongResourceClaimQuery query,
    std::string settings_snapshot
)
{
    auto recorder = std::make_shared<ResourceProcessBatchRecorder>(batch_repository, query, settings_snapshot, diagnostics);
    std::string failure_message;
    try {
        auto result = co_await workflow->processPending(std::move(query), recorder.get());
        co_await runtime->postBlocking<void>([recorder, result = std::move(result)]() mutable {
            recorder->finish(result);
        });
        co_return;
    } catch (const std::exception &e) {
        failure_message = e.what();
    } catch (...) {
        failure_message = "Migu resource process failed with unknown error";
    }
    co_await runtime->postBlocking<void>([recorder, failure_message]() mutable {
        recorder->failBatch(failure_message);
    });
}

// 平台总数已知时，把受 limit 截断的成功结果暴露为部分完成。
void applySingerDeclaredTotalCompletion(application::model::SingerCollectionResult &result, std::optional<int> declared_total, std::string_view item_name)
{
    if (!result.success || !declared_total || *declared_total <= result.written_count) {
        return;
    }

    result.success = false;
    result.partial = result.written_count > 0;
    result.error = std::string(item_name) + " incomplete: fetched " + std::to_string(result.written_count) + " of " + std::to_string(*declared_total);
}

// 判断当前错误是否只是声明总数不足导致的可重算状态。
bool isDeclaredTotalIncompleteError(const std::string &error)
{
    return error.find(" incomplete: fetched ") != std::string::npos;
}

// 用数据库真实关系数量重算歌手歌曲/专辑完成度，避免按本轮写入数量误判。
void applyRealSingerCountCompletion(application::model::SingerCollectionResult &result, const application::model::MiguSingerCatalogRecord &singer, bool songs_type)
{
    const auto declared_total = songs_type ? singer.source_song_total : singer.source_album_total;
    const int real_count = songs_type ? singer.songs_sync_count : singer.albums_sync_count;
    const std::string_view item_name = songs_type ? "singer songs" : "singer albums";
    result.source_song_total = singer.source_song_total;
    result.source_album_total = singer.source_album_total;
    result.written_count = real_count;

    const bool can_recalculate = result.success || (result.partial && isDeclaredTotalIncompleteError(result.error));
    if (!can_recalculate) {
        return;
    }
    if (!declared_total) {
        result.success = true;
        result.partial = false;
        result.error.clear();
        return;
    }
    if (real_count >= *declared_total) {
        result.success = true;
        result.partial = false;
        result.error.clear();
        return;
    }

    result.success = false;
    result.partial = real_count > 0;
    result.error = std::string(item_name) + " incomplete: fetched " + std::to_string(real_count) + " of " + std::to_string(*declared_total);
}

// 根据采集结果和已知平台总数计算同步状态；总数未知时不把成功采集推断为全量完成。
int syncStatusFromResult(const application::model::SingerCollectionResult &result, std::optional<int> declared_total)
{
    if (result.partial) {
        return 4;
    }
    if (!result.success) {
        return 3;
    }
    return declared_total ? 2 : 4;
}

// 根据采集结果转换批次目标状态；部分完成作为失败 item 暴露给前端治理。
BatchTaskManager::TargetResult batchResultFromSingerCollection(const application::model::SingerCollectionResult &result, bool sync_status_updated)
{
    if (!sync_status_updated) {
        return {false, result.written_count, "failed to update singer sync status", "persistence", "persist_failed"};
    }
    if (result.partial) {
        const std::string error = result.error.empty() ? "target partially completed" : result.error;
        auto classification = classifyFailure(error);
        return {false, result.written_count, error, std::move(classification.category), "partial_completed"};
    }
    if (result.success) {
        return {true, result.written_count, {}, {}, "ok"};
    }
    auto classification = classifyFailure(result.error);
    return {false, result.written_count, result.error, std::move(classification.category), std::move(classification.code)};
}

// 补池默认处理未同步、失败和部分完成目标。
std::vector<int> defaultBackfillStatuses(std::vector<int> statuses)
{
    return statuses.empty() ? std::vector<int> {0, 3, 4} : std::move(statuses);
}

// 判断补池目标状态是否允许被 retry 认领。
bool isClaimableSyncStatus(int status)
{
    return status == 0 || status == 3 || status == 4;
}

// 判断本次请求是否选择了目标当前状态。
bool containsStatus(const std::vector<int> &statuses, int status)
{
    return std::find(statuses.begin(), statuses.end(), status) != statuses.end();
}

// 生成人可读且稳定的显式 ID 跳过原因。
std::string skippedReasonForStatus(int status, const std::vector<int> &statuses)
{
    if (!containsStatus(statuses, status)) {
        return "status_not_selected";
    }
    if (status == 1) {
        return "running";
    }
    if (status == 2) {
        return "completed";
    }
    if (!isClaimableSyncStatus(status)) {
        return "status_not_claimable";
    }
    return "claim_conflict";
}

// 将目录专辑记录转换为 workflow 所需来源参数。
application::model::AlbumSourceMetadata albumSourceFromRecord(const application::model::MiguAlbumCatalogRecord &record)
{
    application::model::AlbumSourceMetadata album;
    album.source = record.source;
    album.source_album_id = record.source_album_id;
    album.source_resource_type = record.source_resource_type;
    album.source_res_type = record.source_res_type;
    album.source_album_kind = record.source_album_kind;
    album.title = record.title;
    album.normalized_title = record.normalized_title;
    album.source_artist_id = record.source_artist_id;
    album.source_artist_name = record.source_artist_name;
    album.artist_name = record.source_artist_name.empty() ? record.primary_artist_name : record.source_artist_name;
    album.cover_url = record.cover_url;
    album.total_count = record.total_count;
    album.detail_sync_status = record.detail_sync_status;
    return album;
}

// 从批次 item 中提取前端治理目标 ID。
std::string sourceIdFromItem(const application::model::CrawlBatchItemRecord &item)
{
    if (!item.source_singer_id.empty()) {
        return item.source_singer_id;
    }
    if (!item.source_album_id.empty()) {
        return item.source_album_id;
    }
    if (!item.source_song_id.empty()) {
        return item.source_song_id;
    }
    return item.item_key;
}

// 判断批次 item 类型是否属于当前元数据补池治理范围。
bool isMetadataBatchItemType(const std::string &item_type)
{
    return item_type == "singerSongs" || item_type == "singerAlbums" || item_type == "albumDetail" || item_type == "albumSongs";
}

// 将 item_type 映射到补池类型。
std::string backfillTypeFromItemType(const std::string &item_type)
{
    if (item_type == "singerSongs") {
        return "singer-songs";
    }
    if (item_type == "singerAlbums") {
        return "singer-albums";
    }
    if (item_type == "albumDetail") {
        return "album-details";
    }
    if (item_type == "albumSongs") {
        return "album-songs";
    }
    return {};
}

// 批次二次治理默认只处理失败、取消或超时失败的 item。
bool matchesGovernanceItemFilter(const application::model::CrawlBatchItemRecord &item, const application::model::CrawlBatchGovernanceRequest &request, bool failed_only)
{
    if (!request.states.empty() && std::find(request.states.begin(), request.states.end(), item.state) == request.states.end()) {
        return false;
    }
    if (request.states.empty() && failed_only && item.state != "Failed" && item.state != "Cancelled") {
        return false;
    }
    if (!request.error_category.empty() && item.error_category != request.error_category) {
        return false;
    }
    if (!request.result_code.empty() && item.result_code != request.result_code) {
        return false;
    }
    return true;
}

// 构造批次治理响应明细。
application::model::CrawlBatchGovernanceItem governanceItemFromBatchItem(const application::model::CrawlBatchItemRecord &item, std::string reason)
{
    application::model::CrawlBatchGovernanceItem output;
    output.source_id = sourceIdFromItem(item);
    output.name = item.item_label.empty() ? item.item_key : item.item_label;
    output.status = item.state;
    output.reason = std::move(reason);
    return output;
}

// 构造治理审计事件，payload 保持最小脱敏上下文。
application::model::MiguGovernanceEventRecord governanceEvent(std::string action, std::string target_type, std::string target_id, std::string batch_id, std::string before_status, std::string after_status, std::string result_code, std::string error_category, std::string reason)
{
    application::model::MiguGovernanceEventRecord event;
    event.source = "migu";
    event.action = std::move(action);
    event.target_type = std::move(target_type);
    event.target_id = std::move(target_id);
    event.batch_id = std::move(batch_id);
    event.before_status = std::move(before_status);
    event.after_status = std::move(after_status);
    event.result_code = std::move(result_code);
    event.error_category = std::move(error_category);
    event.reason = std::move(reason);
    event.payload = "{}";
    event.created_at = nowSeconds();
    return event;
}

// 连接非空展示片段，保持前端次级说明紧凑可读。
std::string joinDisplayParts(const std::vector<std::string> &parts, std::string_view separator = " · ")
{
    std::ostringstream out;
    bool first = true;
    for (const auto &part : parts) {
        if (part.empty()) {
            continue;
        }
        if (!first) {
            out << separator;
        }
        first = false;
        out << part;
    }
    return out.str();
}

// 将资源类型转换为控制台展示名。
std::string resourceKindDisplayName(const std::string &kind)
{
    if (kind == "audio") {
        return "音频";
    }
    if (kind == "cover") {
        return "封面";
    }
    if (kind == "lyric" || kind == "lrc") {
        return "歌词";
    }
    if (kind == "mrc") {
        return "MRC";
    }
    if (kind == "trc") {
        return "TRC";
    }
    return kind;
}

// 将批次类型转换为治理审计展示名。
std::string batchTaskDisplayName(const std::string &task_type)
{
    if (task_type == "resourceProcess") {
        return "资源下载批次";
    }
    if (task_type == "singerSongs" || task_type == "singerSongsFromDb" || task_type == "singerSongsRetry") {
        return "歌手歌曲批次";
    }
    if (task_type == "singerAlbums" || task_type == "singerAlbumsFromDb" || task_type == "singerAlbumsRetry") {
        return "歌手专辑批次";
    }
    if (task_type == "albumDetails" || task_type == "albumDetail") {
        return "专辑详情批次";
    }
    if (task_type == "albumSongs") {
        return "专辑歌曲批次";
    }
    if (task_type == "singerProfileRefresh") {
        return "歌手资料批次";
    }
    return task_type.empty() ? "批次" : task_type;
}

// 将补池类型转换为治理审计展示摘要。
std::string backfillTypeDisplayName(const std::string &backfill_type)
{
    if (backfill_type == "singer-songs") {
        return "歌手歌曲补池";
    }
    if (backfill_type == "singer-albums") {
        return "歌手专辑补池";
    }
    if (backfill_type == "album-details") {
        return "专辑详情补池";
    }
    if (backfill_type == "album-songs") {
        return "专辑歌曲补池";
    }
    return backfill_type;
}

// 为资源治理事件补充可展示目标快照。
void applyResourceSnapshot(application::model::MiguGovernanceEventRecord &event, const application::model::MiguResourceQueueRecord &resource)
{
    event.target_name = resource.title;
    event.target_summary = joinDisplayParts({resourceKindDisplayName(resource.resource_kind), resource.format_type, resource.artist});
    event.target_source_id = resource.source_content_id.empty() ? resource.source_song_id : resource.source_content_id;
    event.target_owner_name = resource.artist;
}

// 为批次治理事件补充可展示目标快照。
void applyBatchSnapshot(application::model::MiguGovernanceEventRecord &event, const application::model::CrawlBatchRecord &batch)
{
    const auto display_name = batchTaskDisplayName(batch.task_type);
    event.target_name = batch.display_name.empty() ? display_name : batch.display_name;
    event.target_summary = batch.target_summary.empty() ? joinDisplayParts({display_name, batch.state, batch.target_count > 0 ? std::to_string(batch.target_count) + " 条" : std::string {}}) : batch.target_summary;
}

// 构造资源 retry/reset 审计事件，成功时去掉与 action 重复的 reason。
application::model::MiguGovernanceEventRecord resourceGovernanceEvent(std::string action, const application::model::MiguResourceActionItem &item, const std::optional<application::model::MiguResourceQueueRecord> &resource, std::string success_reason)
{
    const bool success = item.reason == success_reason;
    auto event = governanceEvent(std::move(action), "resource", std::to_string(item.resource_id), "", std::to_string(item.status), success ? "0" : std::to_string(item.status), success ? "ok" : "failed", success ? "" : "invalid_target", success ? "" : item.reason);
    if (resource) {
        applyResourceSnapshot(event, *resource);
    } else {
        event.target_name = item.name;
        event.target_source_id = item.source_id;
    }
    return event;
}

// 构造补池目标治理事件。
application::model::MiguGovernanceEventRecord backfillGovernanceEvent(const std::string &backfill_type, const application::model::MiguBackfillActionItem &item, const std::string &batch_id)
{
    const bool success = item.reason == "claimed" && !batch_id.empty();
    const std::string reason = success ? "" : (item.reason == "claimed" ? "batch_not_started" : item.reason);
    const std::string target_type = (backfill_type == "singer-songs" || backfill_type == "singer-albums") ? "singer" : "album";
    auto event = governanceEvent("backfill." + backfill_type + ".retry", target_type, item.source_id, batch_id, std::to_string(item.status), success ? "1" : std::to_string(item.status), success ? "ok" : "failed", success ? "" : "invalid_target", reason);
    event.target_name = item.name;
    event.target_summary = backfillTypeDisplayName(backfill_type);
    event.target_source_id = item.source_id;
    return event;
}

} // namespace

CrawlerService::CrawlerService(CrawlerServiceDependencies dependencies) : runtime_settings_(std::move(dependencies.options.runtime_settings)),
                                                                          diagnostics_(std::move(dependencies.diagnostics))
{
    if (!diagnostics_) {
        diagnostics_ = std::make_shared<application::ports::NullDiagnosticsSink>();
    }

    runtime_settings_.max_batch_concurrency = (std::max)(1, dependencies.options.max_batch_concurrency <= 0 ? kDefaultBatchConcurrency : dependencies.options.max_batch_concurrency);
    runtime_settings_.max_page_concurrency = (std::max)(1, dependencies.options.max_page_concurrency <= 0 ? kDefaultPageConcurrency : dependencies.options.max_page_concurrency);
    runtime_settings_.max_retries = (std::max)(0, dependencies.options.max_retries < 0 ? kDefaultRetries : dependencies.options.max_retries);
    runtime_settings_.task_timeout_ms = static_cast<int>((dependencies.options.task_timeout.count() <= 0 ? std::chrono::minutes(10) : dependencies.options.task_timeout).count());
    runtime_settings_.resource_download_concurrency = dependencies.options.runtime_settings.resource_download_concurrency <= 0 ? kDefaultResourceDownloadConcurrency : dependencies.options.runtime_settings.resource_download_concurrency;
    runtime_settings_.resource_download_retries = dependencies.options.runtime_settings.resource_download_retries < 0 ? kDefaultResourceDownloadRetries : dependencies.options.runtime_settings.resource_download_retries;
    runtime_settings_.resource_download_timeout_ms = dependencies.options.runtime_settings.resource_download_timeout_ms <= 0 ? kDefaultResourceDownloadTimeoutMs : dependencies.options.runtime_settings.resource_download_timeout_ms;
    runtime_settings_.pacmtoken_refresh_interval_seconds = dependencies.options.runtime_settings.pacmtoken_refresh_interval_seconds < 0 ? kDefaultPacmtokenRefreshIntervalSeconds : dependencies.options.runtime_settings.pacmtoken_refresh_interval_seconds;
    runtime_settings_.auto_refresh_pacmtoken = dependencies.options.runtime_settings.auto_refresh_pacmtoken;
    runtime_settings_.refresh_pacmtoken_on_440013 = dependencies.options.runtime_settings.refresh_pacmtoken_on_440013;
    {
        auto normalized_resource_options = normalizeResourceProcessOptions({
            static_cast<std::size_t>(runtime_settings_.resource_download_concurrency),
            runtime_settings_.resource_download_retries,
            runtime_settings_.resource_download_timeout_ms,
            runtime_settings_.pacmtoken_refresh_interval_seconds,
            runtime_settings_.auto_refresh_pacmtoken,
            runtime_settings_.refresh_pacmtoken_on_440013,
        });
        runtime_settings_.resource_download_concurrency = static_cast<int>(normalized_resource_options.resource_download_concurrency);
        runtime_settings_.resource_download_retries = normalized_resource_options.resource_download_retries;
        runtime_settings_.resource_download_timeout_ms = normalized_resource_options.resource_download_timeout_ms;
        runtime_settings_.pacmtoken_refresh_interval_seconds = normalized_resource_options.pacmtoken_refresh_interval_seconds;
    }

    auto song_resource_queue = dependencies.persistence.song_resource_queue;
    auto song_resource_provider = dependencies.providers.song_resource_provider;
    auto song_resource_parser = dependencies.parsers.song_resource_parser;
    auto song_resource_content_fetcher = dependencies.providers.song_resource_content_fetcher;
    auto song_resource_blob_store = dependencies.persistence.song_resource_blob_store;
    auto song_resource_cloud_uploader = dependencies.persistence.song_resource_cloud_uploader;
    auto song_resource_batch_cloud_uploader = dependencies.persistence.song_resource_batch_cloud_uploader;
    auto async_song_resource_queue = dependencies.persistence.async_song_resource_queue;
    auto async_song_resource_provider = dependencies.providers.async_song_resource_provider;
    auto async_song_resource_content_fetcher = dependencies.providers.async_song_resource_content_fetcher;
    auto async_song_resource_blob_store = dependencies.persistence.async_song_resource_blob_store;
    auto async_song_resource_cloud_uploader = dependencies.persistence.async_song_resource_cloud_uploader;
    auto async_song_resource_batch_cloud_uploader = dependencies.persistence.async_song_resource_batch_cloud_uploader;
    auto async_song_resource_remote_copier = dependencies.persistence.async_song_resource_remote_copier;
    auto migu_pacmtoken_refresher = dependencies.providers.migu_pacmtoken_refresher;
    auto migu_pacmtoken_store = dependencies.providers.migu_pacmtoken_store;
    async_runtime_ = dependencies.executors.async_runtime;

    if (song_resource_queue && song_resource_provider && song_resource_parser && song_resource_content_fetcher && song_resource_blob_store) {
        song_resource_workflow_ = std::make_shared<SongResourceDownloadWorkflow>(
            song_resource_queue,
            song_resource_provider,
            song_resource_parser,
            song_resource_content_fetcher,
            song_resource_blob_store,
            SongResourcePathPlanner {},
            std::move(dependencies.executors.resource_executor),
            migu_pacmtoken_refresher,
            migu_pacmtoken_store,
            song_resource_cloud_uploader,
            dependencies.resource_processing
        );
    }

    if (async_runtime_) {
        if (!async_song_resource_queue && song_resource_queue) {
            async_song_resource_queue = std::make_shared<BlockingAsyncSongResourceQueue>(song_resource_queue, async_runtime_);
        }
        if (!async_song_resource_provider && song_resource_provider) {
            async_song_resource_provider = std::make_shared<BlockingAsyncSongResourceProvider>(song_resource_provider, async_runtime_);
        }
        if (!async_song_resource_content_fetcher && song_resource_content_fetcher) {
            async_song_resource_content_fetcher = std::make_shared<BlockingAsyncSongResourceContentFetcher>(song_resource_content_fetcher, async_runtime_);
        }
        if (!async_song_resource_blob_store && song_resource_blob_store) {
            async_song_resource_blob_store = std::make_shared<BlockingAsyncSongResourceBlobStore>(song_resource_blob_store, async_runtime_);
        }
        if (!async_song_resource_cloud_uploader && song_resource_cloud_uploader) {
            async_song_resource_cloud_uploader = std::make_shared<BlockingAsyncSongResourceCloudUploader>(song_resource_cloud_uploader, async_runtime_);
        }
        if (!async_song_resource_batch_cloud_uploader && song_resource_batch_cloud_uploader) {
            async_song_resource_batch_cloud_uploader = std::make_shared<BlockingAsyncSongResourceBatchCloudUploader>(song_resource_batch_cloud_uploader, async_runtime_);
        }
        if (async_song_resource_queue && async_song_resource_provider && song_resource_parser && async_song_resource_content_fetcher && async_song_resource_blob_store) {
            async_song_resource_workflow_ = std::make_shared<AsyncSongResourceDownloadWorkflow>(
                async_runtime_,
                async_song_resource_queue,
                async_song_resource_provider,
                song_resource_parser,
                async_song_resource_content_fetcher,
                async_song_resource_blob_store,
                SongResourcePathPlanner {},
                migu_pacmtoken_refresher,
                migu_pacmtoken_store,
                async_song_resource_remote_copier,
                async_song_resource_cloud_uploader,
                async_song_resource_batch_cloud_uploader,
                dependencies.resource_processing
            );
        }
    }

    persistence_ = std::make_shared<MetadataPersistenceService>(
        std::move(dependencies.persistence.file_store),
        std::move(dependencies.persistence.song_repository),
        std::move(dependencies.persistence.album_repository),
        std::move(dependencies.persistence.singer_repository),
        std::move(dependencies.persistence.song_source_repository),
        std::move(dependencies.persistence.singer_source_repository),
        std::move(dependencies.persistence.album_source_writer),
        std::move(dependencies.executors.persistence_executor),
        MetadataIngestionDependencies {dependencies.persistence.metadata_batch_writer, diagnostics_}
    );

    workflow_ = assembleCollectionWorkflows(dependencies, persistence_, diagnostics_);

    batch_task_repository_ = dependencies.persistence.batch_task_repository;
    resource_process_runner_ = TaskRunner(dependencies.executors.batch_executor, diagnostics_);
    batch_task_manager_ = std::make_unique<BatchTaskManager>(
        std::move(dependencies.executors.batch_executor),
        diagnostics_,
        batch_task_repository_,
        [this]() {
            return maxBatchConcurrency();
        },
        [this]() {
            return taskTimeout();
        }
    );
    singer_source_sync_queue_ = std::move(dependencies.persistence.singer_source_sync_queue);
    batch_query_service_ = std::make_unique<CrawlerBatchQueryService>(dependencies.persistence.batch_task_repository);
    catalog_query_service_ = std::make_unique<MusicCatalogQueryService>(dependencies.persistence.catalog_query_repository);
}

bool CrawlerService::processUrl(std::string url, int limit)
{
    return workflow_ ? workflow_->processUrl(std::move(url), limit) : false;
}

bool CrawlerService::searchAndSave(std::string keyword, int limit)
{
    if (keyword.empty() || !workflow_) {
        return false;
    }

    const auto target = target_dispatcher_.dispatch(keyword);
    const int normalized_limit = normalizeLimitForTarget(limit, target.kind);
    switch (target.kind) {
    case CrawlTargetKind::Url:
        return workflow_->processUrl(target.text, normalized_limit);
    case CrawlTargetKind::SingerSongs:
        return crawlSingerSongsAndSave(target.singer_id, normalized_limit);
    case CrawlTargetKind::SingerIndex:
        return workflow_->crawlSingerIndexAndSave(target.text, normalized_limit);
    case CrawlTargetKind::SingerAlbums:
        return crawlSingerAlbumsAndSave(target.singer_id, normalized_limit);
    case CrawlTargetKind::AlbumDetails:
        return workflow_->crawlPendingAlbumDetailsAndSave(target.text, normalized_limit, maxPageConcurrency());
    case CrawlTargetKind::AlbumSongs:
        return workflow_->crawlPendingAlbumSongsAndSave(target.text, normalized_limit, maxPageConcurrency());
    case CrawlTargetKind::Keyword:
        return workflow_->searchAndSave(target.text, normalized_limit, maxPageConcurrency());
    }
    return false;
}

bool CrawlerService::crawlSingerSongsAndSave(std::uint64_t singer_id, int limit)
{
    const auto result = crawlSingerSongsWithStoredTotals(singer_id, limit);
    return result.written_count > 0 && (result.success || result.partial);
}

bool CrawlerService::crawlSingerIndexAndSave(std::string tab, int limit)
{
    return workflow_ ? workflow_->crawlSingerIndexAndSave(std::move(tab), limit) : false;
}

bool CrawlerService::crawlSingerAlbumsAndSave(std::uint64_t singer_id, int limit)
{
    const auto result = crawlSingerAlbumsWithStoredTotals(singer_id, limit);
    return result.written_count > 0 && (result.success || result.partial);
}

bool CrawlerService::crawlPendingAlbumDetailsAndSave(std::string source, int limit)
{
    return workflow_ ? workflow_->crawlPendingAlbumDetailsAndSave(std::move(source), limit, maxPageConcurrency()) : false;
}

bool CrawlerService::crawlPendingAlbumSongsAndSave(std::string source, int limit)
{
    return workflow_ ? workflow_->crawlPendingAlbumSongsAndSave(std::move(source), limit, maxPageConcurrency()) : false;
}

bool CrawlerService::searchAndSaveWithRetry(std::string keyword, int limit)
{
    const int attempts = maxRetries() + 1;
    for (int attempt = 0; attempt < attempts; ++attempt) {
        try {
            const bool success = searchAndSave(keyword, limit);
            if (success || attempt + 1 >= attempts) {
                return success;
            }
        } catch (const std::exception &e) {
            diagnostics_->error("CrawlerService", std::string("searchAndSave attempt failed: ") + e.what());
            if (attempt + 1 >= attempts) {
                return false;
            }
        } catch (...) {
            diagnostics_->error("CrawlerService", "searchAndSave attempt failed with unknown error");
            if (attempt + 1 >= attempts) {
                return false;
            }
        }
    }
    return false;
}

application::model::SingerCollectionResult CrawlerService::crawlSingerSongsWithStoredTotals(std::uint64_t singer_id, int limit)
{
    if (!workflow_ || singer_id == 0) {
        return {false, false, 0, "singer song workflow is not configured"};
    }

    std::optional<int> source_song_total;
    std::optional<int> source_album_total;
    const std::string source_singer_id = std::to_string(singer_id);
    if (catalog_query_service_) {
        if (auto singer = catalog_query_service_->findSinger(source_singer_id)) {
            source_song_total = singer->source_song_total;
            source_album_total = singer->source_album_total;
        }
    }

    auto result = workflow_->crawlSingerSongsAndSaveResult(singer_id, limit, maxPageConcurrency(), source_song_total, source_album_total);
    if (catalog_query_service_) {
        if (auto singer = catalog_query_service_->findSinger(source_singer_id)) {
            applyRealSingerCountCompletion(result, *singer, true);
        }
    }
    return result;
}

application::model::SingerCollectionResult CrawlerService::crawlSingerAlbumsWithStoredTotals(std::uint64_t singer_id, int limit)
{
    if (!workflow_ || singer_id == 0) {
        return {false, false, 0, "singer album workflow is not configured"};
    }

    std::optional<int> source_song_total;
    std::optional<int> source_album_total;
    const std::string source_singer_id = std::to_string(singer_id);
    if (catalog_query_service_) {
        if (auto singer = catalog_query_service_->findSinger(source_singer_id)) {
            source_song_total = singer->source_song_total;
            source_album_total = singer->source_album_total;
        }
    }

    auto result = workflow_->crawlSingerAlbumsAndSaveResult(singer_id, limit, maxPageConcurrency(), source_song_total, source_album_total);
    if (catalog_query_service_) {
        if (auto singer = catalog_query_service_->findSinger(source_singer_id)) {
            applyRealSingerCountCompletion(result, *singer, false);
        }
    }
    return result;
}

bool CrawlerService::processUrlSync(std::string_view url, int limit)
{
    return processUrl(std::string(url), limit);
}

bool CrawlerService::searchAndSaveSync(std::string_view keyword, int limit)
{
    return searchAndSave(std::string(keyword), limit);
}

bool CrawlerService::crawlSingerSongsAndSaveSync(std::uint64_t singer_id, int limit)
{
    return crawlSingerSongsAndSave(singer_id, limit);
}

bool CrawlerService::crawlSingerIndexAndSaveSync(std::string_view tab, int limit)
{
    return crawlSingerIndexAndSave(std::string(tab), limit);
}

bool CrawlerService::crawlSingerAlbumsAndSaveSync(std::uint64_t singer_id, int limit)
{
    return crawlSingerAlbumsAndSave(singer_id, limit);
}

bool CrawlerService::crawlPendingAlbumDetailsAndSaveSync(std::string_view source, int limit)
{
    return crawlPendingAlbumDetailsAndSave(std::string(source), limit);
}

bool CrawlerService::crawlPendingAlbumSongsAndSaveSync(std::string_view source, int limit)
{
    return crawlPendingAlbumSongsAndSave(std::string(source), limit);
}

std::string CrawlerService::startBatchAsync(const std::vector<std::string> &keywords, int limit)
{
    return startBatchAsync(keywords, limit, "generic", "{}");
}

std::string CrawlerService::startBatchAsync(const std::vector<std::string> &targets, int limit, std::string task_type, std::string request_params)
{
    if (!batch_task_manager_) {
        return {};
    }

    std::string normalized_type = task_type.empty() ? "generic" : std::move(task_type);
    const int normalized_limit = normalizeLimitForTask(limit, normalized_type);
    auto items = makeTargetItems(targets, normalized_type);
    const std::string handler_type = normalized_type;
    return startBatchItemsAsync(
        std::move(items),
        normalized_limit,
        std::move(normalized_type),
        std::move(request_params),
        [this, handler_type](std::string target, int item_limit) {
            if (handler_type == "singerIndex") {
                const auto dispatched = target_dispatcher_.dispatch(target);
                if (dispatched.kind != CrawlTargetKind::SingerIndex || !workflow_) {
                    return BatchTaskManager::TargetResult {false, 0, "invalid singer index target", "invalid_target", "invalid_target"};
                }
                for (int attempt = 0; attempt <= maxRetries(); ++attempt) {
                    try {
                        const auto result = workflow_->crawlSingerIndexAndSaveResult(dispatched.text, item_limit);
                        if (result.success || attempt >= maxRetries()) {
                            return batchResultFromSingerCollection(result, true);
                        }
                    } catch (const std::exception &e) {
                        if (attempt >= maxRetries()) {
                            return failureTargetResult(e.what());
                        }
                    } catch (...) {
                        if (attempt >= maxRetries()) {
                            return failureTargetResult("unknown singer index crawl error");
                        }
                    }
                }
                return failureTargetResult("singer index crawl failed");
            }
            const auto dispatched = target_dispatcher_.dispatch(target);
            if (dispatched.kind == CrawlTargetKind::SingerSongs) {
                for (int attempt = 0; attempt <= maxRetries(); ++attempt) {
                    try {
                        const auto result = crawlSingerSongsWithStoredTotals(dispatched.singer_id, item_limit);
                        if (result.success || result.partial || attempt >= maxRetries()) {
                            return batchResultFromSingerCollection(result, true);
                        }
                    } catch (const std::exception &e) {
                        if (attempt >= maxRetries()) {
                            return failureTargetResult(e.what());
                        }
                    } catch (...) {
                        if (attempt >= maxRetries()) {
                            return failureTargetResult("unknown singer songs crawl error");
                        }
                    }
                }
                return failureTargetResult("singer songs crawl failed");
            }
            if (dispatched.kind == CrawlTargetKind::SingerAlbums) {
                for (int attempt = 0; attempt <= maxRetries(); ++attempt) {
                    try {
                        const auto result = crawlSingerAlbumsWithStoredTotals(dispatched.singer_id, item_limit);
                        if (result.success || result.partial || attempt >= maxRetries()) {
                            return batchResultFromSingerCollection(result, true);
                        }
                    } catch (const std::exception &e) {
                        if (attempt >= maxRetries()) {
                            return failureTargetResult(e.what());
                        }
                    } catch (...) {
                        if (attempt >= maxRetries()) {
                            return failureTargetResult("unknown singer albums crawl error");
                        }
                    }
                }
                return failureTargetResult("singer albums crawl failed");
            }
            return boolTargetResult(searchAndSaveWithRetry(std::move(target), item_limit));
        }
    );
}

std::string CrawlerService::startAlbumDetailsBackfillAsync(std::string source, int limit, std::string request_params)
{
    if (!batch_task_manager_ || !workflow_) {
        return {};
    }

    const int normalized_limit = normalizeLimit(limit);
    auto pending = workflow_->pendingAlbumDetails(source, normalized_limit);
    return startAlbumDetailsBackfillItemsAsync(std::move(pending), normalized_limit, std::move(request_params));
}

std::string CrawlerService::startAlbumDetailsBackfillItemsAsync(std::vector<application::model::AlbumSourceMetadata> pending, int limit, std::string request_params)
{
    if (!batch_task_manager_ || !workflow_) {
        return {};
    }

    const int normalized_limit = normalizeLimit(limit);
    std::unordered_map<std::string, application::model::AlbumSourceMetadata> by_key;
    std::vector<application::model::CrawlBatchItemRecord> items;
    items.reserve(pending.size());
    for (std::size_t index = 0; index < pending.size(); ++index) {
        const auto &album = pending[index];
        const std::string key = "album-detail:" + album.source + ":" + album.source_album_id;
        by_key[key] = album;
        application::model::CrawlBatchItemRecord item;
        item.item_index = static_cast<int>(index);
        item.item_key = key;
        item.item_type = "albumDetail";
        item.item_label = album.title.empty() ? album.source_album_id : album.title;
        item.source = album.source;
        item.source_album_id = album.source_album_id;
        item.payload = "{\"source_res_type\":\"" + album.source_res_type + "\"}";
        items.push_back(std::move(item));
    }

    return startBatchItemsAsync(
        std::move(items),
        normalized_limit,
        "albumDetails",
        std::move(request_params),
        [this, by_key = std::move(by_key)](std::string target, int) {
            auto it = by_key.find(target);
            if (it == by_key.end()) {
                return BatchTaskManager::TargetResult {false, 0, "album target not found", "invalid_target", "invalid_target"};
            }
            try {
                return boolTargetResult(workflow_->crawlAlbumDetailAndSave(it->second));
            } catch (const std::exception &e) {
                return failureTargetResult(e.what());
            } catch (...) {
                return failureTargetResult("unknown album detail crawl error");
            }
        },
        {},
        true
    );
}

std::string CrawlerService::startAlbumSongsBackfillAsync(std::string source, int limit, std::string request_params)
{
    if (!batch_task_manager_ || !workflow_) {
        return {};
    }

    const int normalized_limit = normalizeLimit(limit);
    auto pending = workflow_->pendingAlbumSongs(source, normalized_limit);
    return startAlbumSongsBackfillItemsAsync(std::move(pending), normalized_limit, std::move(request_params));
}

std::string CrawlerService::startAlbumSongsBackfillItemsAsync(std::vector<application::model::AlbumSourceMetadata> pending, int limit, std::string request_params)
{
    if (!batch_task_manager_ || !workflow_) {
        return {};
    }

    const int normalized_limit = normalizeLimit(limit);
    std::unordered_map<std::string, application::model::AlbumSourceMetadata> by_key;
    std::vector<application::model::CrawlBatchItemRecord> items;
    items.reserve(pending.size());
    for (std::size_t index = 0; index < pending.size(); ++index) {
        const auto &album = pending[index];
        const std::string key = "album-songs:" + album.source + ":" + album.source_album_id;
        by_key[key] = album;
        application::model::CrawlBatchItemRecord item;
        item.item_index = static_cast<int>(index);
        item.item_key = key;
        item.item_type = "albumSongs";
        item.item_label = album.title.empty() ? album.source_album_id : album.title;
        item.source = album.source;
        item.source_album_id = album.source_album_id;
        item.payload = "{\"source_res_type\":\"" + album.source_res_type + "\"}";
        items.push_back(std::move(item));
    }

    return startBatchItemsAsync(
        std::move(items),
        normalized_limit,
        "albumSongs",
        std::move(request_params),
        [this, by_key = std::move(by_key)](std::string target, int) {
            auto it = by_key.find(target);
            if (it == by_key.end()) {
                return BatchTaskManager::TargetResult {false, 0, "album target not found", "invalid_target", "invalid_target"};
            }
            try {
                return boolTargetResult(workflow_->crawlAlbumSongsAndSave(it->second));
            } catch (const std::exception &e) {
                return failureTargetResult(e.what());
            } catch (...) {
                return failureTargetResult("unknown album songs crawl error");
            }
        },
        {},
        true
    );
}

std::string CrawlerService::startSingerSongsFromDbAsync(application::model::SingerSourceSyncQuery query, int limit_per_singer, std::string request_params)
{
    if (!batch_task_manager_ || !workflow_ || !singer_source_sync_queue_) {
        return {};
    }

    query.limit = normalizeTargetLimit(query.limit);
    auto targets = singer_source_sync_queue_->claimPendingSongs(query);
    std::unordered_map<std::string, application::model::SingerSourceSyncTarget> by_key;
    std::vector<application::model::CrawlBatchItemRecord> items;
    items.reserve(targets.size());
    for (std::size_t index = 0; index < targets.size(); ++index) {
        const auto &target = targets[index];
        const std::string key = "singer:" + target.source_singer_id;
        by_key[key] = target;

        application::model::CrawlBatchItemRecord item;
        item.item_index = static_cast<int>(index);
        item.item_key = key;
        item.item_type = "singerSongs";
        item.item_label = target.name.empty() ? target.source_singer_id : target.name;
        item.source = target.source;
        item.source_singer_id = target.source_singer_id;
        item.payload = "{\"force\":" + std::string(query.force ? "true" : "false") + ",\"source_country\":\"" + escapeJsonString(target.source_country) + "\",\"gender\":" + std::to_string(target.gender)
                       + ",\"source_song_total\":" + (target.source_song_total ? std::to_string(*target.source_song_total) : "null")
                       + ",\"source_album_total\":" + (target.source_album_total ? std::to_string(*target.source_album_total) : "null") + "}";
        items.push_back(std::move(item));
    }

    auto queue = singer_source_sync_queue_;
    return startBatchItemsAsync(
        std::move(items),
        normalizeSingerIdsLimit(limit_per_singer),
        "singerSongsFromDb",
        std::move(request_params),
        [this, queue, force = query.force, by_key = std::move(by_key)](std::string target, int item_limit) {
            auto it = by_key.find(target);
            if (it == by_key.end()) {
                return BatchTaskManager::TargetResult {false, 0, "singer target not found", "invalid_target", "invalid_target"};
            }

            const auto &source = it->second;
            const auto singer_id = parseSingerId(source.source_singer_id);
            if (singer_id == 0) {
                (void)queue->updateSongsSyncStatus(source.source, source.source_singer_id, 3, 0, "invalid singer id");
                return BatchTaskManager::TargetResult {false, 0, "invalid singer id", "invalid_target", "invalid_target"};
            }

            application::model::SingerCollectionResult result;
            try {
                result = workflow_->crawlSingerSongsAndSaveResult(singer_id, item_limit, maxPageConcurrency(), source.source_song_total, source.source_album_total, force);
            } catch (const std::exception &e) {
                result = {false, false, 0, e.what()};
            } catch (...) {
                result = {false, false, 0, "unknown singer songs crawl error"};
            }

            if (!result.source_song_total && source.source_song_total) {
                result.source_song_total = source.source_song_total;
            }
            applySingerDeclaredTotalCompletion(result, result.source_song_total, "singer songs");
            if (catalog_query_service_) {
                if (auto singer = catalog_query_service_->findSinger(source.source_singer_id)) {
                    applyRealSingerCountCompletion(result, *singer, true);
                }
            }
            const bool updated = queue->updateSongsSyncStatus(source.source, source.source_singer_id, syncStatusFromResult(result, result.source_song_total), result.written_count, result.error);
            return batchResultFromSingerCollection(result, updated);
        }
    );
}

std::string CrawlerService::startSingerAlbumsFromDbAsync(application::model::SingerSourceSyncQuery query, int limit_per_singer, std::string request_params)
{
    if (!batch_task_manager_ || !workflow_ || !singer_source_sync_queue_) {
        return {};
    }

    query.limit = normalizeTargetLimit(query.limit);
    auto targets = singer_source_sync_queue_->claimPendingAlbums(query);
    std::unordered_map<std::string, application::model::SingerSourceSyncTarget> by_key;
    std::vector<application::model::CrawlBatchItemRecord> items;
    items.reserve(targets.size());
    for (std::size_t index = 0; index < targets.size(); ++index) {
        const auto &target = targets[index];
        const std::string key = "singer-albums:" + target.source_singer_id;
        by_key[key] = target;

        application::model::CrawlBatchItemRecord item;
        item.item_index = static_cast<int>(index);
        item.item_key = key;
        item.item_type = "singerAlbums";
        item.item_label = target.name.empty() ? target.source_singer_id : target.name;
        item.source = target.source;
        item.source_singer_id = target.source_singer_id;
        item.payload = "{\"force\":" + std::string(query.force ? "true" : "false") + ",\"source_country\":\"" + escapeJsonString(target.source_country) + "\",\"gender\":" + std::to_string(target.gender)
                       + ",\"source_song_total\":" + (target.source_song_total ? std::to_string(*target.source_song_total) : "null")
                       + ",\"source_album_total\":" + (target.source_album_total ? std::to_string(*target.source_album_total) : "null") + "}";
        items.push_back(std::move(item));
    }

    auto queue = singer_source_sync_queue_;
    return startBatchItemsAsync(
        std::move(items),
        normalizeSingerIdsLimit(limit_per_singer),
        "singerAlbumsFromDb",
        std::move(request_params),
        [this, queue, force = query.force, by_key = std::move(by_key)](std::string target, int item_limit) {
            auto it = by_key.find(target);
            if (it == by_key.end()) {
                return BatchTaskManager::TargetResult {false, 0, "singer target not found", "invalid_target", "invalid_target"};
            }

            const auto &source = it->second;
            const auto singer_id = parseSingerId(source.source_singer_id);
            if (singer_id == 0) {
                (void)queue->updateAlbumsSyncStatus(source.source, source.source_singer_id, 3, 0, "invalid singer id");
                return BatchTaskManager::TargetResult {false, 0, "invalid singer id", "invalid_target", "invalid_target"};
            }

            application::model::SingerCollectionResult result;
            try {
                result = workflow_->crawlSingerAlbumsAndSaveResult(singer_id, item_limit, maxPageConcurrency(), source.source_song_total, source.source_album_total, force);
            } catch (const std::exception &e) {
                result = {false, false, 0, e.what()};
            } catch (...) {
                result = {false, false, 0, "unknown singer albums crawl error"};
            }

            if (!result.source_album_total && source.source_album_total) {
                result.source_album_total = source.source_album_total;
            }
            applySingerDeclaredTotalCompletion(result, result.source_album_total, "singer albums");
            if (catalog_query_service_) {
                if (auto singer = catalog_query_service_->findSinger(source.source_singer_id)) {
                    applyRealSingerCountCompletion(result, *singer, false);
                }
            }
            const bool updated = queue->updateAlbumsSyncStatus(source.source, source.source_singer_id, syncStatusFromResult(result, result.source_album_total), result.written_count, result.error);
            return batchResultFromSingerCollection(result, updated);
        }
    );
}

std::optional<application::model::SingerSourceSyncPreviewResult> CrawlerService::previewSingerSongsFromDb(application::model::SingerSourceSyncQuery query) const
{
    if (!singer_source_sync_queue_) {
        return std::nullopt;
    }
    query.limit = normalizeTargetLimit(query.limit);
    return singer_source_sync_queue_->previewPendingSongs(std::move(query));
}

std::optional<application::model::SingerSourceSyncPreviewResult> CrawlerService::previewSingerAlbumsFromDb(application::model::SingerSourceSyncQuery query) const
{
    if (!singer_source_sync_queue_) {
        return std::nullopt;
    }
    query.limit = normalizeTargetLimit(query.limit);
    return singer_source_sync_queue_->previewPendingAlbums(std::move(query));
}

std::vector<application::model::CrawlBatchItemRecord> CrawlerService::makeTargetItems(const std::vector<std::string> &targets, std::string item_type) const
{
    std::vector<application::model::CrawlBatchItemRecord> items;
    items.reserve(targets.size());
    for (std::size_t index = 0; index < targets.size(); ++index) {
        application::model::CrawlBatchItemRecord item;
        item.item_index = static_cast<int>(index);
        item.item_key = targets[index];
        item.item_type = item_type;
        item.item_label = targets[index];
        item.source = "migu";
        const auto target = target_dispatcher_.dispatch(targets[index]);
        if (target.kind == CrawlTargetKind::SingerSongs || target.kind == CrawlTargetKind::SingerAlbums) {
            item.source_singer_id = std::to_string(target.singer_id);
        }
        items.push_back(std::move(item));
    }
    return items;
}

std::string CrawlerService::startBatchItemsAsync(
    std::vector<application::model::CrawlBatchItemRecord> items,
    int limit,
    std::string task_type,
    std::string request_params,
    BatchTaskManager::TargetHandler handler,
    std::string parent_batch_id,
    bool allow_empty
)
{
    if (!batch_task_manager_) {
        return {};
    }

    std::string effective_task_type = task_type.empty() && !items.empty() ? items.front().item_type : std::move(task_type);
    const int normalized_limit = normalizeLimitForTask(limit, effective_task_type);

    return batch_task_manager_->start(
        items,
        normalized_limit,
        std::move(effective_task_type),
        std::move(request_params),
        runtimeSettingsJson(),
        std::move(parent_batch_id),
        std::move(handler),
        allow_empty
    );
}

bool CrawlerService::cancelBatch(const std::string &batch_id)
{
    if (batch_task_manager_ && batch_task_manager_->cancel(batch_id)) return true;
    if (!batch_task_repository_) return false;
    auto batch = batch_task_repository_->findBatch(batch_id);
    if (!batch || batch->task_type != "resourceProcess" || (batch->state != "Running" && batch->state != "Pending")) return false;
    batch->cancel_requested = true;
    batch->state = "Cancelled";
    batch->updated_at = nowSeconds();
    batch->last_error = "Resource cancellation requested";
    return batch_task_repository_->updateBatchProgress(*batch);
}

std::optional<application::model::BatchProgressSnapshot> CrawlerService::getBatchProgress(const std::string &batch_id)
{
    return batch_task_manager_ ? batch_task_manager_->progress(batch_id) : std::nullopt;
}

application::model::CrawlBatchPage CrawlerService::listBatches(application::model::CrawlBatchQuery query) const
{
    return batch_query_service_ ? batch_query_service_->listBatches(std::move(query)) : application::model::CrawlBatchPage {};
}

std::optional<application::model::CrawlBatchRecord> CrawlerService::findBatch(std::string batch_id) const
{
    return batch_query_service_ ? batch_query_service_->findBatch(std::move(batch_id)) : std::nullopt;
}

application::model::CrawlBatchItemPage CrawlerService::listBatchItems(std::string batch_id, application::model::CrawlBatchItemQuery query) const
{
    return batch_query_service_ ? batch_query_service_->listItems(std::move(batch_id), std::move(query)) : application::model::CrawlBatchItemPage {};
}

application::model::CrawlBatchEventPage CrawlerService::listBatchEvents(std::string batch_id, application::model::CrawlBatchEventQuery query) const
{
    return batch_query_service_ ? batch_query_service_->listEvents(std::move(batch_id), std::move(query)) : application::model::CrawlBatchEventPage {};
}

application::model::CrawlBatchSummary CrawlerService::batchSummary(int stale_after_seconds) const
{
    return batch_query_service_ ? batch_query_service_->summary("migu", stale_after_seconds) : application::model::CrawlBatchSummary {};
}

application::model::CrawlBatchGovernanceResult CrawlerService::rerunFailedBatch(std::string batch_id, application::model::CrawlBatchGovernanceRequest request, std::string request_params)
{
    request.mode = "failed";
    return cloneBatch(std::move(batch_id), std::move(request), std::move(request_params));
}

application::model::CrawlBatchGovernanceResult CrawlerService::cloneBatch(std::string batch_id, application::model::CrawlBatchGovernanceRequest request, std::string request_params)
{
    application::model::CrawlBatchGovernanceResult result;
    if (!batch_query_service_ || !batch_task_manager_ || !workflow_) {
        return result;
    }
    const auto batch = batch_query_service_->findBatch(batch_id);
    if (!batch) {
        result.items.push_back({"", "", "", "batch_not_found"});
        return result;
    }

    request.limit = static_cast<int>(normalizeTargetLimit(static_cast<std::size_t>(request.limit <= 0 ? 50 : request.limit)));
    const bool failed_only = request.mode != "all";
    std::vector<application::model::CrawlBatchItemRecord> selected;
    for (int page = 1; static_cast<int>(selected.size()) < request.limit; ++page) {
        application::model::CrawlBatchItemQuery item_query;
        item_query.page = page;
        item_query.page_size = 200;
        auto item_page = batch_query_service_->listItems(batch_id, item_query);
        if (item_page.items.empty()) {
            break;
        }
        for (const auto &item : item_page.items) {
            if (!matchesGovernanceItemFilter(item, request, failed_only)) {
                continue;
            }
            if (!isMetadataBatchItemType(item.item_type)) {
                ++result.skipped_count;
                result.items.push_back(governanceItemFromBatchItem(item, "unsupported_item_type"));
                continue;
            }
            auto copy = item;
            copy.batch_id.clear();
            copy.item_index = static_cast<int>(selected.size());
            copy.state = "Pending";
            copy.retry_count = 0;
            copy.written_count = 0;
            copy.error_category.clear();
            copy.result_code.clear();
            copy.started_at = 0;
            copy.finished_at = 0;
            copy.last_error.clear();
            copy.created_at = 0;
            copy.updated_at = 0;
            selected.push_back(std::move(copy));
            result.items.push_back(governanceItemFromBatchItem(item, "claimed"));
            if (static_cast<int>(selected.size()) >= request.limit) {
                break;
            }
        }
        if (item_page.items.size() < static_cast<std::size_t>(item_page.page_size)) {
            break;
        }
    }

    if (selected.empty()) {
        return result;
    }

    std::vector<std::string> album_ids;
    for (const auto &item : selected) {
        if ((item.item_type == "albumDetail" || item.item_type == "albumSongs") && !item.source_album_id.empty()) {
            album_ids.push_back(item.source_album_id);
        }
    }
    std::unordered_map<std::string, application::model::AlbumSourceMetadata> albums_by_id;
    if (!album_ids.empty() && catalog_query_service_) {
        for (const auto &album : catalog_query_service_->findAlbumSourcesByIds(album_ids)) {
            albums_by_id[album.source_album_id] = album;
        }
    }

    const int per_target_limit = normalizeLimit(request.limit_per_target <= 0 ? batch->limit_count : request.limit_per_target);
    const std::string task_type = batch->task_type.empty() ? "batchGovernance" : batch->task_type + "Governance";
    result.batch_id = startBatchItemsAsync(
        selected,
        per_target_limit,
        task_type,
        request_params.empty() ? batch->request_params : std::move(request_params),
        [this, albums_by_id = std::move(albums_by_id)](std::string target, int item_limit) {
            const auto parsed = target_dispatcher_.dispatch(target);
            if (parsed.kind == CrawlTargetKind::SingerSongs) {
                try {
                    std::optional<int> source_song_total;
                    std::optional<int> source_album_total;
                    if (catalog_query_service_) {
                        if (auto singer = catalog_query_service_->findSinger(std::to_string(parsed.singer_id))) {
                            source_song_total = singer->source_song_total;
                            source_album_total = singer->source_album_total;
                        }
                    }
                    return batchResultFromSingerCollection(workflow_->crawlSingerSongsAndSaveResult(parsed.singer_id, item_limit, maxPageConcurrency(), source_song_total, source_album_total), true);
                } catch (const std::exception &e) {
                    return failureTargetResult(e.what());
                }
            }
            if (parsed.kind == CrawlTargetKind::SingerAlbums) {
                try {
                    std::optional<int> source_song_total;
                    std::optional<int> source_album_total;
                    if (catalog_query_service_) {
                        if (auto singer = catalog_query_service_->findSinger(std::to_string(parsed.singer_id))) {
                            source_song_total = singer->source_song_total;
                            source_album_total = singer->source_album_total;
                        }
                    }
                    return batchResultFromSingerCollection(workflow_->crawlSingerAlbumsAndSaveResult(parsed.singer_id, item_limit, maxPageConcurrency(), source_song_total, source_album_total), true);
                } catch (const std::exception &e) {
                    return failureTargetResult(e.what());
                }
            }
            const auto pos = target.rfind(':');
            const std::string album_id = pos == std::string::npos ? target : target.substr(pos + 1);
            auto album_it = albums_by_id.find(album_id);
            if (album_it == albums_by_id.end()) {
                return BatchTaskManager::TargetResult {false, 0, "album target not found", "invalid_target", "invalid_target"};
            }
            try {
                if (target.rfind("album-songs:", 0) == 0) {
                    return boolTargetResult(workflow_->crawlAlbumSongsAndSave(album_it->second));
                }
                return boolTargetResult(workflow_->crawlAlbumDetailAndSave(album_it->second));
            } catch (const std::exception &e) {
                return failureTargetResult(e.what());
            } catch (...) {
                return failureTargetResult("unknown batch governance crawl error");
            }
        },
        batch_id
    );
    result.claimed_count = static_cast<int>(selected.size());
    if (catalog_query_service_) {
        auto event = governanceEvent(request.mode == "all" ? "batch.clone" : "batch.rerun-failed", "batch", batch_id, result.batch_id, batch->state, "Running", result.batch_id.empty() ? "failed" : "ok", result.batch_id.empty() ? "unknown" : "", request.reason);
        applyBatchSnapshot(event, *batch);
        catalog_query_service_->appendGovernanceEvents({std::move(event)});
    }
    return result;
}

application::model::CrawlBatchGovernanceResult CrawlerService::backfillRetryFromBatch(std::string batch_id, application::model::CrawlBatchGovernanceRequest request, std::string request_params)
{
    application::model::CrawlBatchGovernanceResult result;
    if (!batch_query_service_) {
        return result;
    }
    const auto batch = batch_query_service_->findBatch(batch_id);
    if (!batch) {
        result.items.push_back({"", "", "", "batch_not_found"});
        return result;
    }

    request.limit = static_cast<int>(normalizeTargetLimit(static_cast<std::size_t>(request.limit <= 0 ? 50 : request.limit)));
    std::vector<std::string> source_ids;
    std::string backfill_type;
    std::set<std::string> seen;
    for (int page = 1; static_cast<int>(source_ids.size()) < request.limit; ++page) {
        application::model::CrawlBatchItemQuery item_query;
        item_query.page = page;
        item_query.page_size = 200;
        auto item_page = batch_query_service_->listItems(batch_id, item_query);
        if (item_page.items.empty()) {
            break;
        }
        for (const auto &item : item_page.items) {
            if (!matchesGovernanceItemFilter(item, request, true)) {
                continue;
            }
            const std::string item_backfill_type = backfillTypeFromItemType(item.item_type);
            if (item_backfill_type.empty()) {
                ++result.skipped_count;
                result.items.push_back(governanceItemFromBatchItem(item, "unsupported_item_type"));
                continue;
            }
            if (backfill_type.empty()) {
                backfill_type = item_backfill_type;
            }
            if (item_backfill_type != backfill_type) {
                ++result.skipped_count;
                result.items.push_back(governanceItemFromBatchItem(item, "mixed_item_type_skipped"));
                continue;
            }
            const std::string source_id = sourceIdFromItem(item);
            if (source_id.empty() || !seen.insert(source_id).second) {
                continue;
            }
            source_ids.push_back(source_id);
            result.items.push_back(governanceItemFromBatchItem(item, "claimed"));
            if (static_cast<int>(source_ids.size()) >= request.limit) {
                break;
            }
        }
        if (item_page.items.size() < static_cast<std::size_t>(item_page.page_size)) {
            break;
        }
    }

    if (source_ids.empty() || backfill_type.empty()) {
        return result;
    }
    application::model::MiguBackfillTargetQuery query;
    query.source_ids = std::move(source_ids);
    query.limit = request.limit;
    auto retry = retryMiguBackfill(backfill_type, std::move(query), request.limit_per_target, request_params.empty() ? batch->request_params : std::move(request_params));
    result.batch_id = retry.batch_id;
    result.claimed_count = retry.claimed_count;
    result.skipped_count += retry.skipped_count;
    if (catalog_query_service_) {
        auto event = governanceEvent("batch.backfill-retry", "batch", batch_id, result.batch_id, batch->state, "Running", result.batch_id.empty() ? "failed" : "ok", result.batch_id.empty() ? "unknown" : "", request.reason);
        applyBatchSnapshot(event, *batch);
        catalog_query_service_->appendGovernanceEvents({std::move(event)});
    }
    return result;
}

bool CrawlerService::archiveBatch(std::string batch_id, std::string reason) const
{
    if (!batch_query_service_) {
        return false;
    }
    const auto before = batch_query_service_->findBatch(batch_id);
    const bool archived = batch_query_service_->archiveBatch(batch_id, reason);
    if (archived && catalog_query_service_) {
        auto event = governanceEvent("batch.archive", "batch", batch_id, batch_id, before ? before->state : "", "archived", "ok", "", reason);
        if (before) {
            applyBatchSnapshot(event, *before);
        }
        catalog_query_service_->appendGovernanceEvents({std::move(event)});
    }
    return archived;
}

application::model::MusicOverview CrawlerService::overview() const
{
    return catalog_query_service_ ? catalog_query_service_->overview() : application::model::MusicOverview {};
}

application::model::MiguSingerCatalogPage CrawlerService::listMiguSingers(application::model::MiguSingerCatalogQuery query) const
{
    return catalog_query_service_ ? catalog_query_service_->listSingers(std::move(query)) : application::model::MiguSingerCatalogPage {};
}

std::optional<application::model::MiguSingerCatalogRecord> CrawlerService::findMiguSinger(std::string source_singer_id) const
{
    return catalog_query_service_ ? catalog_query_service_->findSinger(std::move(source_singer_id)) : std::nullopt;
}

application::model::MiguSongCatalogPage CrawlerService::listMiguSingerSongs(std::string source_singer_id, application::model::MiguSongCatalogQuery query) const
{
    query.source_singer_id = std::move(source_singer_id);
    return listMiguSongs(std::move(query));
}

application::model::MiguAlbumCatalogPage CrawlerService::listMiguSingerAlbums(std::string source_singer_id, application::model::MiguAlbumCatalogQuery query) const
{
    query.artist = std::move(source_singer_id);
    return listMiguAlbums(std::move(query));
}

application::model::MiguAlbumCatalogPage CrawlerService::listMiguAlbums(application::model::MiguAlbumCatalogQuery query) const
{
    return catalog_query_service_ ? catalog_query_service_->listAlbums(std::move(query)) : application::model::MiguAlbumCatalogPage {};
}

std::optional<application::model::MiguAlbumCatalogRecord> CrawlerService::findMiguAlbum(std::string source_album_id) const
{
    return catalog_query_service_ ? catalog_query_service_->findAlbum(std::move(source_album_id)) : std::nullopt;
}

application::model::MiguSongCatalogPage CrawlerService::listMiguAlbumSongs(std::string source_album_id, application::model::MiguSongCatalogQuery query) const
{
    query.source_album_id = std::move(source_album_id);
    return listMiguSongs(std::move(query));
}

application::model::MiguSongCatalogPage CrawlerService::listMiguSongs(application::model::MiguSongCatalogQuery query) const
{
    return catalog_query_service_ ? catalog_query_service_->listSongs(std::move(query)) : application::model::MiguSongCatalogPage {};
}

std::optional<application::model::MiguSongCatalogRecord> CrawlerService::findMiguSong(std::string source_song_id) const
{
    return catalog_query_service_ ? catalog_query_service_->findSong(std::move(source_song_id)) : std::nullopt;
}

application::model::MiguResourceQueuePage CrawlerService::listMiguResources(application::model::MiguResourceQueueQuery query) const
{
    return catalog_query_service_ ? catalog_query_service_->listResources(std::move(query)) : application::model::MiguResourceQueuePage {};
}

std::optional<application::model::MiguResourceQueueRecord> CrawlerService::findMiguResource(std::uint64_t resource_id) const
{
    return catalog_query_service_ ? catalog_query_service_->findResource(resource_id) : std::nullopt;
}

application::model::MiguResourcePreviewResult CrawlerService::previewMiguResources(application::model::MiguResourceActionQuery query) const
{
    if (!catalog_query_service_) {
        return {};
    }
    query.limit = static_cast<int>(normalizeTargetLimit(static_cast<std::size_t>(query.limit)));
    return catalog_query_service_->previewResources(std::move(query));
}

application::model::MiguResourceActionResult CrawlerService::retryMiguResources(application::model::MiguResourceActionQuery query) const
{
    application::model::MiguResourceActionResult result;
    if (!catalog_query_service_) {
        return result;
    }
    query.statuses = defaultResourceRetryStatuses(std::move(query.statuses));
    query.limit = static_cast<int>(normalizeTargetLimit(static_cast<std::size_t>(query.limit)));

    if (!query.resource_ids.empty()) {
        for (const auto resource_id : query.resource_ids) {
            auto resource = catalog_query_service_->findResource(resource_id);
            if (!resource) {
                result.items.push_back({resource_id, {}, {}, 0, "not_found"});
                continue;
            }
            const bool status_allowed = std::find(query.statuses.begin(), query.statuses.end(), resource->status) != query.statuses.end();
            result.items.push_back(resourceActionItemFromRecord(*resource, status_allowed ? "retry" : "status_not_selected", status_allowed ? 0 : resource->status));
        }
    } else {
        application::model::MiguResourceQueueQuery preview;
        preview.page = 1;
        preview.page_size = (std::min)(query.limit, 20);
        preview.keyword = query.keyword;
        preview.statuses = query.statuses;
        preview.resource_kind = query.resource_kind;
        preview.format_type = query.format_type;
        for (const auto &resource : catalog_query_service_->listResources(std::move(preview)).items) {
            result.items.push_back(resourceActionItemFromRecord(resource, "retry", 0));
        }
    }

    result.updated_count = catalog_query_service_->retryResources(std::move(query));
    result.success_count = result.updated_count;
    result.failed_count = 0;
    const int requested_count = static_cast<int>(result.items.size());
    result.skipped_count = requested_count == 0 ? 0 : (std::max)(0, requested_count - result.updated_count);
    std::vector<application::model::MiguGovernanceEventRecord> events;
    for (const auto &item : result.items) {
        std::optional<application::model::MiguResourceQueueRecord> resource;
        if (item.resource_id != 0) {
            resource = catalog_query_service_->findResource(item.resource_id);
        }
        events.push_back(resourceGovernanceEvent("resource.retry", item, resource, "retry"));
    }
    catalog_query_service_->appendGovernanceEvents(events);
    return result;
}

application::model::MiguResourceActionResult CrawlerService::resetMiguResources(application::model::MiguResourceActionQuery query) const
{
    application::model::MiguResourceActionResult result;
    if (!catalog_query_service_) {
        return result;
    }
    query.statuses = defaultResourceResetStatuses(std::move(query.statuses));
    query.limit = static_cast<int>(normalizeTargetLimit(static_cast<std::size_t>(query.limit)));

    if (!query.resource_ids.empty()) {
        for (const auto resource_id : query.resource_ids) {
            auto resource = catalog_query_service_->findResource(resource_id);
            if (!resource) {
                result.items.push_back({resource_id, {}, {}, 0, "not_found"});
                continue;
            }
            const bool status_allowed = std::find(query.statuses.begin(), query.statuses.end(), resource->status) != query.statuses.end();
            result.items.push_back(resourceActionItemFromRecord(*resource, status_allowed ? "reset" : "status_not_selected", status_allowed ? 0 : resource->status));
        }
    } else {
        application::model::MiguResourceQueueQuery preview;
        preview.page = 1;
        preview.page_size = (std::min)(query.limit, 20);
        preview.keyword = query.keyword;
        preview.statuses = query.statuses;
        preview.resource_kind = query.resource_kind;
        preview.format_type = query.format_type;
        for (const auto &resource : catalog_query_service_->listResources(std::move(preview)).items) {
            result.items.push_back(resourceActionItemFromRecord(resource, "reset", 0));
        }
    }

    result.updated_count = catalog_query_service_->resetResources(std::move(query));
    result.success_count = result.updated_count;
    result.failed_count = 0;
    const int requested_count = static_cast<int>(result.items.size());
    result.skipped_count = requested_count == 0 ? 0 : (std::max)(0, requested_count - result.updated_count);
    std::vector<application::model::MiguGovernanceEventRecord> events;
    for (const auto &item : result.items) {
        std::optional<application::model::MiguResourceQueueRecord> resource;
        if (item.resource_id != 0) {
            resource = catalog_query_service_->findResource(item.resource_id);
        }
        events.push_back(resourceGovernanceEvent("resource.reset", item, resource, "reset"));
    }
    catalog_query_service_->appendGovernanceEvents(events);
    return result;
}

application::model::MiguResourceActionResult CrawlerService::recoverStaleMiguResources(application::model::MiguResourceActionQuery query) const
{
    application::model::MiguResourceActionResult result;
    if (!catalog_query_service_) {
        return result;
    }
    query.limit = static_cast<int>(normalizeTargetLimit(static_cast<std::size_t>(query.limit)));
    if (query.stale_after_seconds <= 0) {
        query.stale_after_seconds = 3600;
    }
    result.updated_count = catalog_query_service_->recoverStaleResources(std::move(query), "stale running resource recovered");
    result.success_count = result.updated_count;
    if (result.updated_count > 0) {
        catalog_query_service_->appendGovernanceEvents({governanceEvent("resource.recover-stale", "resource", "*", "", "1", "3", "timeout", "timeout", "stale running resource recovered")});
    }
    return result;
}

application::model::SongResourceProcessResult CrawlerService::processMiguResources(application::model::SongResourceClaimQuery query)
{
    if (!song_resource_workflow_) {
        return {};
    }
    if (query.source.empty()) {
        query.source = "migu";
    }
    if (query.batch_id.empty()) {
        query.batch_id = generateResourceProcessBatchId();
    }
    query.limit = normalizeTargetLimit(query.limit);
    query.options = effectiveResourceProcessOptions(getRuntimeSettings(), query.option_overrides);
    const std::string settings_snapshot = runtimeSettingsJson();
    ResourceProcessBatchRecorder recorder(batch_task_repository_, query, settings_snapshot, diagnostics_);
    recorder.begin();
    try {
        auto result = song_resource_workflow_->processPending(std::move(query), &recorder);
        recorder.finish(result);
        return result;
    } catch (const std::exception &e) {
        recorder.failBatch(e.what());
        throw;
    } catch (...) {
        recorder.failBatch("Migu resource process failed with unknown error");
        throw;
    }
}

std::string CrawlerService::startMiguResourcesProcessAsync(application::model::SongResourceClaimQuery query)
{
    if ((!async_song_resource_workflow_ || !async_runtime_) && (!song_resource_workflow_ || !resource_process_runner_.configured())) {
        return {};
    }
    if (!batch_task_repository_) {
        return {};
    }
    if (query.source.empty()) {
        query.source = "migu";
    }
    if (query.batch_id.empty()) {
        query.batch_id = generateResourceProcessBatchId();
    }
    query.limit = normalizeTargetLimit(query.limit);
    query.options = effectiveResourceProcessOptions(getRuntimeSettings(), query.option_overrides);

    if (!async_song_resource_workflow_ || !async_runtime_) {
        auto workflow = song_resource_workflow_;
        auto batch_repository = batch_task_repository_;
        const std::string batch_id = query.batch_id;
        const std::string settings_snapshot = runtimeSettingsJson();
        ResourceProcessBatchRecorder start_recorder(batch_repository, query, settings_snapshot, diagnostics_);
        start_recorder.begin();

        const bool submitted = resource_process_runner_.submitDetached(
            [workflow, batch_repository, diagnostics = diagnostics_, query, settings_snapshot]() mutable {
                ResourceProcessBatchRecorder recorder(batch_repository, query, settings_snapshot, diagnostics);
                try {
                    auto result = workflow->processPending(std::move(query), &recorder);
                    recorder.finish(result);
                } catch (const std::exception &e) {
                    recorder.failBatch(e.what());
                    throw;
                } catch (...) {
                    recorder.failBatch("Migu resource process failed with unknown error");
                    throw;
                }
            },
            "migu resource process"
        );
        if (!submitted) {
            start_recorder.failBatch("Failed to submit Migu resource process task");
            return {};
        }
        return batch_id;
    }

    auto workflow = async_song_resource_workflow_;
    auto runtime = async_runtime_;
    auto batch_repository = batch_task_repository_;
    const std::string batch_id = query.batch_id;
    const std::string settings_snapshot = runtimeSettingsJson();
    ResourceProcessBatchRecorder start_recorder(batch_repository, query, settings_snapshot, diagnostics_);
    start_recorder.begin();

    const bool submitted = async_runtime_->spawn(runAsyncResourceProcessTask(workflow, runtime, batch_repository, diagnostics_, query, settings_snapshot));
    if (!submitted) {
        start_recorder.failBatch("Failed to submit Migu resource process task");
        return {};
    }
    return batch_id;
}

application::model::MiguIntegritySummary CrawlerService::miguIntegrity(int stale_after_seconds) const
{
    return catalog_query_service_ ? catalog_query_service_->integrity(stale_after_seconds) : application::model::MiguIntegritySummary {};
}

application::model::MiguSchemaStatus CrawlerService::miguSchemaStatus() const
{
    return catalog_query_service_ ? catalog_query_service_->schemaStatus() : application::model::MiguSchemaStatus {};
}

application::model::MiguGovernanceEventPage CrawlerService::listMiguGovernanceEvents(application::model::MiguGovernanceEventQuery query) const
{
    return catalog_query_service_ ? catalog_query_service_->listGovernanceEvents(std::move(query)) : application::model::MiguGovernanceEventPage {};
}

application::model::MiguBackfillRetryResult CrawlerService::retryMiguBackfill(std::string backfill_type, application::model::MiguBackfillTargetQuery query, int limit_per_target, std::string request_params)
{
    query.statuses = defaultBackfillStatuses(std::move(query.statuses));
    query.limit = static_cast<int>(normalizeTargetLimit(static_cast<std::size_t>(query.limit)));
    application::model::MiguBackfillRetryResult retry;
    if (backfill_type == "singer-songs" || backfill_type == "singer-albums") {
        if (!batch_task_manager_ || !workflow_ || !singer_source_sync_queue_ || !catalog_query_service_) {
            return retry;
        }

        auto claim = backfill_type == "singer-songs" ? catalog_query_service_->claimSingerSongs(query) : catalog_query_service_->claimSingerAlbums(query);
        retry.claimed_count = claim.claimed_count;
        retry.skipped_count = claim.skipped_count;
        for (const auto &singer : claim.singers) {
            retry.items.push_back({singer.source_singer_id, singer.name, 1, "claimed"});
        }
        if (!query.source_ids.empty()) {
            for (const auto &source_id : query.source_ids) {
                const auto claimed = std::find_if(claim.singers.begin(), claim.singers.end(), [&source_id](const auto &singer) {
                    return singer.source_singer_id == source_id;
                });
                if (claimed == claim.singers.end()) {
                    auto singer = catalog_query_service_->findSinger(source_id);
                    if (!singer) {
                        retry.items.push_back({source_id, {}, 0, "not_found"});
                    } else {
                        const int status = backfill_type == "singer-songs" ? singer->songs_sync_status : singer->albums_sync_status;
                        retry.items.push_back({source_id, singer->name, status, skippedReasonForStatus(status, query.statuses)});
                    }
                }
            }
        }

        std::unordered_map<std::string, application::model::MiguSingerCatalogRecord> by_key;
        std::vector<application::model::CrawlBatchItemRecord> items;
        items.reserve(claim.singers.size());
        for (std::size_t index = 0; index < claim.singers.size(); ++index) {
            auto singer = claim.singers[index];
            if (singer.source_singer_id.empty()) {
                continue;
            }
            const std::string key = backfill_type == "singer-songs" ? "singer:" + singer.source_singer_id : "singer-albums:" + singer.source_singer_id;
            by_key[key] = singer;

            application::model::CrawlBatchItemRecord item;
            item.item_index = static_cast<int>(items.size());
            item.item_key = key;
            item.item_type = backfill_type == "singer-songs" ? "singerSongs" : "singerAlbums";
            item.item_label = singer.name.empty() ? singer.source_singer_id : singer.name;
            item.source = "migu";
            item.source_singer_id = singer.source_singer_id;
            item.payload = "{\"force\":" + std::string(query.force ? "true" : "false") + ",\"source_country\":\"" + escapeJsonString(singer.source_country) + "\",\"gender\":" + std::to_string(singer.gender)
                           + ",\"source_song_total\":" + (singer.source_song_total ? std::to_string(*singer.source_song_total) : "null")
                           + ",\"source_album_total\":" + (singer.source_album_total ? std::to_string(*singer.source_album_total) : "null") + "}";
            items.push_back(std::move(item));
        }

        auto queue = singer_source_sync_queue_;
        const std::string task_type = backfill_type == "singer-songs" ? "singerSongsRetry" : "singerAlbumsRetry";
        retry.batch_id = startBatchItemsAsync(
            std::move(items),
            normalizeLimit(limit_per_target),
            task_type,
            std::move(request_params),
            [this, queue, backfill_type, force = query.force, by_key = std::move(by_key)](std::string target, int item_limit) {
                auto it = by_key.find(target);
                if (it == by_key.end()) {
                    return BatchTaskManager::TargetResult {false, 0, "singer target not found", "invalid_target", "invalid_target"};
                }
                const auto &source = it->second;
                const auto singer_id = parseSingerId(source.source_singer_id);
                if (singer_id == 0) {
                    return BatchTaskManager::TargetResult {false, 0, "invalid singer id", "invalid_target", "invalid_target"};
                }

                application::model::SingerCollectionResult result;
                try {
                    result = backfill_type == "singer-songs" ? workflow_->crawlSingerSongsAndSaveResult(singer_id, item_limit, maxPageConcurrency(), source.source_song_total, source.source_album_total, force) : workflow_->crawlSingerAlbumsAndSaveResult(singer_id, item_limit, maxPageConcurrency(), source.source_song_total, source.source_album_total, force);
                } catch (const std::exception &e) {
                    result = {false, false, 0, e.what()};
                } catch (...) {
                    result = {false, false, 0, "unknown singer retry error"};
                }

                if (backfill_type == "singer-songs") {
                    if (!result.source_song_total && source.source_song_total) {
                        result.source_song_total = source.source_song_total;
                    }
                    applySingerDeclaredTotalCompletion(result, result.source_song_total, "singer songs");
                    if (catalog_query_service_) {
                        if (auto singer = catalog_query_service_->findSinger(source.source_singer_id)) {
                            applyRealSingerCountCompletion(result, *singer, true);
                        }
                    }
                } else {
                    if (!result.source_album_total && source.source_album_total) {
                        result.source_album_total = source.source_album_total;
                    }
                    applySingerDeclaredTotalCompletion(result, result.source_album_total, "singer albums");
                    if (catalog_query_service_) {
                        if (auto singer = catalog_query_service_->findSinger(source.source_singer_id)) {
                            applyRealSingerCountCompletion(result, *singer, false);
                        }
                    }
                }
                const bool updated = backfill_type == "singer-songs" ? queue->updateSongsSyncStatus("migu", source.source_singer_id, syncStatusFromResult(result, result.source_song_total), result.written_count, result.error) : queue->updateAlbumsSyncStatus("migu", source.source_singer_id, syncStatusFromResult(result, result.source_album_total), result.written_count, result.error);
                return batchResultFromSingerCollection(result, updated);
            }
        );
        if (catalog_query_service_) {
            std::vector<application::model::MiguGovernanceEventRecord> events;
            events.reserve(retry.items.size());
            for (const auto &item : retry.items) {
                events.push_back(backfillGovernanceEvent(backfill_type, item, retry.batch_id));
            }
            catalog_query_service_->appendGovernanceEvents(events);
        }
        return retry;
    }

    if (backfill_type == "album-details" || backfill_type == "album-songs") {
        if (!batch_task_manager_ || !workflow_ || !catalog_query_service_) {
            return retry;
        }
        auto claim = backfill_type == "album-details" ? catalog_query_service_->claimAlbumDetails(query) : catalog_query_service_->claimAlbumSongs(query);
        retry.claimed_count = claim.claimed_count;
        retry.skipped_count = claim.skipped_count;
        for (const auto &album : claim.albums) {
            retry.items.push_back({album.source_album_id, album.source_title.empty() ? album.title : album.source_title, 1, "claimed"});
        }
        if (!query.source_ids.empty()) {
            for (const auto &source_id : query.source_ids) {
                const auto claimed = std::find_if(claim.albums.begin(), claim.albums.end(), [&source_id](const auto &album) {
                    return album.source_album_id == source_id;
                });
                if (claimed == claim.albums.end()) {
                    auto album = catalog_query_service_->findAlbum(source_id);
                    if (!album) {
                        retry.items.push_back({source_id, {}, 0, "not_found"});
                    } else if (backfill_type == "album-songs" && album->detail_sync_status != 2) {
                        retry.items.push_back({source_id, album->source_title.empty() ? album->title : album->source_title, album->songs_sync_status, "album_detail_not_completed"});
                    } else {
                        const int status = backfill_type == "album-details" ? album->detail_sync_status : album->songs_sync_status;
                        retry.items.push_back({source_id, album->source_title.empty() ? album->title : album->source_title, status, skippedReasonForStatus(status, query.statuses)});
                    }
                }
            }
        }
        std::vector<application::model::AlbumSourceMetadata> albums;
        albums.reserve(claim.albums.size());
        for (const auto &record : claim.albums) {
            albums.push_back(albumSourceFromRecord(record));
        }
        retry.batch_id = backfill_type == "album-details" ? startAlbumDetailsBackfillItemsAsync(std::move(albums), limit_per_target, std::move(request_params)) : startAlbumSongsBackfillItemsAsync(std::move(albums), limit_per_target, std::move(request_params));
        if (catalog_query_service_) {
            std::vector<application::model::MiguGovernanceEventRecord> events;
            events.reserve(retry.items.size());
            for (const auto &item : retry.items) {
                events.push_back(backfillGovernanceEvent(backfill_type, item, retry.batch_id));
            }
            catalog_query_service_->appendGovernanceEvents(events);
        }
        return retry;
    }

    return retry;
}

application::model::MiguBackfillRetryResult CrawlerService::refreshMiguSingerProfiles(application::model::MiguBackfillTargetQuery query, bool force, std::string request_params)
{
    application::model::MiguBackfillRetryResult result;
    if (!batch_task_manager_ || !workflow_ || !catalog_query_service_) {
        return result;
    }

    query.source = query.source.empty() ? "migu" : std::move(query.source);
    const bool explicit_source_ids = !query.source_ids.empty();
    const bool run_all = !explicit_source_ids && query.limit == 0;
    const std::size_t target_limit = run_all ? std::numeric_limits<std::size_t>::max() : normalizeTargetLimit(static_cast<std::size_t>(query.limit));

    std::vector<application::model::MiguSingerCatalogRecord> targets;
    if (!run_all) {
        targets.reserve(target_limit);
    }
    const auto should_refresh = [force](const application::model::MiguSingerCatalogRecord &singer) {
        return force || !singer.source_song_total || !singer.source_album_total;
    };

    if (explicit_source_ids) {
        for (const auto &source_id : query.source_ids) {
            auto singer = catalog_query_service_->findSinger(source_id);
            if (!singer) {
                result.items.push_back({source_id, {}, 0, "not_found"});
                ++result.skipped_count;
                continue;
            }
            if (!should_refresh(*singer)) {
                result.items.push_back({source_id, singer->name, 2, "profile_totals_present"});
                ++result.skipped_count;
                continue;
            }
            targets.push_back(*singer);
            result.items.push_back({source_id, singer->name, 1, "claimed"});
        }
    } else {
        for (int page = 1; targets.size() < target_limit; ++page) {
            application::model::MiguSingerCatalogQuery list_query;
            list_query.source = query.source;
            list_query.page = page;
            list_query.page_size = 200;
            list_query.keyword = query.keyword;
            list_query.country = query.country;
            list_query.gender = query.gender;
            auto page_result = catalog_query_service_->listSingers(list_query);
            if (page_result.items.empty()) {
                break;
            }
            for (const auto &singer : page_result.items) {
                if (!should_refresh(singer)) {
                    continue;
                }
                targets.push_back(singer);
                result.items.push_back({singer.source_singer_id, singer.name, 1, "claimed"});
                if (targets.size() >= target_limit) {
                    break;
                }
            }
            if (page_result.items.size() < static_cast<std::size_t>(page_result.page_size)) {
                break;
            }
        }
    }

    result.claimed_count = static_cast<int>(targets.size());
    if (targets.empty()) {
        return result;
    }

    std::unordered_map<std::string, application::model::MiguSingerCatalogRecord> by_key;
    std::vector<application::model::CrawlBatchItemRecord> items;
    items.reserve(targets.size());
    for (std::size_t index = 0; index < targets.size(); ++index) {
        const auto &singer = targets[index];
        const std::string key = "singer-profile:" + singer.source_singer_id;
        by_key[key] = singer;

        application::model::CrawlBatchItemRecord item;
        item.item_index = static_cast<int>(items.size());
        item.item_key = key;
        item.item_type = "singerProfile";
        item.item_label = singer.name.empty() ? singer.source_singer_id : singer.name;
        item.source = singer.source;
        item.source_singer_id = singer.source_singer_id;
        item.payload = "{\"force\":" + std::string(force ? "true" : "false")
                       + ",\"source_song_total\":" + (singer.source_song_total ? std::to_string(*singer.source_song_total) : "null")
                       + ",\"source_album_total\":" + (singer.source_album_total ? std::to_string(*singer.source_album_total) : "null") + "}";
        items.push_back(std::move(item));
    }

    auto queue = singer_source_sync_queue_;
    result.batch_id = startBatchItemsAsync(
        std::move(items),
        1,
        "singerProfileRefresh",
        std::move(request_params),
        [this, queue, by_key = std::move(by_key)](std::string target, int) {
            auto it = by_key.find(target);
            if (it == by_key.end()) {
                return BatchTaskManager::TargetResult {false, 0, "singer profile target not found", "invalid_target", "invalid_target"};
            }

            const auto &source = it->second;
            const auto singer_id = parseSingerId(source.source_singer_id);
            if (singer_id == 0) {
                return BatchTaskManager::TargetResult {false, 0, "invalid singer id", "invalid_target", "invalid_target"};
            }

            application::model::SingerSourceProfile profile;
            try {
                profile = workflow_->fetchAndSaveSingerSourceProfile(singer_id);
            } catch (const std::exception &e) {
                return failureTargetResult(e.what());
            } catch (...) {
                return failureTargetResult("unknown singer profile refresh error");
            }
            const bool refreshed = profile.source_song_total || profile.source_album_total;
            if (!refreshed) {
                return BatchTaskManager::TargetResult {false, 0, "singer profile did not contain declared totals", "parse", "parse_failed"};
            }

            if (queue && catalog_query_service_) {
                if (auto singer = catalog_query_service_->findSinger(source.source_singer_id)) {
                    (void)queue->updateSongsSyncStatus(singer->source, singer->source_singer_id, singer->songs_sync_status, singer->songs_sync_count, singer->songs_sync_error);
                    (void)queue->updateAlbumsSyncStatus(singer->source, singer->source_singer_id, singer->albums_sync_status, singer->albums_sync_count, singer->albums_sync_error);
                }
            }
            return BatchTaskManager::TargetResult {true, 1, {}, {}, "ok"};
        }
    );
    return result;
}

int CrawlerService::resetMiguBackfill(std::string backfill_type, application::model::MiguBackfillTargetQuery query) const
{
    query.statuses = defaultBackfillStatuses(std::move(query.statuses));
    query.limit = static_cast<int>(normalizeTargetLimit(static_cast<std::size_t>(query.limit)));
    if (!catalog_query_service_) {
        return 0;
    }
    if (backfill_type == "singer-songs") {
        return catalog_query_service_->resetSingerSongs(std::move(query));
    }
    if (backfill_type == "singer-albums") {
        return catalog_query_service_->resetSingerAlbums(std::move(query));
    }
    if (backfill_type == "album-details") {
        return catalog_query_service_->resetAlbumDetails(std::move(query));
    }
    if (backfill_type == "album-songs") {
        return catalog_query_service_->resetAlbumSongs(std::move(query));
    }
    return 0;
}

application::model::MiguStaleRecoveryResult CrawlerService::recoverMiguStale(application::model::MiguStaleRecoveryRequest request) const
{
    if (request.source.empty()) {
        request.source = "migu";
    }
    request.stale_after_seconds = (std::max)(1, request.stale_after_seconds);
    const std::string error = "stale running recovered";

    application::model::MiguStaleRecoveryResult result;
    result.recovered_at = nowSeconds();
    if (catalog_query_service_) {
        if (includesRecoveryType(request.types, "singer-songs")) {
            result.singer_songs_recovered = catalog_query_service_->recoverStaleSingerSongs(request.source, request.stale_after_seconds, error);
        }
        if (includesRecoveryType(request.types, "singer-albums")) {
            result.singer_albums_recovered = catalog_query_service_->recoverStaleSingerAlbums(request.source, request.stale_after_seconds, error);
        }
        if (includesRecoveryType(request.types, "album-details")) {
            result.album_details_recovered = catalog_query_service_->recoverStaleAlbumDetails(request.source, request.stale_after_seconds, error);
        }
        if (includesRecoveryType(request.types, "album-songs")) {
            result.album_songs_recovered = catalog_query_service_->recoverStaleAlbumSongs(request.source, request.stale_after_seconds, error);
        }
        if (includesRecoveryType(request.types, "resources")) {
            application::model::MiguResourceActionQuery query;
            query.source = request.source;
            query.limit = 1000;
            query.stale_after_seconds = request.stale_after_seconds;
            for (;;) {
                const int recovered = catalog_query_service_->recoverStaleResources(query, error);
                if (recovered <= 0) {
                    break;
                }
                result.resources_recovered += recovered;
                if (recovered < query.limit) {
                    break;
                }
            }
        }
    }
    if (batch_query_service_ && includesRecoveryType(request.types, "batches")) {
        result.batches_recovered = batch_query_service_->recoverStaleRunningBatches(request.source, request.stale_after_seconds, error);
    }
    return result;
}

application::model::CrawlerRuntimeSettings CrawlerService::getRuntimeSettings() const
{
    std::lock_guard<std::mutex> lock(settings_mutex_);
    return runtime_settings_;
}

std::optional<std::string> CrawlerService::updateRuntimeSettings(const application::model::CrawlerRuntimeSettingsUpdate &update)
{
    if (update.max_batch_concurrency && (*update.max_batch_concurrency < 1 || *update.max_batch_concurrency > 128)) {
        return "max_batch_concurrency must be between 1 and 128";
    }
    if (update.max_page_concurrency && (*update.max_page_concurrency < 1 || *update.max_page_concurrency > 128)) {
        return "max_page_concurrency must be between 1 and 128";
    }
    if (update.max_retries && (*update.max_retries < 0 || *update.max_retries > 10)) {
        return "max_retries must be between 0 and 10";
    }
    if (update.task_timeout_ms && (*update.task_timeout_ms < 1000 || *update.task_timeout_ms > 86400000)) {
        return "task_timeout_ms must be between 1000 and 86400000";
    }
    if (update.resource_download_concurrency && (*update.resource_download_concurrency < 1 || *update.resource_download_concurrency > 32)) {
        return "resource_download_concurrency must be between 1 and 32";
    }
    if (update.resource_download_retries && (*update.resource_download_retries < 0 || *update.resource_download_retries > 5)) {
        return "resource_download_retries must be between 0 and 5";
    }
    if (update.resource_download_timeout_ms && (*update.resource_download_timeout_ms < 10000 || *update.resource_download_timeout_ms > 600000)) {
        return "resource_download_timeout_ms must be between 10000 and 600000";
    }
    if (update.pacmtoken_refresh_interval_seconds && (*update.pacmtoken_refresh_interval_seconds < 0 || *update.pacmtoken_refresh_interval_seconds > 3600)) {
        return "pacmtoken_refresh_interval_seconds must be between 0 and 3600";
    }

    std::lock_guard<std::mutex> lock(settings_mutex_);
    if (update.max_batch_concurrency) {
        runtime_settings_.max_batch_concurrency = *update.max_batch_concurrency;
    }
    if (update.max_page_concurrency) {
        runtime_settings_.max_page_concurrency = *update.max_page_concurrency;
    }
    if (update.max_retries) {
        runtime_settings_.max_retries = *update.max_retries;
    }
    if (update.task_timeout_ms) {
        runtime_settings_.task_timeout_ms = *update.task_timeout_ms;
    }
    if (update.resource_download_concurrency) {
        runtime_settings_.resource_download_concurrency = *update.resource_download_concurrency;
    }
    if (update.resource_download_retries) {
        runtime_settings_.resource_download_retries = *update.resource_download_retries;
    }
    if (update.resource_download_timeout_ms) {
        runtime_settings_.resource_download_timeout_ms = *update.resource_download_timeout_ms;
    }
    if (update.pacmtoken_refresh_interval_seconds) {
        runtime_settings_.pacmtoken_refresh_interval_seconds = *update.pacmtoken_refresh_interval_seconds;
    }
    if (update.auto_refresh_pacmtoken) {
        runtime_settings_.auto_refresh_pacmtoken = *update.auto_refresh_pacmtoken;
    }
    if (update.refresh_pacmtoken_on_440013) {
        runtime_settings_.refresh_pacmtoken_on_440013 = *update.refresh_pacmtoken_on_440013;
    }
    return std::nullopt;
}

int CrawlerService::maxBatchConcurrency() const
{
    std::lock_guard<std::mutex> lock(settings_mutex_);
    return runtime_settings_.max_batch_concurrency;
}

int CrawlerService::maxPageConcurrency() const
{
    std::lock_guard<std::mutex> lock(settings_mutex_);
    return runtime_settings_.max_page_concurrency;
}

int CrawlerService::maxRetries() const
{
    std::lock_guard<std::mutex> lock(settings_mutex_);
    return runtime_settings_.max_retries;
}

std::chrono::milliseconds CrawlerService::taskTimeout() const
{
    std::lock_guard<std::mutex> lock(settings_mutex_);
    return std::chrono::milliseconds(runtime_settings_.task_timeout_ms);
}

std::string CrawlerService::runtimeSettingsJson() const
{
    const auto settings = getRuntimeSettings();
    std::ostringstream out;
    out << "{\"max_batch_concurrency\":" << settings.max_batch_concurrency
        << ",\"max_page_concurrency\":" << settings.max_page_concurrency
        << ",\"max_retries\":" << settings.max_retries
        << ",\"task_timeout_ms\":" << settings.task_timeout_ms
        << ",\"resource_download_concurrency\":" << settings.resource_download_concurrency
        << ",\"resource_download_retries\":" << settings.resource_download_retries
        << ",\"resource_download_timeout_ms\":" << settings.resource_download_timeout_ms
        << ",\"pacmtoken_refresh_interval_seconds\":" << settings.pacmtoken_refresh_interval_seconds
        << ",\"auto_refresh_pacmtoken\":" << (settings.auto_refresh_pacmtoken ? "true" : "false")
        << ",\"refresh_pacmtoken_on_440013\":" << (settings.refresh_pacmtoken_on_440013 ? "true" : "false")
        << '}';
    return out.str();
}

} // namespace orchestrator::modules::crawler::application::use_cases
