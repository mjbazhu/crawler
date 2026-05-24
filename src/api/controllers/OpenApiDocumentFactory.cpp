#include "crawler/api/controllers/OpenApiDocumentFactory.h"

#include <nlohmann/json.hpp>

#include <string>
#include <utility>

namespace orchestrator::modules::crawler::api::controllers {

using json = nlohmann::json;

namespace {

// 创建 OpenAPI schema 引用对象。
json ref(std::string value)
{
    return json {{"$ref", std::move(value)}};
}

// 创建带说明的 OpenAPI schema 引用对象。
json describedRef(std::string value, std::string description)
{
    return json {{"allOf", json::array({ref(std::move(value))})}, {"description", std::move(description)}};
}

// 创建 application/json 响应或请求内容对象。
json jsonContent(const json &schema)
{
    return json {{"application/json", {{"schema", schema}}}};
}

// 创建标准错误响应。
json errorResponse(std::string description)
{
    return json {
        {"description", std::move(description)},
        {"content", jsonContent(ref("#/components/schemas/ErrorResponse"))},
    };
}

// 创建带 JSON body 的请求体对象。
json requestBody(const json &schema, bool required = true)
{
    return json {
        {"required", required},
        {"content", jsonContent(schema)},
    };
}

// Migu 歌手分类枚举。
json singerTabEnum()
{
    return json::array({
        "huayu-nan",
        "huayu-nv",
        "huayu-group",
        "oumei-nan",
        "oumei-nv",
        "oumei-group",
        "rihan-nan",
        "rihan-nv",
        "rihan-group",
    });
}

// 通用 limit schema。
json limitSchema()
{
    return json {{"type", "integer"}, {"minimum", 1}, {"maximum", 500}, {"default", 20}, {"description", "Maximum items per selected target."}};
}

// 歌手分类入口 limit schema；0 表示每个分类全部歌手。
json singerIndexLimitSchema()
{
    return json {{"type", "integer"}, {"minimum", 0}, {"default", 20}, {"description", "Maximum singers per selected category. Use 0 to crawl all singers parsed from each category."}};
}

// 歌手 ID 采集入口 limit schema；0 表示每个歌手全部歌曲或专辑。
json singerIdsLimitSchema()
{
    return json {{"type", "integer"}, {"minimum", 0}, {"maximum", 500}, {"default", 0}, {"description", "Maximum items per selected target. Use 0 to crawl all items for each selected singer."}};
}

// 通用区间字段 schema。
json rangeProperties()
{
    return json {
        {"start_index", {{"type", "integer"}, {"minimum", 0}, {"default", 0}, {"description", "Inclusive 0-based start index."}}},
        {"startPosition", {{"type", "integer"}, {"minimum", 0}, {"description", "Alias of start_index."}}},
        {"start", {{"type", "integer"}, {"minimum", 0}, {"description", "Alias of start_index."}}},
        {"end_index", {{"type", "integer"}, {"minimum", 1}, {"description", "Exclusive 0-based end index. Omit it to process to the array end."}}},
        {"endPosition", {{"type", "integer"}, {"minimum", 1}, {"description", "Alias of end_index."}}},
        {"end", {{"type", "integer"}, {"minimum", 1}, {"description", "Alias of end_index."}}},
    };
}

// 创建只包含当前 Migu 元数据链路的 schema。
void buildSchemas(json &schemas)
{
    schemas["ErrorResponse"] = {
        {"type", "object"},
        {"required", json::array({"error_code", "message", "error"})},
        {"properties",
         {
             {"error_code", {{"type", "string"}, {"description", "Stable frontend-facing error code."}}},
             {"message", {{"type", "string"}, {"description", "Human-readable Chinese or English error message."}}},
             {"error", {{"type", "string"}, {"description", "Backward-compatible alias of message."}}},
             {"details", {{"nullable", true}, {"description", "Optional structured details for field validation."}}},
         }},
    };
    schemas["SingerTab"] = {
        {"type", "string"},
        {"enum", singerTabEnum()},
        {"description", "Migu singer category tab."},
    };
    schemas["MiguId"] = {
        {"description", "Migu numeric ID. Frontend should prefer integer."},
        {"oneOf", json::array({
                      json {{"type", "integer"}, {"minimum", 1}},
                      json {{"type", "string"}, {"minLength", 1}, {"maxLength", 64}},
                  })},
    };

    json index_properties = rangeProperties();
    index_properties["tab"] = ref("#/components/schemas/SingerTab");
    index_properties["singer_tab"] = ref("#/components/schemas/SingerTab");
    index_properties["singerTab"] = ref("#/components/schemas/SingerTab");
    index_properties["tabs"] = {{"type", "array"}, {"minItems", 1}, {"maxItems", 1000}, {"items", ref("#/components/schemas/SingerTab")}};
    index_properties["singer_tabs"] = index_properties["tabs"];
    index_properties["singerTabs"] = index_properties["tabs"];
    index_properties["limit"] = singerIndexLimitSchema();
    schemas["SingerIndexCrawlRequest"] = {
        {"type", "object"},
        {"description", "Crawl Migu singer catalog by category tab. Provide tab/singer_tab or tabs/singer_tabs."},
        {"anyOf", json::array({json {{"required", json::array({"tab"})}}, json {{"required", json::array({"singer_tab"})}}, json {{"required", json::array({"tabs"})}}})},
        {"properties", index_properties},
    };

    json singer_id_properties = rangeProperties();
    singer_id_properties["singer_id"] = ref("#/components/schemas/MiguId");
    singer_id_properties["singerId"] = ref("#/components/schemas/MiguId");
    singer_id_properties["singer_ids"] = {{"type", "array"}, {"minItems", 1}, {"maxItems", 1000}, {"items", ref("#/components/schemas/MiguId")}};
    singer_id_properties["singerIds"] = singer_id_properties["singer_ids"];
    singer_id_properties["limit"] = singerIdsLimitSchema();
    schemas["SingerIdsCrawlRequest"] = {
        {"type", "object"},
        {"description", "Crawl Migu singer songs or singer albums by singer IDs."},
        {"anyOf", json::array({json {{"required", json::array({"singer_id"})}}, json {{"required", json::array({"singer_ids"})}}})},
        {"properties", singer_id_properties},
    };

    schemas["AlbumBackfillRequest"] = {
        {"type", "object"},
        {"description", "Backfill pending album records from album_source_refs. source currently only supports migu."},
        {"properties",
         {
             {"source", {{"type", "string"}, {"enum", json::array({"migu"})}, {"default", "migu"}}},
             {"limit", limitSchema()},
         }},
    };
    schemas["SingerFromDbFilter"] = {
        {"type", "object"},
        {"description", "Optional filters for singer_source_refs. Empty country, gender or q means no filtering."},
        {"properties",
         {
             {"sync_status", {{"type", "array"}, {"items", {{"type", "integer"}, {"minimum", 0}, {"maximum", 4}}}, {"default", json::array({0, 3, 4})}}},
             {"syncStatus", {{"type", "array"}, {"items", {{"type", "integer"}, {"minimum", 0}, {"maximum", 4}}}}},
             {"country", {{"type", "string"}, {"maxLength", 128}, {"description", "Optional source_country filter. Empty string disables it."}}},
             {"gender", {{"nullable", true}, {"type", "integer"}, {"minimum", 0}, {"maximum", 3}, {"description", "Optional singers.gender filter. Omit or null to disable it."}}},
             {"q", {{"type", "string"}, {"maxLength", 128}, {"description", "Optional singer name/source_singer_id fuzzy keyword. Empty string disables it."}}},
         }},
    };
    schemas["SingerFromDbCrawlRequest"] = {
        {"type", "object"},
        {"description", "Claim singers from singer_source_refs by sync status and start singer songs or albums crawl. Empty filter fields do not constrain the queue."},
        {"properties",
         {
             {"source", {{"type", "string"}, {"enum", json::array({"migu"})}, {"default", "migu"}}},
             {"filter", ref("#/components/schemas/SingerFromDbFilter")},
             {"max_targets", {{"type", "integer"}, {"minimum", 1}, {"maximum", 1000}, {"default", 20}, {"description", "Maximum singers to claim from singer_source_refs."}}},
             {"maxTargets", {{"type", "integer"}, {"minimum", 1}, {"maximum", 1000}}},
             {"limit_per_singer", {{"type", "integer"}, {"minimum", 1}, {"maximum", 500}, {"default", 20}, {"description", "Maximum songs or albums fetched for each singer."}}},
             {"limitPerSinger", {{"type", "integer"}, {"minimum", 1}, {"maximum", 500}}},
             {"include_stale_running", {{"type", "boolean"}, {"default", false}, {"description", "Whether to reclaim targets stuck in status 1 longer than stale_after_seconds."}}},
             {"stale_after_seconds", {{"type", "integer"}, {"minimum", 60}, {"maximum", 86400}, {"default", 3600}}},
         }},
    };
    schemas["SingerFromDbPreviewItem"] = {
        {"type", "object"},
        {"required", json::array({"singer_id", "source_name", "sync_status"})},
        {"properties",
         {
             {"singer_id", {{"type", "string"}, {"description", "Migu source_singer_id."}}},
             {"source_name", {{"type", "string"}, {"description", "Singer display name from local catalog."}}},
             {"country", {{"type", "string"}, {"description", "Optional source_country filter value."}}},
             {"gender", {{"type", "integer"}, {"minimum", 0}, {"maximum", 3}}},
             {"sync_status", {{"type", "integer"}, {"minimum", 0}, {"maximum", 4}}},
         }},
    };
    schemas["SingerFromDbPreviewResponse"] = {
        {"type", "object"},
        {"required", json::array({"matched_count", "eligible_count", "items"})},
        {"properties",
         {
             {"matched_count", {{"type", "integer"}, {"minimum", 0}, {"description", "Rows matching source/country/gender/q before sync-status eligibility is applied."}}},
             {"eligible_count", {{"type", "integer"}, {"minimum", 0}, {"description", "Rows that match the current sync-status and stale-running policy."}}},
             {"items", {{"type", "array"}, {"items", ref("#/components/schemas/SingerFromDbPreviewItem")}}},
         }},
    };
    schemas["TaskResponse"] = {
        {"type", "object"},
        {"required", json::array({"batch_id", "message", "target_count", "limit"})},
        {"properties",
         {
             {"batch_id", {{"type", "string"}, {"description", "Background task ID used for progress polling and stop requests."}}},
             {"message", {{"type", "string"}}},
             {"target", {{"type", "string"}, {"description", "Only returned by album backfill endpoints."}}},
             {"target_count", {{"type", "integer"}, {"minimum", 0}}},
             {"claimed_count", {{"type", "integer"}, {"minimum", 0}, {"description", "Only returned by backfill retry endpoints. Number of targets claimed in database."}}},
             {"skipped_count", {{"type", "integer"}, {"minimum", 0}, {"description", "Only returned by backfill retry endpoints. Explicit IDs skipped because they do not exist or are not in claimable status."}}},
             {"success_count", {{"type", "integer"}, {"minimum", 0}}},
             {"failed_count", {{"type", "integer"}, {"minimum", 0}}},
             {"items", {{"type", "array"}, {"items", ref("#/components/schemas/BackfillActionItem")}}},
             {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 500}}},
             {"start_index", {{"type", "integer"}, {"description", "Only returned by list endpoints."}}},
             {"end_index", {{"type", "integer"}, {"description", "Only returned by list endpoints."}}},
         }},
    };
    schemas["StopResponse"] = {
        {"type", "object"},
        {"required", json::array({"batch_id", "message"})},
        {"properties",
         {
             {"batch_id", {{"type", "string"}}},
             {"message", {{"type", "string"}, {"example", "migu task stop requested"}}},
         }},
    };
    schemas["Progress"] = {
        {"type", "object"},
        {"required", json::array({"batch_id", "state", "total", "processed", "success", "failed", "duration_seconds", "percent"})},
        {"properties",
         {
             {"batch_id", {{"type", "string"}}},
             {"state", {{"type", "string"}, {"enum", json::array({"Pending", "Running", "Completed", "Failed", "Cancelled"})}}},
             {"total", {{"type", "integer"}, {"minimum", 0}}},
             {"processed", {{"type", "integer"}, {"minimum", 0}}},
             {"success", {{"type", "integer"}, {"minimum", 0}}},
             {"failed", {{"type", "integer"}, {"minimum", 0}}},
             {"current_item", {{"type", "string"}}},
             {"recent_errors", {{"type", "array"}, {"items", {{"type", "string"}}}}},
             {"duration_seconds", {{"type", "integer"}, {"minimum", 0}}},
             {"percent", {{"type", "number"}, {"minimum", 0}, {"maximum", 100}}},
         }},
    };
    schemas["BatchTask"] = {
        {"type", "object"},
        {"required", json::array({"batch_id", "type", "state", "target_count", "processed", "success", "failed", "created_at", "duration_seconds"})},
        {"properties",
         {
             {"batch_id", {{"type", "string"}}},
             {"source", {{"type", "string"}, {"example", "migu"}}},
             {"type", {{"type", "string"}, {"enum", json::array({"singerIndex", "singerSongs", "singerAlbums", "singerSongsFromDb", "singerAlbumsFromDb", "albumDetails", "albumSongs"})}}},
             {"state", {{"type", "string"}, {"enum", json::array({"Pending", "Running", "Completed", "Failed", "Cancelled"})}}},
             {"request_params", {{"type", "object"}, {"additionalProperties", true}}},
             {"settings_snapshot", {{"type", "object"}, {"additionalProperties", true}}},
             {"limit", {{"type", "integer"}, {"minimum", 0}}},
             {"target_count", {{"type", "integer"}, {"minimum", 0}}},
             {"processed", {{"type", "integer"}, {"minimum", 0}}},
             {"success", {{"type", "integer"}, {"minimum", 0}}},
             {"failed", {{"type", "integer"}, {"minimum", 0}}},
             {"cancel_requested", {{"type", "boolean"}}},
             {"last_error", {{"type", "string"}}},
             {"display_name", {{"type", "string"}, {"description", "Human-readable batch label derived from the first target when available."}}},
             {"target_summary", {{"type", "string"}, {"description", "Aggregated target names, ids and failure snippets for list display."}}},
             {"target_keywords", {{"type", "string"}, {"description", "Aggregated target searchable text returned for frontend search context."}}},
             {"error_category", {{"type", "string"}, {"description", "Comma-separated distinct item error categories in this batch."}}},
             {"result_code", {{"type", "string"}, {"description", "Comma-separated distinct item result codes in this batch."}}},
             {"created_at", {{"type", "integer"}, {"minimum", 0}}},
             {"started_at", {{"nullable", true}, {"type", "integer"}, {"minimum", 0}}},
             {"finished_at", {{"nullable", true}, {"type", "integer"}, {"minimum", 0}}},
             {"updated_at", {{"type", "integer"}, {"minimum", 0}}},
             {"archived_at", {{"nullable", true}, {"type", "integer"}, {"minimum", 0}}},
             {"archived_reason", {{"nullable", true}, {"type", "string"}}},
             {"parent_batch_id", {{"nullable", true}, {"type", "string"}}},
             {"duration_seconds", {{"type", "integer"}, {"minimum", 0}}},
         }},
    };
    schemas["BatchTaskPage"] = {
        {"type", "object"},
        {"required", json::array({"items", "page", "page_size", "total"})},
        {"properties",
         {
             {"items", {{"type", "array"}, {"items", ref("#/components/schemas/BatchTask")}}},
             {"page", {{"type", "integer"}, {"minimum", 1}}},
             {"page_size", {{"type", "integer"}, {"minimum", 1}}},
             {"total", {{"type", "integer"}, {"minimum", 0}}},
         }},
    };
    schemas["BatchTaskItem"] = {
        {"type", "object"},
        {"required", json::array({"batch_id", "item_index", "item_key", "item_type", "state"})},
        {"properties",
         {
             {"batch_id", {{"type", "string"}}},
             {"item_index", {{"type", "integer"}, {"minimum", 0}}},
             {"item_key", {{"type", "string"}}},
             {"item_type", {{"type", "string"}}},
             {"item_label", {{"type", "string"}}},
             {"source", {{"type", "string"}, {"example", "migu"}}},
             {"source_singer_id", {{"type", "string"}}},
             {"source_album_id", {{"type", "string"}}},
             {"source_song_id", {{"type", "string"}}},
             {"state", {{"type", "string"}, {"enum", json::array({"Pending", "Running", "Completed", "Failed", "Cancelled"})}}},
             {"retry_count", {{"type", "integer"}, {"minimum", 0}}},
             {"written_count", {{"type", "integer"}, {"minimum", 0}}},
             {"error_category", {{"type", "string"}, {"enum", json::array({"network", "parse", "persistence", "copyright", "resource", "invalid_target", "timeout", "unknown", ""})}}},
             {"result_code", {{"type", "string"}, {"enum", json::array({"ok", "network_failed", "parse_failed", "persist_failed", "invalid_target", "timeout", "partial_completed", "failed", ""})}}},
             {"started_at", {{"nullable", true}, {"type", "integer"}, {"minimum", 0}}},
             {"finished_at", {{"nullable", true}, {"type", "integer"}, {"minimum", 0}}},
             {"last_error", {{"type", "string"}}},
             {"payload", {{"type", "object"}, {"additionalProperties", true}}},
             {"created_at", {{"type", "integer"}, {"minimum", 0}}},
             {"updated_at", {{"type", "integer"}, {"minimum", 0}}},
         }},
    };
    schemas["BatchTaskItemPage"] = {
        {"type", "object"},
        {"required", json::array({"items", "page", "page_size", "total"})},
        {"properties",
         {
             {"items", {{"type", "array"}, {"items", ref("#/components/schemas/BatchTaskItem")}}},
             {"page", {{"type", "integer"}, {"minimum", 1}}},
             {"page_size", {{"type", "integer"}, {"minimum", 1}}},
             {"total", {{"type", "integer"}, {"minimum", 0}}},
         }},
    };
    schemas["BatchTaskEvent"] = {
        {"type", "object"},
        {"required", json::array({"batch_id", "event_type", "message", "created_at"})},
        {"properties",
         {
             {"batch_id", {{"type", "string"}}},
             {"item_index", {{"nullable", true}, {"type", "integer"}, {"minimum", 0}}},
             {"event_type", {{"type", "string"}}},
             {"message", {{"type", "string"}}},
             {"payload", {{"type", "object"}, {"additionalProperties", true}}},
             {"created_at", {{"type", "integer"}, {"minimum", 0}}},
         }},
    };
    schemas["BatchTaskEventPage"] = {
        {"type", "object"},
        {"required", json::array({"items", "page", "page_size", "total"})},
        {"properties",
         {
             {"items", {{"type", "array"}, {"items", ref("#/components/schemas/BatchTaskEvent")}}},
             {"page", {{"type", "integer"}, {"minimum", 1}}},
             {"page_size", {{"type", "integer"}, {"minimum", 1}}},
             {"total", {{"type", "integer"}, {"minimum", 0}}},
         }},
    };
    schemas["DistributionEntry"] = {
        {"type", "object"},
        {"required", json::array({"key", "count"})},
        {"properties", {{"key", {{"type", "string"}}}, {"count", {{"type", "integer"}, {"minimum", 0}}}}},
    };
    schemas["TaskSummary"] = {
        {"type", "object"},
        {"required", json::array({"state_distribution", "failure_task_types", "running_count", "generated_at"})},
        {"properties",
         {
             {"state_distribution", {{"type", "array"}, {"items", ref("#/components/schemas/DistributionEntry")}}},
             {"failure_task_types", {{"type", "array"}, {"items", ref("#/components/schemas/DistributionEntry")}}},
             {"latest_success", {{"nullable", true}, {"oneOf", json::array({ref("#/components/schemas/BatchTask")})}}},
             {"latest_failed", {{"nullable", true}, {"oneOf", json::array({ref("#/components/schemas/BatchTask")})}}},
             {"running_count", {{"type", "integer"}, {"minimum", 0}}},
             {"oldest_stale_running_at", {{"nullable", true}, {"type", "integer"}, {"minimum", 0}}},
             {"generated_at", {{"type", "integer"}, {"minimum", 0}}},
         }},
    };
    schemas["BatchGovernanceRequest"] = {
        {"type", "object"},
        {"description", "Create a new batch from previous batch items, or archive the old batch. Old batches are never rerun in place."},
        {"properties",
         {
             {"mode", {{"type", "string"}, {"enum", json::array({"all", "failed"})}, {"default", "failed"}}},
             {"state", {{"oneOf", json::array({json {{"type", "string"}}, json {{"type", "array"}, {"items", {{"type", "string"}}}}})}}},
             {"states", {{"type", "array"}, {"items", {{"type", "string"}}}}},
             {"error_category", {{"type", "string"}, {"enum", json::array({"network", "parse", "persistence", "copyright", "resource", "invalid_target", "timeout", "unknown", ""})}}},
             {"result_code", {{"type", "string"}, {"enum", json::array({"ok", "network_failed", "parse_failed", "persist_failed", "invalid_target", "timeout", "partial_completed", "failed", ""})}}},
             {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 1000}, {"default", 50}}},
             {"limit_per_target", {{"type", "integer"}, {"minimum", 1}, {"maximum", 500}, {"default", 50}}},
             {"reason", {{"type", "string"}, {"maxLength", 512}}},
         }},
    };
    schemas["BatchGovernanceItem"] = {
        {"type", "object"},
        {"required", json::array({"source_id", "name", "status", "reason"})},
        {"properties",
         {
             {"source_id", {{"type", "string"}}},
             {"name", {{"type", "string"}}},
             {"status", {{"type", "string"}}},
             {"reason", {{"type", "string"}}},
         }},
    };
    schemas["BatchGovernanceResponse"] = {
        {"type", "object"},
        {"required", json::array({"batch_id", "claimed_count", "success_count", "failed_count", "skipped_count", "items"})},
        {"properties",
         {
             {"batch_id", {{"nullable", true}, {"type", "string"}}},
             {"claimed_count", {{"type", "integer"}, {"minimum", 0}}},
             {"success_count", {{"type", "integer"}, {"minimum", 0}}},
             {"failed_count", {{"type", "integer"}, {"minimum", 0}}},
             {"skipped_count", {{"type", "integer"}, {"minimum", 0}}},
             {"items", {{"type", "array"}, {"items", ref("#/components/schemas/BatchGovernanceItem")}}},
         }},
    };
    schemas["BatchArchiveResponse"] = {
        {"type", "object"},
        {"required", json::array({"batch_id", "archived"})},
        {"properties", {{"batch_id", {{"type", "string"}}}, {"archived", {{"type", "boolean"}}}}},
    };
    schemas["StatusDistribution"] = {
        {"type", "object"},
        {"additionalProperties", {{"type", "integer"}, {"minimum", 0}}},
        {"description", "Status-code keyed count map. Sync states use 0 pending, 1 running, 2 completed, 3 failed, 4 partial. Resource states use 0 pending, 1 running, 2 completed, 3 failed, 4 skipped."},
    };
    schemas["SyncStatus"] = {
        {"type", "integer"},
        {"enum", json::array({0, 1, 2, 3, 4})},
        {"description", "Metadata sync status: 0 pending, 1 running, 2 completed, 3 failed, 4 partial."},
    };
    schemas["Overview"] = {
        {"type", "object"},
        {"required", json::array({"singers", "singer_sources", "songs", "song_sources", "albums", "album_sources", "singer_song_relations", "singer_album_relations", "album_song_relations", "album_detail_sync", "album_song_sync", "resource_status"})},
        {"properties",
         {
             {"singers", {{"type", "integer"}, {"minimum", 0}}},
             {"singer_sources", {{"type", "integer"}, {"minimum", 0}}},
             {"songs", {{"type", "integer"}, {"minimum", 0}}},
             {"song_sources", {{"type", "integer"}, {"minimum", 0}}},
             {"albums", {{"type", "integer"}, {"minimum", 0}}},
             {"album_sources", {{"type", "integer"}, {"minimum", 0}}},
             {"singer_song_relations", {{"type", "integer"}, {"minimum", 0}, {"description", "Total singer-song relation rows derived from song_artist_refs for Migu song sources."}}},
             {"singer_album_relations", {{"type", "integer"}, {"minimum", 0}, {"description", "Total singer-album relations derived from album_source_refs.source_artist_id."}}},
             {"album_song_relations", {{"type", "integer"}, {"minimum", 0}, {"description", "Total album-song relation rows derived from album_song_refs."}}},
             {"singer_song_sync", ref("#/components/schemas/StatusDistribution")},
             {"singer_album_sync", ref("#/components/schemas/StatusDistribution")},
             {"album_detail_sync", ref("#/components/schemas/StatusDistribution")},
             {"album_song_sync", ref("#/components/schemas/StatusDistribution")},
             {"resource_status", ref("#/components/schemas/StatusDistribution")},
         }},
    };
    schemas["CatalogPage"] = {
        {"type", "object"},
        {"required", json::array({"items", "page", "page_size", "total"})},
        {"properties",
         {
             {"items", {{"type", "array"}, {"items", {{"type", "object"}, {"additionalProperties", true}}}}},
             {"page", {{"type", "integer"}, {"minimum", 1}}},
             {"page_size", {{"type", "integer"}, {"minimum", 1}, {"maximum", 200}}},
             {"total", {{"type", "integer"}, {"minimum", 0}}},
         }},
    };
    schemas["SingerRecord"] = {
        {"type", "object"},
        {"required", json::array({"source", "source_singer_id", "name", "songs_sync_status", "albums_sync_status"})},
        {"properties",
         {
             {"singer_db_id", {{"type", "integer"}, {"minimum", 0}}},
             {"name", {{"type", "string"}}},
             {"normalized_name", {{"type", "string"}}},
             {"gender", {{"type", "integer"}, {"minimum", 0}, {"maximum", 3}}},
             {"status", {{"type", "integer"}}},
             {"source_ref_id", {{"type", "integer"}, {"minimum", 0}}},
             {"source", {{"type", "string"}, {"enum", json::array({"migu"})}}},
             {"source_item_key", {{"type", "string"}}},
             {"source_singer_id", {{"type", "string"}}},
             {"source_name_pinyin", {{"type", "string"}}},
             {"country", {{"type", "string"}}},
             {"source_tag", {{"type", "string"}}},
             {"source_metric", {{"type", "string"}}},
             {"avatar_url", {{"type", "string"}}},
             {"songs_sync_status", describedRef("#/components/schemas/SyncStatus", "Exposed singer-song sync status. It is derived from real song_artist_refs: when the singer has related songs and no sync error, pending or partial stored state is exposed as completed (2); running (1) remains running.")},
             {"songs_synced_at", {{"nullable", true}, {"type", "integer"}, {"minimum", 0}, {"description", "Unix timestamp of the last singer-song sync or summary repair."}}},
             {"songs_count", {{"type", "integer"}, {"minimum", 0}, {"description", "Alias of songs_sync_count for frontend count displays."}}},
             {"songs_sync_count", {{"type", "integer"}, {"minimum", 0}, {"description", "Distinct related song count derived from song_artist_refs for this Migu source_singer_id."}}},
             {"songs_sync_error", {{"type", "string"}, {"description", "Last singer-song sync error. A non-empty error prevents real-content summary repair from exposing the status as completed."}}},
             {"songs_last_batch_id", {{"nullable", true}, {"type", "string"}}},
             {"songs_running_batch_id", {{"nullable", true}, {"type", "string"}}},
             {"albums_sync_status", describedRef("#/components/schemas/SyncStatus", "Exposed singer-album sync status. It is derived from album_source_refs.source_artist_id: when the singer has related albums and no sync error, pending or partial stored state is exposed as completed (2); running (1) remains running.")},
             {"albums_synced_at", {{"nullable", true}, {"type", "integer"}, {"minimum", 0}, {"description", "Unix timestamp of the last singer-album sync or summary repair."}}},
             {"albums_count", {{"type", "integer"}, {"minimum", 0}, {"description", "Alias of albums_sync_count for frontend count displays."}}},
             {"albums_sync_count", {{"type", "integer"}, {"minimum", 0}, {"description", "Related album count derived from album_source_refs.source_artist_id for this Migu source_singer_id."}}},
             {"albums_sync_error", {{"type", "string"}, {"description", "Last singer-album sync error. A non-empty error prevents real-content summary repair from exposing the status as completed."}}},
             {"albums_last_batch_id", {{"nullable", true}, {"type", "string"}}},
             {"albums_running_batch_id", {{"nullable", true}, {"type", "string"}}},
             {"created_at", {{"type", "integer"}, {"minimum", 0}}},
             {"updated_at", {{"type", "integer"}, {"minimum", 0}}},
         }},
    };
    schemas["AlbumRecord"] = {
        {"type", "object"},
        {"required", json::array({"source", "source_album_id", "source_title", "detail_sync_status", "songs_sync_status"})},
        {"properties",
         {
             {"album_db_id", {{"type", "integer"}, {"minimum", 0}}},
             {"title", {{"type", "string"}}},
             {"normalized_title", {{"type", "string"}}},
             {"primary_artist_name", {{"type", "string"}}},
             {"release_date", {{"type", "string"}}},
             {"source_ref_id", {{"type", "integer"}, {"minimum", 0}}},
             {"source", {{"type", "string"}, {"enum", json::array({"migu"})}}},
             {"source_item_key", {{"type", "string"}}},
             {"source_album_id", {{"type", "string"}}},
             {"source_resource_type", {{"type", "string"}}},
             {"source_res_type", {{"type", "string"}}},
             {"source_album_kind", {{"type", "integer"}, {"minimum", 0}}},
             {"source_title", {{"type", "string"}}},
             {"source_artist_id", {{"type", "string"}}},
             {"source_artist_name", {{"type", "string"}}},
             {"cover_url", {{"type", "string"}}},
             {"total_count", {{"type", "string"}}},
             {"publish_date", {{"type", "string"}}},
             {"play_num", {{"type", "integer"}, {"minimum", 0}}},
             {"play_num_desc", {{"type", "string"}}},
             {"detail_sync_status", ref("#/components/schemas/SyncStatus")},
             {"detail_synced_at", {{"nullable", true}, {"type", "integer"}, {"minimum", 0}}},
             {"detail_sync_error", {{"type", "string"}}},
             {"detail_last_batch_id", {{"nullable", true}, {"type", "string"}}},
             {"detail_running_batch_id", {{"nullable", true}, {"type", "string"}}},
             {"songs_sync_status", ref("#/components/schemas/SyncStatus")},
             {"songs_synced_at", {{"nullable", true}, {"type", "integer"}, {"minimum", 0}}},
             {"songs_count", {{"type", "integer"}, {"minimum", 0}, {"description", "Alias of album_songs_count for frontend count displays."}}},
             {"album_songs_count", {{"type", "integer"}, {"minimum", 0}, {"description", "Distinct related song count derived from album_song_refs for this source_album_id."}}},
             {"songs_sync_count", {{"type", "integer"}, {"minimum", 0}}},
             {"songs_sync_error", {{"type", "string"}}},
             {"songs_last_batch_id", {{"nullable", true}, {"type", "string"}}},
             {"songs_running_batch_id", {{"nullable", true}, {"type", "string"}}},
             {"created_at", {{"type", "integer"}, {"minimum", 0}}},
             {"updated_at", {{"type", "integer"}, {"minimum", 0}}},
         }},
    };
    schemas["SongRecord"] = {
        {"type", "object"},
        {"required", json::array({"source", "source_song_id", "title"})},
        {"properties",
         {
             {"song_db_id", {{"type", "integer"}, {"minimum", 0}}},
             {"title", {{"type", "string"}}},
             {"normalized_title", {{"type", "string"}}},
             {"source_duration_seconds", {{"type", "integer"}, {"minimum", 0}}},
             {"source_ref_id", {{"type", "integer"}, {"minimum", 0}}},
             {"source", {{"type", "string"}, {"enum", json::array({"migu"})}}},
             {"source_item_key", {{"type", "string"}}},
             {"source_song_id", {{"type", "string"}}},
             {"source_content_id", {{"type", "string"}}},
             {"source_resource_type", {{"type", "string"}}},
             {"source_title_pinyin", {{"type", "string"}}},
             {"source_album_id", {{"type", "string"}}},
             {"source_album_name", {{"type", "string"}}},
             {"primary_artist_id", {{"type", "string"}, {"description", "Primary display artist ID derived from song_artist_refs."}}},
             {"primary_artist_name", {{"type", "string"}, {"description", "Primary display artist name derived from song_artist_refs."}}},
             {"cover_url", {{"type", "string"}}},
             {"lyric_url", {{"type", "string"}}},
             {"mrc_url", {{"type", "string"}}},
             {"trc_url", {{"type", "string"}}},
             {"copyright_id", {{"type", "string"}}},
             {"metadata_status", {{"type", "integer"}}},
             {"format_types", {{"type", "string"}}},
             {"resource_count", {{"type", "integer"}, {"minimum", 0}}},
             {"failed_resource_count", {{"type", "integer"}, {"minimum", 0}}},
             {"created_at", {{"type", "integer"}, {"minimum", 0}}},
             {"updated_at", {{"type", "integer"}, {"minimum", 0}}},
         }},
    };
    schemas["ResourceQueueRecord"] = {
        {"type", "object"},
        {"required", json::array({"resource_id", "source", "source_song_id", "resource_kind", "format_type", "status"})},
        {"properties",
         {
             {"resource_id", {{"type", "integer"}, {"minimum", 0}}},
             {"song_source_ref_id", {{"type", "integer"}, {"minimum", 0}}},
             {"source", {{"type", "string"}, {"enum", json::array({"migu"})}}},
             {"source_song_id", {{"type", "string"}}},
             {"source_content_id", {{"type", "string"}}},
             {"title", {{"type", "string"}}},
             {"artist", {{"type", "string"}}},
             {"album", {{"type", "string"}}},
             {"resource_kind", {{"type", "string"}}},
             {"format_type", {{"type", "string"}}},
             {"remote_url", {{"type", "string"}}},
             {"local_path", {{"type", "string"}}},
             {"size_bytes", {{"type", "integer"}, {"minimum", 0}}},
             {"checksum", {{"type", "string"}}},
             {"status", {{"type", "integer"}, {"minimum", 0}, {"maximum", 4}}},
             {"retry_count", {{"type", "integer"}, {"minimum", 0}}},
             {"last_error", {{"type", "string"}}},
             {"error_category", {{"type", "string"}, {"enum", json::array({"network", "parse", "persistence", "copyright", "resource", "invalid_target", "timeout", "unknown", ""})}}},
             {"result_code", {{"type", "string"}, {"enum", json::array({"ok", "network_failed", "parse_failed", "persist_failed", "invalid_target", "timeout", "partial_completed", "failed", ""})}}},
             {"last_batch_id", {{"nullable", true}, {"type", "string"}}},
             {"running_batch_id", {{"nullable", true}, {"type", "string"}}},
             {"last_processed_at", {{"nullable", true}, {"type", "integer"}, {"minimum", 0}}},
             {"fetched_at", {{"nullable", true}, {"type", "integer"}, {"minimum", 0}}},
             {"created_at", {{"type", "integer"}, {"minimum", 0}}},
             {"updated_at", {{"type", "integer"}, {"minimum", 0}}},
         }},
    };
    schemas["SingerRecordPage"] = {
        {"type", "object"},
        {"required", json::array({"items", "page", "page_size", "total"})},
        {"properties", {{"items", {{"type", "array"}, {"items", ref("#/components/schemas/SingerRecord")}}}, {"page", {{"type", "integer"}, {"minimum", 1}}}, {"page_size", {{"type", "integer"}, {"minimum", 1}, {"maximum", 200}}}, {"total", {{"type", "integer"}, {"minimum", 0}}}}},
    };
    schemas["AlbumRecordPage"] = {
        {"type", "object"},
        {"required", json::array({"items", "page", "page_size", "total"})},
        {"properties", {{"items", {{"type", "array"}, {"items", ref("#/components/schemas/AlbumRecord")}}}, {"page", {{"type", "integer"}, {"minimum", 1}}}, {"page_size", {{"type", "integer"}, {"minimum", 1}, {"maximum", 200}}}, {"total", {{"type", "integer"}, {"minimum", 0}}}}},
    };
    schemas["SongRecordPage"] = {
        {"type", "object"},
        {"required", json::array({"items", "page", "page_size", "total"})},
        {"properties", {{"items", {{"type", "array"}, {"items", ref("#/components/schemas/SongRecord")}}}, {"page", {{"type", "integer"}, {"minimum", 1}}}, {"page_size", {{"type", "integer"}, {"minimum", 1}, {"maximum", 200}}}, {"total", {{"type", "integer"}, {"minimum", 0}}}}},
    };
    schemas["ResourceQueueRecordPage"] = {
        {"type", "object"},
        {"required", json::array({"items", "page", "page_size", "total"})},
        {"properties", {{"items", {{"type", "array"}, {"items", ref("#/components/schemas/ResourceQueueRecord")}}}, {"page", {{"type", "integer"}, {"minimum", 1}}}, {"page_size", {{"type", "integer"}, {"minimum", 1}, {"maximum", 200}}}, {"total", {{"type", "integer"}, {"minimum", 0}}}}},
    };
    schemas["ResourceActionRequest"] = {
        {"type", "object"},
        {"description", "Govern resource queue records only. It does not fetch, probe or download resource URLs."},
        {"properties",
         {
             {"source", {{"type", "string"}, {"enum", json::array({"migu"})}, {"default", "migu"}}},
             {"action", {{"type", "string"}, {"enum", json::array({"retry", "reset", "recover-stale"})}, {"description", "Required by /api/migu/resources/preview. Ignored by concrete retry/reset/recover endpoints."}}},
             {"resource_ids", {{"type", "array"}, {"items", {{"type", "integer"}, {"minimum", 1}}}}},
             {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 1000}, {"default", 50}}},
             {"stale_after_seconds", {{"type", "integer"}, {"minimum", 1}, {"default", 3600}}},
             {"filter", {{"type", "object"}, {"additionalProperties", true}, {"description", "Supports status/resource_status, q, kind and format."}}},
         }},
    };
    schemas["ResourceProcessRequest"] = {
        {"type", "object"},
        {"description", "Claims pending Migu resource records, fetches the resource URL when needed, downloads content, stores bytes and writes back status. By default it claims status 0/3 across all resource kinds; filter.kind/resource_kind can limit processing to audio, cover, lyric, mrc or trc. Use async=true for long-running batches. Audio resources use the runtime pacmtoken stored through POST /api/migu/pacmtoken/refresh and may auto-refresh it during processing; tokens are not written to batch tables or logs."},
        {"properties",
         {
             {"source", {{"type", "string"}, {"enum", json::array({"migu"})}, {"default", "migu"}}},
             {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 1000}, {"default", 20}}},
             {"offset", {{"type", "integer"}, {"minimum", 0}, {"default", 0}}},
             {"async", {{"type", "boolean"}, {"default", false}, {"description", "When true, returns 202 with batch_id and processes resources in the background."}}},
             {"batch_id", {{"type", "string"}, {"description", "Optional caller-provided batch id; generated when omitted."}}},
             {"filter", {{"type", "object"}, {"additionalProperties", true}, {"description", "Supports status/resource_status/resourceStatus/statuses, kind/resource_kind/resourceKind, format/format_type/formatType and content_id/contentId/source_content_id/sourceContentId. Omitted status defaults to 0 and 3. Omitted kind does not filter resource kinds; valid kinds are audio, cover, lyric, mrc and trc."}}},
             {"options", ref("#/components/schemas/ResourceProcessOptions")},
         }},
    };
    schemas["ResourceProcessOptions"] = {
        {"type", "object"},
        {"description", "Per-request resource download options. Omitted fields use GET /api/migu/settings mutable defaults."},
        {"properties",
         {
             {"resource_download_concurrency", {{"type", "integer"}, {"minimum", 1}, {"maximum", 32}, {"default", 3}}},
             {"resourceDownloadConcurrency", {{"type", "integer"}, {"minimum", 1}, {"maximum", 32}}},
             {"resource_download_retries", {{"type", "integer"}, {"minimum", 0}, {"maximum", 5}, {"default", 1}}},
             {"resourceDownloadRetries", {{"type", "integer"}, {"minimum", 0}, {"maximum", 5}}},
             {"resource_download_timeout_ms", {{"type", "integer"}, {"minimum", 10000}, {"maximum", 600000}, {"default", 120000}}},
             {"resourceDownloadTimeoutMs", {{"type", "integer"}, {"minimum", 10000}, {"maximum", 600000}}},
             {"pacmtoken_refresh_interval_seconds", {{"type", "integer"}, {"minimum", 0}, {"maximum", 3600}, {"default", 300}, {"description", "0 disables periodic long-run refresh."}}},
             {"pacmtokenRefreshIntervalSeconds", {{"type", "integer"}, {"minimum", 0}, {"maximum", 3600}}},
             {"auto_refresh_pacmtoken", {{"type", "boolean"}, {"default", true}}},
             {"autoRefreshPacmtoken", {{"type", "boolean"}}},
             {"refresh_pacmtoken_on_440013", {{"type", "boolean"}, {"default", true}}},
             {"refreshPacmtokenOn440013", {{"type", "boolean"}}},
         }},
    };
    schemas["ResourceProcessAcceptedResponse"] = {
        {"type", "object"},
        {"required", json::array({"accepted", "async", "batch_id", "task_url", "items_url", "events_url"})},
        {"properties",
         {
             {"accepted", {{"type", "boolean"}}},
             {"async", {{"type", "boolean"}}},
             {"batch_id", {{"type", "string"}}},
             {"task_url", {{"type", "string"}}},
             {"items_url", {{"type", "string"}}},
             {"events_url", {{"type", "string"}}},
         }},
    };
    schemas["ResourceProcessItem"] = {
        {"type", "object"},
        {"required", json::array({"resource_id", "resource_kind", "format_type", "ok", "skipped", "related", "synthetic", "local_path", "size_bytes", "checksum", "error_category", "result_code", "message"})},
        {"properties",
         {
             {"resource_id", {{"type", "integer"}, {"minimum", 0}}},
             {"song_source_ref_id", {{"type", "integer"}, {"minimum", 0}}},
             {"source", {{"type", "string"}}},
             {"source_song_id", {{"type", "string"}}},
             {"source_content_id", {{"type", "string"}}},
             {"title", {{"type", "string"}}},
             {"resource_kind", {{"type", "string"}}},
             {"format_type", {{"type", "string"}}},
             {"ok", {{"type", "boolean"}}},
             {"skipped", {{"type", "boolean"}}},
             {"related", {{"type", "boolean"}, {"description", "True when the item was discovered from an audio response lyric/mrc/trc URL."}}},
             {"synthetic", {{"type", "boolean"}, {"description", "True when no real resource row could be claimed, for example related_not_claimed."}}},
             {"local_path", {{"type", "string"}, {"description", "Storage path returned by the blob store after save. Empty when the item did not save content."}}},
             {"size_bytes", {{"type", "integer"}, {"minimum", 0}}},
             {"checksum", {{"type", "string"}}},
             {"error_category", {{"type", "string"}, {"description", "Failure or skip category. Empty when ok is true."}}},
             {"result_code", {{"type", "string"}}},
             {"message", {{"type", "string"}}},
         }},
    };
    schemas["ResourceProcessResponse"] = {
        {"type", "object"},
        {"required", json::array({"batch_id", "claimed_count", "succeeded_count", "failed_count", "skipped_count", "related_succeeded_count", "related_failed_count", "related_skipped_count", "synthetic_related_skipped_count", "items"})},
        {"properties",
         {
             {"batch_id", {{"type", "string"}}},
             {"claimed_count", {{"type", "integer"}, {"minimum", 0}}},
             {"succeeded_count", {{"type", "integer"}, {"minimum", 0}}},
             {"failed_count", {{"type", "integer"}, {"minimum", 0}}},
             {"skipped_count", {{"type", "integer"}, {"minimum", 0}}},
             {"related_succeeded_count", {{"type", "integer"}, {"minimum", 0}}},
             {"related_failed_count", {{"type", "integer"}, {"minimum", 0}}},
             {"related_skipped_count", {{"type", "integer"}, {"minimum", 0}}},
             {"synthetic_related_skipped_count", {{"type", "integer"}, {"minimum", 0}}},
             {"items", {{"type", "array"}, {"items", ref("#/components/schemas/ResourceProcessItem")}}},
         }},
    };
    schemas["ResourceActionItem"] = {
        {"type", "object"},
        {"required", json::array({"resource_id", "source_id", "name", "status", "reason"})},
        {"properties",
         {
             {"resource_id", {{"type", "integer"}, {"minimum", 0}}},
             {"source_id", {{"type", "string"}}},
             {"name", {{"type", "string"}}},
             {"status", {{"type", "integer"}, {"minimum", 0}, {"maximum", 4}}},
             {"reason", {{"type", "string"}}},
         }},
    };
    schemas["ResourceActionResponse"] = {
        {"type", "object"},
        {"required", json::array({"action", "updated_count", "success_count", "failed_count", "skipped_count", "items"})},
        {"properties",
         {
             {"action", {{"type", "string"}, {"enum", json::array({"retry", "reset", "recover-stale"})}}},
             {"updated_count", {{"type", "integer"}, {"minimum", 0}}},
             {"success_count", {{"type", "integer"}, {"minimum", 0}}},
             {"failed_count", {{"type", "integer"}, {"minimum", 0}}},
             {"skipped_count", {{"type", "integer"}, {"minimum", 0}}},
             {"items", {{"type", "array"}, {"items", ref("#/components/schemas/ResourceActionItem")}}},
         }},
    };
    schemas["ResourcePreviewItem"] = {
        {"type", "object"},
        {"required", json::array({"resource_id", "target_id", "target_name", "resource_kind", "format_type", "sync_status", "eligible", "reason"})},
        {"properties",
         {
             {"resource_id", {{"type", "integer"}, {"minimum", 0}}},
             {"target_id", {{"type", "string"}}},
             {"target_name", {{"type", "string"}}},
             {"resource_kind", {{"type", "string"}}},
             {"format_type", {{"type", "string"}}},
             {"sync_status", {{"type", "integer"}, {"minimum", 0}, {"maximum", 4}}},
             {"eligible", {{"type", "boolean"}}},
             {"reason", {{"type", "string"}}},
         }},
    };
    schemas["ResourcePreviewResponse"] = {
        {"type", "object"},
        {"required", json::array({"source", "action", "matched_count", "eligible_count", "ineligible_count", "missing_count", "items"})},
        {"properties",
         {
             {"source", {{"type", "string"}, {"enum", json::array({"migu"})}}},
             {"action", {{"type", "string"}, {"enum", json::array({"retry", "reset", "recover-stale"})}}},
             {"matched_count", {{"type", "integer"}, {"minimum", 0}}},
             {"eligible_count", {{"type", "integer"}, {"minimum", 0}}},
             {"ineligible_count", {{"type", "integer"}, {"minimum", 0}}},
             {"missing_count", {{"type", "integer"}, {"minimum", 0}}},
             {"items", {{"type", "array"}, {"items", ref("#/components/schemas/ResourcePreviewItem")}}},
         }},
    };
    schemas["BackfillActionRequest"] = {
        {"type", "object"},
        {"description", "Retry or reset selected backfill targets. source_ids can be singer IDs or album IDs depending on endpoint. Empty filter fields do not constrain the query."},
        {"properties",
         {
             {"source", {{"type", "string"}, {"enum", json::array({"migu"})}, {"default", "migu"}}},
             {"source_ids", {{"type", "array"}, {"items", ref("#/components/schemas/MiguId")}}},
             {"source_singer_ids", {{"type", "array"}, {"items", ref("#/components/schemas/MiguId")}}},
             {"source_album_ids", {{"type", "array"}, {"items", ref("#/components/schemas/MiguId")}}},
             {"limit", {{"type", "integer"}, {"minimum", 1}, {"maximum", 1000}, {"default", 50}}},
             {"limit_per_target", {{"type", "integer"}, {"minimum", 1}, {"maximum", 500}, {"default", 20}}},
             {"filter", {{"type", "object"}, {"additionalProperties", true}, {"description", "Supports status/sync_status, q, country, gender, artist and kind."}}},
         }},
    };
    schemas["BackfillActionItem"] = {
        {"type", "object"},
        {"required", json::array({"source_id", "name", "status", "reason"})},
        {"properties",
         {
             {"source_id", {{"type", "string"}}},
             {"name", {{"type", "string"}}},
             {"status", {{"nullable", true}, {"type", "integer"}, {"minimum", 0}, {"maximum", 4}}},
             {"reason", {{"type", "string"}, {"description", "claimed, skipped, reset, not_found, completed, running, status_not_selected and related stable reasons."}}},
         }},
    };
    schemas["BackfillQueueTarget"] = {
        {"type", "object"},
        {"required", json::array({"target_id", "target_type", "target_name", "owner_name", "sync_status", "sync_count", "eligible_actions", "eligible", "reason"})},
        {"properties",
         {
             {"target_id", {{"type", "string"}}},
             {"target_type", {{"type", "string"}, {"enum", json::array({"singer", "album"})}}},
             {"target_name", {{"type", "string"}}},
             {"owner_name", {{"type", "string"}}},
             {"source", {{"type", "string"}, {"enum", json::array({"migu"})}}},
             {"sync_status", {{"nullable", true}, {"type", "integer"}, {"minimum", 0}, {"maximum", 4}, {"description", "Effective metadata sync status for this queue type. Singer-song and singer-album queues use the same real-relation derived status as SingerRecord, so real content with no sync error is completed (2) instead of pending (0)."}}},
             {"sync_count", {{"type", "integer"}, {"minimum", 0}, {"description", "Effective synced item count for this queue type. Singer-song queues derive it from song_artist_refs; singer-album queues derive it from album_source_refs.source_artist_id."}}},
             {"last_error", {{"type", "string"}}},
             {"last_batch_id", {{"nullable", true}, {"type", "string"}}},
             {"running_batch_id", {{"nullable", true}, {"type", "string"}}},
             {"last_processed_at", {{"nullable", true}, {"type", "integer"}, {"minimum", 0}}},
             {"eligible_actions", {{"type", "array"}, {"items", {{"type", "string"}, {"enum", json::array({"retry", "reset", "recover-stale"})}}}}},
             {"eligible", {{"type", "boolean"}, {"description", "Whether the target is claimable under the requested status filter after applying the effective real-relation summary."}}},
             {"reason", {{"type", "string"}, {"description", "Eligibility reason such as pending, failed, partial, running, completed, not_found or status_not_selected."}}},
         }},
    };
    schemas["BackfillQueueTargetPage"] = {
        {"type", "object"},
        {"required", json::array({"items", "page", "page_size", "total"})},
        {"properties", {{"items", {{"type", "array"}, {"items", ref("#/components/schemas/BackfillQueueTarget")}}}, {"page", {{"type", "integer"}, {"minimum", 1}}}, {"page_size", {{"type", "integer"}, {"minimum", 1}, {"maximum", 200}}}, {"total", {{"type", "integer"}, {"minimum", 0}}}}},
    };
    schemas["BackfillPreviewResponse"] = {
        {"type", "object"},
        {"required", json::array({"type", "source", "matched_count", "eligible_count", "ineligible_count", "missing_count", "sample_size", "items"})},
        {"properties",
         {
             {"type", {{"type", "string"}, {"enum", json::array({"singer-songs", "singer-albums", "album-details", "album-songs"})}}},
             {"source", {{"type", "string"}, {"enum", json::array({"migu"})}}},
             {"matched_count", {{"type", "integer"}, {"minimum", 0}}},
             {"eligible_count", {{"type", "integer"}, {"minimum", 0}}},
             {"ineligible_count", {{"type", "integer"}, {"minimum", 0}}},
             {"missing_count", {{"type", "integer"}, {"minimum", 0}}},
             {"sample_size", {{"type", "integer"}, {"minimum", 1}, {"maximum", 20}}},
             {"items", {{"type", "array"}, {"items", ref("#/components/schemas/BackfillQueueTarget")}}},
         }},
    };
    schemas["BackfillResetResponse"] = {
        {"type", "object"},
        {"required", json::array({"type", "updated_count", "success_count", "failed_count", "skipped_count", "items"})},
        {"properties",
         {
             {"type", {{"type", "string"}}},
             {"updated_count", {{"type", "integer"}, {"minimum", 0}}},
             {"success_count", {{"type", "integer"}, {"minimum", 0}}},
             {"failed_count", {{"type", "integer"}, {"minimum", 0}}},
             {"skipped_count", {{"type", "integer"}, {"minimum", 0}}},
             {"items", {{"type", "array"}, {"items", ref("#/components/schemas/BackfillActionItem")}}},
         }},
    };
    schemas["DictionaryEntry"] = {
        {"type", "object"},
        {"properties", {{"value", {{"oneOf", json::array({json {{"type", "integer"}}, json {{"type", "string"}}})}}}, {"code", {{"type", "string"}}}, {"label", {{"type", "string"}}}}},
    };
    schemas["DictionaryResponse"] = {
        {"type", "object"},
        {"properties",
         {
             {"sync_status", {{"type", "array"}, {"items", ref("#/components/schemas/DictionaryEntry")}}},
             {"gender", {{"type", "array"}, {"items", ref("#/components/schemas/DictionaryEntry")}}},
             {"album_kind", {{"type", "array"}, {"items", ref("#/components/schemas/DictionaryEntry")}}},
             {"resource_kind", {{"type", "array"}, {"items", {{"type", "string"}}}}},
             {"audio_format", {{"type", "array"}, {"items", {{"type", "string"}}}}},
             {"error_category", {{"type", "array"}, {"items", {{"type", "string"}}}}},
             {"result_code", {{"type", "array"}, {"items", {{"type", "string"}}}}},
         }},
    };
    schemas["StaleRecoveryRequest"] = {
        {"type", "object"},
        {"description", "Recover metadata, resource rows or task rows stuck in running state. Empty types recovers all supported Migu queues and batches."},
        {"properties",
         {
             {"source", {{"type", "string"}, {"enum", json::array({"migu"})}, {"default", "migu"}}},
             {"stale_after_seconds", {{"type", "integer"}, {"minimum", 1}, {"default", 3600}}},
             {"types", {{"type", "array"}, {"items", {{"type", "string"}, {"enum", json::array({"singer-songs", "singer-albums", "album-details", "album-songs", "resources", "batches"})}}}}},
         }},
    };
    schemas["StaleRecoveryResponse"] = {
        {"type", "object"},
        {"required", json::array({"source", "singer_songs_recovered", "singer_albums_recovered", "album_details_recovered", "album_songs_recovered", "resources_recovered", "batches_recovered", "recovered_at"})},
        {"properties",
         {
             {"source", {{"type", "string"}, {"enum", json::array({"migu"})}}},
             {"singer_songs_recovered", {{"type", "integer"}, {"minimum", 0}}},
             {"singer_albums_recovered", {{"type", "integer"}, {"minimum", 0}}},
             {"album_details_recovered", {{"type", "integer"}, {"minimum", 0}}},
             {"album_songs_recovered", {{"type", "integer"}, {"minimum", 0}}},
             {"resources_recovered", {{"type", "integer"}, {"minimum", 0}}},
             {"batches_recovered", {{"type", "integer"}, {"minimum", 0}}},
             {"recovered_at", {{"type", "integer"}, {"minimum", 0}}},
         }},
    };
    schemas["IntegritySummary"] = {
        {"type", "object"},
        {"required", json::array({"singers_without_songs", "singers_without_albums", "albums_without_details", "albums_without_songs", "songs_without_resource_tasks", "resources_failed", "resources_running_stale", "singers_songs_running_stale", "singers_albums_running_stale", "albums_details_running_stale", "albums_songs_running_stale", "running_batches_stale", "generated_at"})},
        {"properties",
         {
             {"singers_without_songs", {{"type", "integer"}, {"minimum", 0}, {"description", "Singer sources without any real song_artist_refs relation. This is based on relationship existence, not only songs_sync_status."}}},
             {"singers_without_albums", {{"type", "integer"}, {"minimum", 0}, {"description", "Singer sources without any real album_source_refs.source_artist_id relation. This is based on relationship existence, not only albums_sync_status."}}},
             {"albums_without_details", {{"type", "integer"}, {"minimum", 0}}},
             {"albums_without_songs", {{"type", "integer"}, {"minimum", 0}}},
             {"songs_without_resource_tasks", {{"type", "integer"}, {"minimum", 0}}},
             {"resources_failed", {{"type", "integer"}, {"minimum", 0}}},
             {"resources_running_stale", {{"type", "integer"}, {"minimum", 0}}},
             {"singers_songs_running_stale", {{"type", "integer"}, {"minimum", 0}}},
             {"singers_albums_running_stale", {{"type", "integer"}, {"minimum", 0}}},
             {"albums_details_running_stale", {{"type", "integer"}, {"minimum", 0}}},
             {"albums_songs_running_stale", {{"type", "integer"}, {"minimum", 0}}},
             {"running_batches_stale", {{"type", "integer"}, {"minimum", 0}}},
             {"generated_at", {{"type", "integer"}, {"minimum", 0}}},
         }},
    };
    schemas["Integrity"] = ref("#/components/schemas/IntegritySummary");
    schemas["SchemaStatus"] = {
        {"type", "object"},
        {"required", json::array({"source", "schema_version", "latest_applied_version", "latest_available_version", "schema_name", "ok", "pending_migrations", "checksum_mismatches", "missing_tables", "missing_columns", "checked_at"})},
        {"properties",
         {
             {"source", {{"type", "string"}, {"enum", json::array({"migu"})}}},
             {"schema_version", {{"type", "integer"}, {"minimum", 0}}},
             {"latest_applied_version", {{"type", "integer"}, {"minimum", 0}}},
             {"latest_available_version", {{"type", "integer"}, {"minimum", 0}}},
             {"schema_name", {{"type", "string"}}},
             {"ok", {{"type", "boolean"}}},
             {"pending_migrations", {{"type", "array"}, {"items", {{"type", "string"}}}}},
             {"checksum_mismatches", {{"type", "array"}, {"items", {{"type", "string"}}}}},
             {"missing_tables", {{"type", "array"}, {"items", {{"type", "string"}}}}},
             {"missing_columns", {{"type", "array"}, {"items", {{"type", "string"}}}}},
             {"checked_at", {{"type", "integer"}, {"minimum", 0}}},
         }},
    };
    schemas["GovernanceEvent"] = {
        {"type", "object"},
        {"required", json::array({"event_id", "source", "action", "target_type", "target_id", "created_at"})},
        {"properties",
         {
             {"event_id", {{"type", "integer"}, {"minimum", 0}}},
             {"source", {{"type", "string"}, {"enum", json::array({"migu"})}}},
             {"action", {{"type", "string"}}},
             {"target_type", {{"type", "string"}}},
             {"target_id", {{"type", "string"}}},
             {"batch_id", {{"nullable", true}, {"type", "string"}}},
             {"before_status", {{"nullable", true}, {"type", "string"}}},
             {"after_status", {{"nullable", true}, {"type", "string"}}},
             {"result_code", {{"nullable", true}, {"type", "string"}}},
             {"error_category", {{"nullable", true}, {"type", "string"}}},
             {"reason", {{"nullable", true}, {"type", "string"}}},
             {"payload", {{"type", "object"}, {"additionalProperties", true}}},
             {"created_at", {{"type", "integer"}, {"minimum", 0}}},
         }},
    };
    schemas["GovernanceEventPage"] = {
        {"type", "object"},
        {"required", json::array({"items", "page", "page_size", "total"})},
        {"properties", {{"items", {{"type", "array"}, {"items", ref("#/components/schemas/GovernanceEvent")}}}, {"page", {{"type", "integer"}, {"minimum", 1}}}, {"page_size", {{"type", "integer"}, {"minimum", 1}, {"maximum", 200}}}, {"total", {{"type", "integer"}, {"minimum", 0}}}}},
    };
    schemas["BatchControlRequest"] = {
        {"type", "object"},
        {"required", json::array({"batch_id"})},
        {"properties", {{"batch_id", {{"type", "string"}, {"minLength", 1}, {"maxLength", 128}}}}},
    };
    schemas["SettingsUpdate"] = {
        {"type", "object"},
        {"description", "Hot-updatable scheduling settings. At least one mutable field is required."},
        {"properties",
         {
             {"max_batch_concurrency", {{"type", "integer"}, {"minimum", 1}, {"maximum", 128}}},
             {"maxBatchConcurrency", {{"type", "integer"}, {"minimum", 1}, {"maximum", 128}}},
             {"max_page_concurrency", {{"type", "integer"}, {"minimum", 1}, {"maximum", 128}}},
             {"maxPageConcurrency", {{"type", "integer"}, {"minimum", 1}, {"maximum", 128}}},
             {"max_retries", {{"type", "integer"}, {"minimum", 0}, {"maximum", 10}}},
             {"maxRetries", {{"type", "integer"}, {"minimum", 0}, {"maximum", 10}}},
             {"task_timeout_ms", {{"type", "integer"}, {"minimum", 1000}, {"maximum", 86400000}}},
             {"taskTimeoutMs", {{"type", "integer"}, {"minimum", 1000}, {"maximum", 86400000}}},
             {"resource_download_concurrency", {{"type", "integer"}, {"minimum", 1}, {"maximum", 32}}},
             {"resourceDownloadConcurrency", {{"type", "integer"}, {"minimum", 1}, {"maximum", 32}}},
             {"resource_download_retries", {{"type", "integer"}, {"minimum", 0}, {"maximum", 5}}},
             {"resourceDownloadRetries", {{"type", "integer"}, {"minimum", 0}, {"maximum", 5}}},
             {"resource_download_timeout_ms", {{"type", "integer"}, {"minimum", 10000}, {"maximum", 600000}}},
             {"resourceDownloadTimeoutMs", {{"type", "integer"}, {"minimum", 10000}, {"maximum", 600000}}},
             {"pacmtoken_refresh_interval_seconds", {{"type", "integer"}, {"minimum", 0}, {"maximum", 3600}}},
             {"pacmtokenRefreshIntervalSeconds", {{"type", "integer"}, {"minimum", 0}, {"maximum", 3600}}},
             {"auto_refresh_pacmtoken", {{"type", "boolean"}}},
             {"autoRefreshPacmtoken", {{"type", "boolean"}}},
             {"refresh_pacmtoken_on_440013", {{"type", "boolean"}}},
             {"refreshPacmtokenOn440013", {{"type", "boolean"}}},
         }},
    };
    schemas["SettingsSnapshot"] = {
        {"type", "object"},
        {"required", json::array({"mutable", "readonly"})},
        {"properties",
         {
             {"mutable", {{"type", "object"}, {"properties", {{"max_batch_concurrency", {{"type", "integer"}}}, {"max_page_concurrency", {{"type", "integer"}}}, {"max_retries", {{"type", "integer"}}}, {"task_timeout_ms", {{"type", "integer"}}}, {"resource_download_concurrency", {{"type", "integer"}}}, {"resource_download_retries", {{"type", "integer"}}}, {"resource_download_timeout_ms", {{"type", "integer"}}}, {"pacmtoken_refresh_interval_seconds", {{"type", "integer"}}}, {"auto_refresh_pacmtoken", {{"type", "boolean"}}}, {"refresh_pacmtoken_on_440013", {{"type", "boolean"}}}}}}},
             {"readonly", {{"type", "object"}, {"properties", {{"http_worker_threads", {{"type", "integer"}}}, {"async_worker_threads", {{"type", "integer"}}}, {"resource_worker_threads", {{"type", "integer"}}}, {"parse_worker_threads", {{"type", "integer"}}}, {"persistence_worker_threads", {{"type", "integer"}}}, {"outbound_http_io_threads", {{"type", "integer"}}}, {"mysql_io_threads", {{"type", "integer"}}}, {"mysql_max_pool_size", {{"type", "integer"}}}, {"persistence_backend", {{"type", "string"}}}}}}},
             {"notes", {{"type", "object"}, {"additionalProperties", {{"type", "string"}}}}},
         }},
    };
    schemas["PacmtokenRefreshRequest"] = {
        {"type", "object"},
        {"description", "Refresh or validate a caller-provided Migu pacmtoken. The first call must provide pacmtoken or a Cookie string containing pacmtoken. Later calls may omit the body to reuse the token stored in backend runtime memory. The backend does not read Migu Cookie environment variables and does not persist the token."},
        {"properties",
         {
             {"pacmtoken", {{"type", "string"}, {"maxLength", 8192}, {"description", "Current raw pacmtoken. A full Cookie string containing pacmtoken is also accepted, but cookie is preferred for full Cookie input."}}},
             {"cookie", {{"type", "string"}, {"maxLength", 8192}, {"description", "Optional full Cookie string copied from an authenticated browser session; pacmtoken is extracted server-side."}}},
             {"store", {{"type", "boolean"}, {"default", true}, {"description", "Whether to store the successful token in backend runtime memory for subsequent refresh calls and resource processing."}}},
             {"update_runtime", {{"type", "boolean"}, {"description", "Alias of store; kept for frontend compatibility."}}},
         }},
        {"example", {{"pacmtoken", "current-pacmtoken"}, {"store", true}}},
    };
    schemas["PacmtokenRefreshResponse"] = {
        {"type", "object"},
        {"description", "Refresh result. The pacmtoken value is returned only so the frontend timer can keep its local state in sync; do not log it."},
        {"required", json::array({"success", "refreshed", "pacmtoken", "pacmtoken_length", "updated_at"})},
        {"properties",
         {
             {"success", {{"type", "boolean"}}},
             {"refreshed", {{"type", "boolean"}, {"description", "True when checkPacMtoken returned a different token."}}},
             {"pacmtoken", {{"type", "string"}, {"description", "Token returned for frontend timer state. Do not log this value."}}},
             {"pacmtoken_length", {{"type", "integer"}, {"minimum", 0}}},
             {"upstream_status", {{"nullable", true}, {"type", "integer"}}},
             {"updated_at", {{"type", "integer"}, {"minimum", 0}}},
             {"message", {{"type", "string"}}},
         }},
        {"example", {{"success", true}, {"refreshed", true}, {"pacmtoken", "new-pacmtoken"}, {"pacmtoken_length", 56}, {"upstream_status", 200}, {"updated_at", 1779370000}, {"message", "migu pacmtoken refreshed"}}},
    };
    schemas["PacmtokenStateResponse"] = {
        {"type", "object"},
        {"description", "Runtime pacmtoken state without exposing the token value."},
        {"required", json::array({"configured", "has_pacmtoken", "pacmtoken_length", "updated_at"})},
        {"properties",
         {
             {"configured", {{"type", "boolean"}}},
             {"has_pacmtoken", {{"type", "boolean"}}},
             {"pacmtoken_length", {{"type", "integer"}, {"minimum", 0}}},
             {"updated_at", {{"nullable", true}, {"type", "integer"}, {"minimum", 0}}},
         }},
    };
    schemas["PacmtokenCurrentResponse"] = {
        {"type", "object"},
        {"description", "Runtime pacmtoken current value for explicit frontend sync. Do not log this response body."},
        {"required", json::array({"configured", "has_pacmtoken", "pacmtoken_length", "updated_at", "pacmtoken"})},
        {"properties",
         {
             {"configured", {{"type", "boolean"}}},
             {"has_pacmtoken", {{"type", "boolean"}}},
             {"pacmtoken_length", {{"type", "integer"}, {"minimum", 0}}},
             {"updated_at", {{"nullable", true}, {"type", "integer"}, {"minimum", 0}}},
             {"pacmtoken", {{"type", "string"}, {"description", "Current runtime token; never persisted by the backend."}}},
         }},
    };
    schemas["HealthResponse"] = {
        {"type", "object"},
        {"required", json::array({"status"})},
        {"properties", {{"status", {{"type", "string"}, {"example", "ok"}}}}},
    };
}

// 创建启动采集任务的路径定义。
json crawlPostPath(std::string summary, std::string description, std::string schema_ref)
{
    return json {
        {"tags", json::array({"migu"})},
        {"summary", std::move(summary)},
        {"description", std::move(description)},
        {"requestBody", requestBody(ref(std::move(schema_ref)))},
        {"responses", {{"202", {{"description", "Task accepted"}, {"content", jsonContent(ref("#/components/schemas/TaskResponse"))}}}, {"400", errorResponse("Invalid request body or limits")}}},
    };
}

// 创建从库歌手采集预览路径定义。
json singerFromDbPreviewPostPath(std::string summary, std::string description)
{
    return json {
        {"tags", json::array({"migu"})},
        {"summary", std::move(summary)},
        {"description", std::move(description)},
        {"requestBody", requestBody(ref("#/components/schemas/SingerFromDbCrawlRequest"), false)},
        {"responses", {{"200", {{"description", "Preview result"}, {"content", jsonContent(ref("#/components/schemas/SingerFromDbPreviewResponse"))}}}, {"400", errorResponse("Invalid preview request")}, {"503", errorResponse("Singer source sync queue is not configured")}}},
    };
}

// 创建 query 参数定义，避免列表接口只写自然语言描述。
json queryParameter(std::string name, json schema, std::string description = {})
{
    json parameter = {
        {"name", std::move(name)},
        {"in", "query"},
        {"required", false},
        {"schema", std::move(schema)},
    };
    if (!description.empty()) {
        parameter["description"] = std::move(description);
    }
    return parameter;
}

// 逗号分隔状态过滤参数 schema。
json statusFilterSchema(std::string description)
{
    return {{"type", "string"}, {"pattern", "^[0-4](,[0-4])*$"}, {"description", std::move(description)}};
}

// 创建内容查询路径定义。
json catalogGetPath(std::string summary, std::string description, std::string path_parameter = {}, std::string response_schema = "#/components/schemas/CatalogPage", json extra_parameters = json::array())
{
    json parameters = json::array();
    if (!path_parameter.empty()) {
        parameters.push_back({{"name", std::move(path_parameter)}, {"in", "path"}, {"required", true}, {"schema", {{"type", "string"}}}});
    }
    parameters.push_back({{"name", "page"}, {"in", "query"}, {"required", false}, {"schema", {{"type", "integer"}, {"minimum", 1}, {"default", 1}}}});
    parameters.push_back({{"name", "page_size"}, {"in", "query"}, {"required", false}, {"schema", {{"type", "integer"}, {"minimum", 1}, {"maximum", 200}, {"default", 20}}}});
    parameters.push_back({{"name", "q"}, {"in", "query"}, {"required", false}, {"schema", {{"type", "string"}}}});
    for (auto &parameter : extra_parameters) {
        parameters.push_back(std::move(parameter));
    }

    return json {
        {"tags", json::array({"migu"})},
        {"summary", std::move(summary)},
        {"description", std::move(description)},
        {"parameters", std::move(parameters)},
        {"responses", {
                          {"200", {{"description", "Catalog page"}, {"content", jsonContent(ref(std::move(response_schema)))}}},
                          {"400", errorResponse("Invalid page or page_size")},
                      }},
    };
}

// 创建内容详情路径定义。
json catalogDetailPath(std::string summary, std::string description, std::string path_parameter, std::string response_schema)
{
    return json {
        {"tags", json::array({"migu"})},
        {"summary", std::move(summary)},
        {"description", std::move(description)},
        {"parameters", json::array({{{"name", std::move(path_parameter)}, {"in", "path"}, {"required", true}, {"schema", {{"type", "string"}}}}})},
        {"responses", {
                          {"200", {{"description", "Catalog record"}, {"content", jsonContent(ref(std::move(response_schema)))}}},
                          {"400", errorResponse("Invalid path parameter")},
                          {"404", errorResponse("Catalog record not found")},
                      }},
    };
}

// 创建补池操作路径定义。
json backfillPostPath(std::string summary, bool accepted)
{
    return json {
        {"tags", json::array({"migu"})},
        {"summary", std::move(summary)},
        {"requestBody", requestBody(ref("#/components/schemas/BackfillActionRequest"), false)},
        {"responses", accepted ? json {{"202", {{"description", "Task accepted"}, {"content", jsonContent(ref("#/components/schemas/TaskResponse"))}}}, {"400", errorResponse("Invalid backfill request")}} : json {{"200", {{"description", "Reset result"}, {"content", jsonContent(ref("#/components/schemas/BackfillResetResponse"))}}}, {"400", errorResponse("Reset requires IDs or filters")}}},
    };
}

} // namespace

std::string OpenApiDocumentFactory::miguDocumentJson()
{
    json doc;
    doc["openapi"] = "3.0.3";
    doc["info"] = {
        {"title", "Crawler Migu Metadata API"},
        {"version", "1.7.0"},
        {"description", "Migu metadata crawler API. Only the current singer, singer songs, singer albums, album details and album songs workflows are exposed."},
    };
    doc["servers"] = json::array({json {{"url", "http://127.0.0.1:8080"}, {"description", "Local crawler service"}}});
    doc["tags"] = json::array({
        json {{"name", "migu"}, {"description", "Migu metadata crawl operations"}},
        json {{"name", "system"}, {"description", "Service health and OpenAPI document"}},
    });

    buildSchemas(doc["components"]["schemas"]);

    doc["paths"]["/api/migu/singers/crawl"]["post"] = crawlPostPath("Crawl singer catalog", "Crawl Migu singer categories and save singers plus singer_source_refs.", "#/components/schemas/SingerIndexCrawlRequest");
    doc["paths"]["/api/migu/singer-songs/crawl"]["post"] = crawlPostPath("Crawl singer songs", "Crawl songs for one or many Migu singer IDs.", "#/components/schemas/SingerIdsCrawlRequest");
    doc["paths"]["/api/migu/singer-songs/crawl-from-db"]["post"] = crawlPostPath("Crawl singer songs from database queue", "Claim singers from singer_source_refs by songs_sync_status and crawl songs. Optional country, gender and q filters are ignored when empty.", "#/components/schemas/SingerFromDbCrawlRequest");
    doc["paths"]["/api/migu/singer-songs/crawl-from-db/preview"]["post"] = singerFromDbPreviewPostPath("Preview singer songs crawl from database queue", "Returns matched count, eligible count and target samples before claiming singer_source_refs rows for song crawling.");
    doc["paths"]["/api/migu/singer-albums/crawl"]["post"] = crawlPostPath("Crawl singer albums", "Crawl album list records for one or many Migu singer IDs.", "#/components/schemas/SingerIdsCrawlRequest");
    doc["paths"]["/api/migu/singer-albums/crawl-from-db"]["post"] = crawlPostPath("Crawl singer albums from database queue", "Claim singers from singer_source_refs by albums_sync_status and crawl albums. Optional country, gender and q filters are ignored when empty.", "#/components/schemas/SingerFromDbCrawlRequest");
    doc["paths"]["/api/migu/singer-albums/crawl-from-db/preview"]["post"] = singerFromDbPreviewPostPath("Preview singer albums crawl from database queue", "Returns matched count, eligible count and target samples before claiming singer_source_refs rows for album crawling.");
    doc["paths"]["/api/migu/album-details/crawl"]["post"] = crawlPostPath("Backfill album details", "Fetch pending album details from album_source_refs and save detail metadata. The same response also parses songItems when present.", "#/components/schemas/AlbumBackfillRequest");
    doc["paths"]["/api/migu/album-songs/crawl"]["post"] = crawlPostPath("Backfill album songs", "Fetch pending album songItems for album_source_refs rows whose details are already synced.", "#/components/schemas/AlbumBackfillRequest");

    doc["paths"]["/api/migu/tasks"]["get"] = {
        {"tags", json::array({"migu"})},
        {"summary", "List persisted Migu tasks"},
        {"description", "Returns task batches from MySQL so the frontend can restore task lists after refresh or service restart."},
        {"parameters", json::array({
                           {{"name", "page"}, {"in", "query"}, {"required", false}, {"schema", {{"type", "integer"}, {"minimum", 1}, {"default", 1}}}},
                           {{"name", "page_size"}, {"in", "query"}, {"required", false}, {"schema", {{"type", "integer"}, {"minimum", 1}, {"maximum", 200}, {"default", 20}}}},
                           {{"name", "state"}, {"in", "query"}, {"required", false}, {"schema", {{"type", "string"}, {"enum", json::array({"Pending", "Running", "Completed", "Failed", "Cancelled"})}}}},
                           {{"name", "type"}, {"in", "query"}, {"required", false}, {"schema", {{"type", "string"}, {"enum", json::array({"singerIndex", "singerSongs", "singerAlbums", "singerSongsFromDb", "singerAlbumsFromDb", "albumDetails", "albumSongs"})}}}},
                           {{"name", "q"}, {"in", "query"}, {"required", false}, {"schema", {{"type", "string"}, {"maxLength", 128}}}, {"description", "Searches batch id, type, display_name, target_summary, target_keywords, last_error, error_category and result_code."}},
                           {{"name", "display_name"}, {"in", "query"}, {"required", false}, {"schema", {{"type", "string"}, {"maxLength", 128}}}},
                           {{"name", "target_summary"}, {"in", "query"}, {"required", false}, {"schema", {{"type", "string"}, {"maxLength", 128}}}},
                           {{"name", "target_keywords"}, {"in", "query"}, {"required", false}, {"schema", {{"type", "string"}, {"maxLength", 128}}}},
                           {{"name", "last_error"}, {"in", "query"}, {"required", false}, {"schema", {{"type", "string"}, {"maxLength", 128}}}},
                           {{"name", "error_category"}, {"in", "query"}, {"required", false}, {"schema", {{"type", "string"}, {"enum", json::array({"network", "parse", "persistence", "copyright", "resource", "invalid_target", "timeout", "unknown"})}}}},
                           {{"name", "result_code"}, {"in", "query"}, {"required", false}, {"schema", {{"type", "string"}, {"enum", json::array({"ok", "network_failed", "parse_failed", "persist_failed", "invalid_target", "timeout", "partial_completed", "failed"})}}}},
                           {{"name", "include_archived"}, {"in", "query"}, {"required", false}, {"schema", {{"type", "boolean"}, {"default", false}}}},
                       })},
        {"responses", {{"200", {{"description", "Task batch page"}, {"content", jsonContent(ref("#/components/schemas/BatchTaskPage"))}}}}},
    };
    doc["paths"]["/api/migu/tasks/summary"]["get"] = {
        {"tags", json::array({"migu"})},
        {"summary", "Get Migu task summary"},
        {"description", "Returns global batch state distribution, latest success/failure batches, running count and stale running signal."},
        {"parameters", json::array({{{"name", "stale_after_seconds"}, {"in", "query"}, {"required", false}, {"schema", {{"type", "integer"}, {"minimum", 1}, {"default", 3600}}}}})},
        {"responses", {{"200", {{"description", "Task summary"}, {"content", jsonContent(ref("#/components/schemas/TaskSummary"))}}}, {"400", errorResponse("Invalid stale_after_seconds")}}},
    };
    doc["paths"]["/api/migu/tasks/{batch_id}"]["get"] = {
        {"tags", json::array({"migu"})},
        {"summary", "Get persisted Migu task"},
        {"parameters", json::array({{{"name", "batch_id"}, {"in", "path"}, {"required", true}, {"schema", {{"type", "string"}, {"maxLength", 128}}}}})},
        {"responses", {{"200", {{"description", "Task batch"}, {"content", jsonContent(ref("#/components/schemas/BatchTask"))}}}, {"400", errorResponse("batch_id is missing")}, {"404", errorResponse("batch_id not found")}}},
    };
    doc["paths"]["/api/migu/tasks/{batch_id}/items"]["get"] = {
        {"tags", json::array({"migu"})},
        {"summary", "List persisted Migu task items"},
        {"parameters", json::array({
                           {{"name", "batch_id"}, {"in", "path"}, {"required", true}, {"schema", {{"type", "string"}, {"maxLength", 128}}}},
                           {{"name", "page"}, {"in", "query"}, {"required", false}, {"schema", {{"type", "integer"}, {"minimum", 1}, {"default", 1}}}},
                           {{"name", "page_size"}, {"in", "query"}, {"required", false}, {"schema", {{"type", "integer"}, {"minimum", 1}, {"maximum", 200}, {"default", 50}}}},
                           {{"name", "state"}, {"in", "query"}, {"required", false}, {"schema", {{"type", "string"}, {"enum", json::array({"Pending", "Running", "Completed", "Failed", "Cancelled"})}}}},
                           {{"name", "error_category"}, {"in", "query"}, {"required", false}, {"schema", {{"type", "string"}, {"enum", json::array({"network", "parse", "persistence", "copyright", "resource", "invalid_target", "timeout", "unknown"})}}}},
                           {{"name", "result_code"}, {"in", "query"}, {"required", false}, {"schema", {{"type", "string"}, {"enum", json::array({"ok", "network_failed", "parse_failed", "persist_failed", "invalid_target", "timeout", "partial_completed", "failed"})}}}},
                       })},
        {"responses", {{"200", {{"description", "Task item page"}, {"content", jsonContent(ref("#/components/schemas/BatchTaskItemPage"))}}}, {"400", errorResponse("batch_id is missing")}}},
    };
    doc["paths"]["/api/migu/tasks/{batch_id}/events"]["get"] = {
        {"tags", json::array({"migu"})},
        {"summary", "List persisted Migu task events"},
        {"parameters", json::array({
                           {{"name", "batch_id"}, {"in", "path"}, {"required", true}, {"schema", {{"type", "string"}, {"maxLength", 128}}}},
                           {{"name", "page"}, {"in", "query"}, {"required", false}, {"schema", {{"type", "integer"}, {"minimum", 1}, {"default", 1}}}},
                           {{"name", "page_size"}, {"in", "query"}, {"required", false}, {"schema", {{"type", "integer"}, {"minimum", 1}, {"maximum", 200}, {"default", 100}}}},
                       })},
        {"responses", {{"200", {{"description", "Task event page"}, {"content", jsonContent(ref("#/components/schemas/BatchTaskEventPage"))}}}, {"400", errorResponse("batch_id is missing")}}},
    };
    const auto batchGovernancePath = [](std::string summary, std::string description) {
        return json {
            {"tags", json::array({"migu"})},
            {"summary", std::move(summary)},
            {"description", std::move(description)},
            {"parameters", json::array({{{"name", "batch_id"}, {"in", "path"}, {"required", true}, {"schema", {{"type", "string"}, {"maxLength", 128}}}}})},
            {"requestBody", requestBody(ref("#/components/schemas/BatchGovernanceRequest"), false)},
            {"responses", {{"202", {{"description", "New governance batch created"}, {"content", jsonContent(ref("#/components/schemas/BatchGovernanceResponse"))}}}, {"400", errorResponse("Invalid batch governance request")}, {"404", errorResponse("No eligible batch items")}}},
        };
    };
    doc["paths"]["/api/migu/tasks/{batch_id}/rerun-failed"]["post"] = batchGovernancePath("Rerun failed Migu task items", "Creates a new batch from failed, cancelled or timeout items of the old batch. The old batch remains immutable.");
    doc["paths"]["/api/migu/tasks/{batch_id}/clone"]["post"] = batchGovernancePath("Clone Migu task", "Creates a new batch from the old batch request and item set. mode=failed clones only failed items.");
    doc["paths"]["/api/migu/tasks/{batch_id}/backfill-retry"]["post"] = batchGovernancePath("Create backfill retry from task items", "Extracts singer or album metadata targets from supported task items and creates a new backfill retry batch.");
    doc["paths"]["/api/migu/tasks/{batch_id}/archive"]["post"] = {
        {"tags", json::array({"migu"})},
        {"summary", "Archive Migu task"},
        {"description", "Marks a batch as archived/hidden without deleting batch, item or event history."},
        {"parameters", json::array({{{"name", "batch_id"}, {"in", "path"}, {"required", true}, {"schema", {{"type", "string"}, {"maxLength", 128}}}}})},
        {"requestBody", requestBody(ref("#/components/schemas/BatchGovernanceRequest"), false)},
        {"responses", {{"200", {{"description", "Batch archived"}, {"content", jsonContent(ref("#/components/schemas/BatchArchiveResponse"))}}}, {"400", errorResponse("Invalid archive request")}, {"404", errorResponse("batch_id not found")}}},
    };
    doc["paths"]["/api/migu/tasks/progress"]["get"] = {
        {"tags", json::array({"migu"})},
        {"summary", "Poll Migu task progress"},
        {"parameters", json::array({{{"name", "batch_id"}, {"in", "query"}, {"required", true}, {"schema", {{"type", "string"}}}}})},
        {"responses", {{"200", {{"description", "Task progress"}, {"content", jsonContent(ref("#/components/schemas/Progress"))}}}, {"400", errorResponse("batch_id is missing")}, {"404", errorResponse("batch_id not found")}}},
    };
    doc["paths"]["/api/migu/tasks/stop"]["post"] = {
        {"tags", json::array({"migu"})},
        {"summary", "Request Migu task stop"},
        {"description", "Cancellation is cooperative. batch_id may be supplied in JSON body or query string."},
        {"parameters", json::array({{{"name", "batch_id"}, {"in", "query"}, {"required", false}, {"schema", {{"type", "string"}, {"maxLength", 128}}}}})},
        {"requestBody", requestBody(ref("#/components/schemas/BatchControlRequest"), false)},
        {"responses", {{"200", {{"description", "Stop requested"}, {"content", jsonContent(ref("#/components/schemas/StopResponse"))}}}, {"400", errorResponse("batch_id is missing or invalid")}, {"404", errorResponse("batch_id not found")}, {"409", errorResponse("batch cannot be cancelled")}}},
    };
    doc["paths"]["/api/migu/overview"]["get"] = {
        {"tags", json::array({"migu"})},
        {"summary", "Get Migu catalog overview"},
        {"description", "Aggregates persisted singer, song, album, sync queue and resource status counts for the frontend dashboard."},
        {"responses", {{"200", {{"description", "Catalog overview"}, {"content", jsonContent(ref("#/components/schemas/Overview"))}}}}},
    };
    doc["paths"]["/api/migu/integrity"]["get"] = {
        {"tags", json::array({"migu"})},
        {"summary", "Get Migu catalog integrity"},
        {"description", "Returns lightweight counts for unfinished metadata, stale metadata running states, stale batches and stale resource tasks. Singer missing-song and missing-album counts use real relationship existence: song_artist_refs for songs and album_source_refs.source_artist_id for albums, not only sync status fields."},
        {"parameters", json::array({{{"name", "stale_after_seconds"}, {"in", "query"}, {"required", false}, {"schema", {{"type", "integer"}, {"minimum", 60}, {"default", 3600}}}}})},
        {"responses", {{"200", {{"description", "Integrity summary"}, {"content", jsonContent(ref("#/components/schemas/IntegritySummary"))}}}}},
    };
    doc["paths"]["/api/migu/schema/status"]["get"] = {
        {"tags", json::array({"migu"})},
        {"summary", "Get Migu MySQL schema status"},
        {"description", "Checks crawler MySQL schema version and required tables/columns for the current metadata console. This endpoint does not migrate data."},
        {"responses", {{"200", {{"description", "Schema status"}, {"content", jsonContent(ref("#/components/schemas/SchemaStatus"))}}}}},
    };
    doc["paths"]["/api/migu/maintenance/recover-stale"]["post"] = {
        {"tags", json::array({"migu"})},
        {"summary", "Recover stale Migu running states"},
        {"description", "Marks metadata rows and persisted batches that have stayed in running state beyond stale_after_seconds as failed so backfill retry can govern them again. This endpoint does not fetch or download resources."},
        {"requestBody", requestBody(ref("#/components/schemas/StaleRecoveryRequest"), false)},
        {"responses", {{"200", {{"description", "Recovery result"}, {"content", jsonContent(ref("#/components/schemas/StaleRecoveryResponse"))}}}, {"400", errorResponse("Invalid stale recovery request")}}},
    };
    doc["paths"]["/api/migu/singers"]["get"] = catalogGetPath("List Migu singers", "Supports q, gender, country, songs_sync_status and albums_sync_status filters. Song and album summary counts are derived from real relationships: song_artist_refs for songs and album_source_refs.source_artist_id for albums. Status filters use the same exposed status, so singers with real content and no sync error are treated as completed (2) instead of pending (0), while running (1) remains running.", {}, "#/components/schemas/SingerRecordPage", json::array({
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          queryParameter("gender", {{"type", "integer"}, {"minimum", 0}, {"maximum", 3}}, "Optional singer gender filter."),
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          queryParameter("country", {{"type", "string"}, {"maxLength", 128}}, "Optional source country/region filter."),
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          queryParameter("songs_sync_status", statusFilterSchema("Comma-separated exposed singer-song sync statuses. Real song_artist_refs with no sync error are exposed as completed (2).")),
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          queryParameter("albums_sync_status", statusFilterSchema("Comma-separated exposed singer-album sync statuses. Real album_source_refs.source_artist_id relations with no sync error are exposed as completed (2).")),
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          queryParameter("min_songs_count", {{"type", "integer"}, {"minimum", 0}}, "Minimum related song count."),
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          queryParameter("max_songs_count", {{"type", "integer"}, {"minimum", 0}}, "Maximum related song count."),
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          queryParameter("min_albums_count", {{"type", "integer"}, {"minimum", 0}}, "Minimum related album count."),
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                          queryParameter("max_albums_count", {{"type", "integer"}, {"minimum", 0}}, "Maximum related album count."),
                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                                      }));
    doc["paths"]["/api/migu/singers/{source_singer_id}"]["get"] = catalogDetailPath("Get Migu singer", "Returns one singer by Migu source_singer_id with real relationship summary counts from song_artist_refs and album_source_refs.source_artist_id. When real content exists and no sync error is stored, the exposed sync status is completed (2) instead of pending (0), except running (1) remains running.", "source_singer_id", "#/components/schemas/SingerRecord");
    doc["paths"]["/api/migu/singers/{source_singer_id}/songs"]["get"] = catalogGetPath("List Migu singer songs", "Returns songs associated with one Migu singer ID.", "source_singer_id", "#/components/schemas/SongRecordPage", json::array({
                                                                                                                                                                                                                                     queryParameter("artist", {{"type", "string"}, {"maxLength", 128}}, "Optional artist name or source singer ID filter."),
                                                                                                                                                                                                                                     queryParameter("album", {{"type", "string"}, {"maxLength", 128}}, "Optional album title or source album ID filter."),
                                                                                                                                                                                                                                     queryParameter("resource_status", statusFilterSchema("Comma-separated resource task statuses.")),
                                                                                                                                                                                                                                     queryParameter("format", {{"type", "string"}, {"maxLength", 32}}, "Optional audio format filter such as HQ or SQ."),
                                                                                                                                                                                                                                 }));
    doc["paths"]["/api/migu/singers/{source_singer_id}/albums"]["get"] = catalogGetPath("List Migu singer albums", "Returns albums associated with one Migu singer ID.", "source_singer_id", "#/components/schemas/AlbumRecordPage", json::array({
                                                                                                                                                                                                                                         queryParameter("artist", {{"type", "string"}, {"maxLength", 128}}, "Optional artist name or source singer ID filter."),
                                                                                                                                                                                                                                         queryParameter("kind", {{"type", "integer"}, {"minimum", 0}, {"maximum", 2}}, "Optional album kind filter."),
                                                                                                                                                                                                                                         queryParameter("detail_sync_status", statusFilterSchema("Comma-separated detail sync statuses.")),
                                                                                                                                                                                                                                         queryParameter("songs_sync_status", statusFilterSchema("Comma-separated album song sync statuses.")),
                                                                                                                                                                                                                                         queryParameter("min_songs_count", {{"type", "integer"}, {"minimum", 0}}, "Minimum related album-song count."),
                                                                                                                                                                                                                                         queryParameter("max_songs_count", {{"type", "integer"}, {"minimum", 0}}, "Maximum related album-song count."),
                                                                                                                                                                                                                                         queryParameter("min_album_songs_count", {{"type", "integer"}, {"minimum", 0}}, "Alias of min_songs_count for album-song relation filtering."),
                                                                                                                                                                                                                                         queryParameter("max_album_songs_count", {{"type", "integer"}, {"minimum", 0}}, "Alias of max_songs_count for album-song relation filtering."),
                                                                                                                                                                                                                                     }));
    doc["paths"]["/api/migu/albums"]["get"] = catalogGetPath("List Migu albums", "Supports q, artist, kind, detail_sync_status and songs_sync_status filters.", {}, "#/components/schemas/AlbumRecordPage", json::array({
                                                                                                                                                                                                                queryParameter("artist", {{"type", "string"}, {"maxLength", 128}}, "Optional artist name or source singer ID filter."),
                                                                                                                                                                                                                queryParameter("kind", {{"type", "integer"}, {"minimum", 0}, {"maximum", 2}}, "Optional album kind filter."),
                                                                                                                                                                                                                queryParameter("detail_sync_status", statusFilterSchema("Comma-separated detail sync statuses.")),
                                                                                                                                                                                                                queryParameter("songs_sync_status", statusFilterSchema("Comma-separated album song sync statuses.")),
                                                                                                                                                                                                                queryParameter("min_songs_count", {{"type", "integer"}, {"minimum", 0}}, "Minimum related album-song count."),
                                                                                                                                                                                                                queryParameter("max_songs_count", {{"type", "integer"}, {"minimum", 0}}, "Maximum related album-song count."),
                                                                                                                                                                                                                queryParameter("min_album_songs_count", {{"type", "integer"}, {"minimum", 0}}, "Alias of min_songs_count for album-song relation filtering."),
                                                                                                                                                                                                                queryParameter("max_album_songs_count", {{"type", "integer"}, {"minimum", 0}}, "Alias of max_songs_count for album-song relation filtering."),
                                                                                                                                                                                                            }));
    doc["paths"]["/api/migu/albums/{source_album_id}"]["get"] = catalogDetailPath("Get Migu album", "Returns one album by Migu source_album_id.", "source_album_id", "#/components/schemas/AlbumRecord");
    doc["paths"]["/api/migu/albums/{source_album_id}/songs"]["get"] = catalogGetPath("List Migu album songs", "Returns songs associated with one Migu album ID.", "source_album_id", "#/components/schemas/SongRecordPage", json::array({
                                                                                                                                                                                                                                queryParameter("artist", {{"type", "string"}, {"maxLength", 128}}, "Optional artist name or source singer ID filter."),
                                                                                                                                                                                                                                queryParameter("album", {{"type", "string"}, {"maxLength", 128}}, "Optional album title or source album ID filter."),
                                                                                                                                                                                                                                queryParameter("resource_status", statusFilterSchema("Comma-separated resource task statuses.")),
                                                                                                                                                                                                                                queryParameter("format", {{"type", "string"}, {"maxLength", 32}}, "Optional audio format filter such as HQ or SQ."),
                                                                                                                                                                                                                            }));
    doc["paths"]["/api/migu/songs"]["get"] = catalogGetPath("List Migu songs", "Supports q, artist, album, resource_status and format filters.", {}, "#/components/schemas/SongRecordPage", json::array({
                                                                                                                                                                                                queryParameter("artist", {{"type", "string"}, {"maxLength", 128}}, "Optional artist name or source singer ID filter."),
                                                                                                                                                                                                queryParameter("album", {{"type", "string"}, {"maxLength", 128}}, "Optional album title or source album ID filter."),
                                                                                                                                                                                                queryParameter("resource_status", statusFilterSchema("Comma-separated resource task statuses.")),
                                                                                                                                                                                                queryParameter("format", {{"type", "string"}, {"maxLength", 32}}, "Optional audio format filter such as HQ or SQ."),
                                                                                                                                                                                            }));
    doc["paths"]["/api/migu/songs/{source_song_id}"]["get"] = catalogDetailPath("Get Migu song", "Returns one song by Migu source_song_id or source_content_id.", "source_song_id", "#/components/schemas/SongRecord");
    doc["paths"]["/api/migu/resources/queue"]["get"] = catalogGetPath("List Migu resource queue", "Resource task queue view. This endpoint does not fetch or download resources.", {}, "#/components/schemas/ResourceQueueRecordPage", json::array({
                                                                                                                                                                                                                                           queryParameter("status", statusFilterSchema("Comma-separated resource task statuses.")),
                                                                                                                                                                                                                                           queryParameter("kind", {{"type", "string"}, {"maxLength", 32}}, "Optional resource kind filter."),
                                                                                                                                                                                                                                           queryParameter("format", {{"type", "string"}, {"maxLength", 32}}, "Optional audio format filter."),
                                                                                                                                                                                                                                       }));
    const auto resourceActionPath = [](std::string summary, std::string description) {
        return json {
            {"tags", json::array({"migu"})},
            {"summary", std::move(summary)},
            {"description", std::move(description)},
            {"requestBody", {{"required", true}, {"content", jsonContent(ref("#/components/schemas/ResourceActionRequest"))}}},
            {"responses", {{"200", {{"description", "Resource queue governance result"}, {"content", jsonContent(ref("#/components/schemas/ResourceActionResponse"))}}}, {"400", ref("#/components/responses/BadRequest")}}},
        };
    };
    doc["paths"]["/api/migu/resources/preview"]["post"] = {
        {"tags", json::array({"migu"})},
        {"summary", "Preview Migu resource queue governance"},
        {"description", "Returns matched, eligible, ineligible and missing resource counts before retry/reset/recover-stale. This endpoint does not write database rows."},
        {"requestBody", requestBody(ref("#/components/schemas/ResourceActionRequest"))},
        {"responses", {{"200", {{"description", "Resource governance preview"}, {"content", jsonContent(ref("#/components/schemas/ResourcePreviewResponse"))}}}, {"400", errorResponse("Invalid resource preview request")}}},
    };
    doc["paths"]["/api/migu/resources/retry"]["post"] = resourceActionPath("Retry Migu resource queue records", "Marks selected failed resource records as pending. Skipped resources remain skipped. It does not fetch, probe or download resources.");
    doc["paths"]["/api/migu/resources/reset"]["post"] = resourceActionPath("Reset Migu resource queue records", "Resets selected resource records to pending and clears retry/error metadata. It does not delete files or fetch resources.");
    doc["paths"]["/api/migu/resources/recover-stale"]["post"] = resourceActionPath("Recover stale Migu resource queue records", "Marks selected stale running resource records as failed so they can be governed later.");
    doc["paths"]["/api/migu/resources/process"]["post"] = {
        {"tags", json::array({"migu"})},
        {"summary", "Process Migu resource queue"},
        {"description", "Claims pending resource records, obtains temporary Migu resource URLs, downloads content, saves bytes and writes results back to MySQL. Omitted kind processes all resource kinds; use filter.kind to select audio, cover, lyric, mrc or trc. Known digital albums are skipped without requesting resource URLs; when an audio response returns 440016, all audio formats for that song are marked skipped while cover/lyric/mrc/trc remain eligible. Set async=true for long-running background processing; item and event rows are written in real time."},
        {"requestBody", requestBody(ref("#/components/schemas/ResourceProcessRequest"))},
        {"responses", {{"200", {{"description", "Resource processing result"}, {"content", jsonContent(ref("#/components/schemas/ResourceProcessResponse"))}}}, {"202", {{"description", "Resource processing accepted"}, {"content", jsonContent(ref("#/components/schemas/ResourceProcessAcceptedResponse"))}}}, {"400", errorResponse("Invalid resource process request")}, {"503", errorResponse("Resource workflow is not configured")}}},
    };
    doc["paths"]["/api/migu/dictionaries"]["get"] = {
        {"tags", json::array({"migu"})},
        {"summary", "Get Migu frontend dictionaries"},
        {"description", "Returns status, gender, album kind, resource kind, audio format and batch error/result code dictionaries for frontend options."},
        {"responses", {{"200", {{"description", "Dictionary values"}, {"content", jsonContent(ref("#/components/schemas/DictionaryResponse"))}}}}},
    };
    const json backfill_singer_params = json::array({
        queryParameter("status", statusFilterSchema("Comma-separated effective metadata sync statuses. Defaults to 0,3,4. Singer queues derive completed status from real song_artist_refs or album_source_refs.source_artist_id when content exists and no sync error is stored.")),
        queryParameter("sync_status", statusFilterSchema("Alias of status with the same effective real-relation status semantics.")),
        queryParameter("country", {{"type", "string"}, {"maxLength", 128}}, "Optional source country/region filter."),
        queryParameter("gender", {{"type", "integer"}, {"minimum", 0}, {"maximum", 3}}, "Optional singer gender filter."),
        queryParameter("min_songs_count", {{"type", "integer"}, {"minimum", 0}}, "Minimum related song count."),
        queryParameter("max_songs_count", {{"type", "integer"}, {"minimum", 0}}, "Maximum related song count."),
        queryParameter("min_albums_count", {{"type", "integer"}, {"minimum", 0}}, "Minimum related album count."),
        queryParameter("max_albums_count", {{"type", "integer"}, {"minimum", 0}}, "Maximum related album count."),
    });
    const json backfill_album_params = json::array({
        queryParameter("status", statusFilterSchema("Comma-separated metadata sync statuses. Defaults to 0,3,4.")),
        queryParameter("sync_status", statusFilterSchema("Alias of status.")),
        queryParameter("artist", {{"type", "string"}, {"maxLength", 128}}, "Optional artist name or source singer ID filter."),
        queryParameter("kind", {{"type", "integer"}, {"minimum", 0}, {"maximum", 2}}, "Optional album kind filter."),
        queryParameter("min_songs_count", {{"type", "integer"}, {"minimum", 0}}, "Minimum related album-song count."),
        queryParameter("max_songs_count", {{"type", "integer"}, {"minimum", 0}}, "Maximum related album-song count."),
        queryParameter("min_album_songs_count", {{"type", "integer"}, {"minimum", 0}}, "Alias of min_songs_count for album queues."),
        queryParameter("max_album_songs_count", {{"type", "integer"}, {"minimum", 0}}, "Alias of max_songs_count for album queues."),
    });
    doc["paths"]["/api/migu/backfill/singer-songs/queue"]["get"] = catalogGetPath("List singer songs backfill queue", "Default status filter is 0,3,4, but singer-song targets use the same real song_artist_refs summary as /api/migu/singers. Singers that already have related songs and no sync error are exposed as completed (2), report a real sync_count, and are not returned by the default missing-song queue.", {}, "#/components/schemas/BackfillQueueTargetPage", backfill_singer_params);
    doc["paths"]["/api/migu/backfill/singer-albums/queue"]["get"] = catalogGetPath("List singer albums backfill queue", "Default status filter is 0,3,4, but singer-album targets use the same real album_source_refs.source_artist_id summary as /api/migu/singers. Singers that already have related albums and no sync error are exposed as completed (2), report a real sync_count, and are not returned by the default missing-album queue.", {}, "#/components/schemas/BackfillQueueTargetPage", backfill_singer_params);
    doc["paths"]["/api/migu/backfill/album-details/queue"]["get"] = catalogGetPath("List album details backfill queue", "Default status filter is 0,3,4.", {}, "#/components/schemas/BackfillQueueTargetPage", backfill_album_params);
    doc["paths"]["/api/migu/backfill/album-songs/queue"]["get"] = catalogGetPath("List album songs backfill queue", "Default status filter is 0,3,4 and details must be completed.", {}, "#/components/schemas/BackfillQueueTargetPage", backfill_album_params);
    doc["paths"]["/api/migu/backfill/{type}/preview"]["post"] = {
        {"tags", json::array({"migu"})},
        {"summary", "Preview Migu backfill selection"},
        {"description", "Returns matched count, eligible count, missing count and sample targets before retry or reset. This endpoint does not claim or reset records."},
        {"parameters", json::array({{{"name", "type"}, {"in", "path"}, {"required", true}, {"schema", {{"type", "string"}, {"enum", json::array({"singer-songs", "singer-albums", "album-details", "album-songs"})}}}}})},
        {"requestBody", requestBody(ref("#/components/schemas/BackfillActionRequest"), false)},
        {"responses", {{"200", {{"description", "Preview result"}, {"content", jsonContent(ref("#/components/schemas/BackfillPreviewResponse"))}}}, {"400", errorResponse("Invalid preview request")}, {"404", errorResponse("Backfill type not found")}}},
    };
    doc["paths"]["/api/migu/backfill/singer-songs/retry"]["post"] = backfillPostPath("Retry singer songs backfill", true);
    doc["paths"]["/api/migu/backfill/singer-albums/retry"]["post"] = backfillPostPath("Retry singer albums backfill", true);
    doc["paths"]["/api/migu/backfill/album-details/retry"]["post"] = backfillPostPath("Retry album details backfill", true);
    doc["paths"]["/api/migu/backfill/album-songs/retry"]["post"] = backfillPostPath("Retry album songs backfill", true);
    doc["paths"]["/api/migu/backfill/singer-songs/reset"]["post"] = backfillPostPath("Reset singer songs backfill status", false);
    doc["paths"]["/api/migu/backfill/singer-albums/reset"]["post"] = backfillPostPath("Reset singer albums backfill status", false);
    doc["paths"]["/api/migu/backfill/album-details/reset"]["post"] = backfillPostPath("Reset album details backfill status", false);
    doc["paths"]["/api/migu/backfill/album-songs/reset"]["post"] = backfillPostPath("Reset album songs backfill status", false);
    doc["paths"]["/api/migu/governance/events"]["get"] = catalogGetPath("List Migu governance events", "Returns audit events produced by resource, backfill, maintenance and batch governance actions.", {}, "#/components/schemas/GovernanceEventPage", json::array({
                                                                                                                                                                                                                                                             queryParameter("target_type", {{"type", "string"}, {"maxLength", 64}}, "Optional target type filter, such as resource, backfill or batch."),
                                                                                                                                                                                                                                                             queryParameter("target_id", {{"type", "string"}, {"maxLength", 128}}, "Optional target ID filter."),
                                                                                                                                                                                                                                                             queryParameter("action", {{"type", "string"}, {"maxLength", 64}}, "Optional governance action filter."),
                                                                                                                                                                                                                                                             queryParameter("batch_id", {{"type", "string"}, {"maxLength", 128}}, "Optional batch ID filter."),
                                                                                                                                                                                                                                                         }));
    doc["paths"]["/api/migu/settings"]["get"] = {
        {"tags", json::array({"migu"})},
        {"summary", "Get Migu crawler settings"},
        {"responses", {{"200", {{"description", "Settings snapshot"}, {"content", jsonContent(ref("#/components/schemas/SettingsSnapshot"))}}}}},
    };
    doc["paths"]["/api/migu/settings"]["put"] = {
        {"tags", json::array({"migu"})},
        {"summary", "Update mutable Migu crawler settings"},
        {"description", "Updates scheduling and resource download knobs for new scheduling decisions."},
        {"requestBody", requestBody(ref("#/components/schemas/SettingsUpdate"))},
        {"responses", {{"200", {{"description", "Updated settings"}, {"content", jsonContent(ref("#/components/schemas/SettingsSnapshot"))}}}, {"400", errorResponse("Invalid mutable setting")}}},
    };
    doc["paths"]["/api/migu/settings"]["patch"] = doc["paths"]["/api/migu/settings"]["put"];
    doc["paths"]["/api/migu/pacmtoken/refresh"]["post"] = {
        {"tags", json::array({"migu"})},
        {"summary", "Refresh Migu pacmtoken"},
        {"description", "Calls Migu checkPacMtoken with the current pacmtoken, stores the successful token in backend memory by default, and returns the token for the frontend timer. The initial token must be provided by the caller; this endpoint does not implement login, does not read Migu Cookie environment variables, and does not write .env."},
        {"requestBody", requestBody(ref("#/components/schemas/PacmtokenRefreshRequest"), false)},
        {"responses", {{"200", {{"description", "pacmtoken refreshed or still valid"}, {"content", jsonContent(ref("#/components/schemas/PacmtokenRefreshResponse"))}}}, {"400", errorResponse("Invalid pacmtoken request")}, {"502", errorResponse("Migu checkPacMtoken failed")}, {"503", errorResponse("pacmtoken refresher is not configured")}}},
    };
    doc["paths"]["/api/migu/pacmtoken/state"]["get"] = {
        {"tags", json::array({"migu"})},
        {"summary", "Get runtime pacmtoken state"},
        {"description", "Returns only token length and updated time. The token value is not included and this state is not persisted."},
        {"responses", {{"200", {{"description", "pacmtoken state"}, {"content", jsonContent(ref("#/components/schemas/PacmtokenStateResponse"))}}}, {"503", errorResponse("pacmtoken service is not configured")}}},
    };
    doc["paths"]["/api/migu/pacmtoken/current"]["post"] = {
        {"tags", json::array({"migu"})},
        {"summary", "Get current runtime pacmtoken"},
        {"description", "Returns the current in-memory token so the frontend can update local storage after long-running auto refresh. Do not log this response body."},
        {"responses", {{"200", {{"description", "current pacmtoken"}, {"content", jsonContent(ref("#/components/schemas/PacmtokenCurrentResponse"))}}}, {"503", errorResponse("pacmtoken service is not configured")}}},
    };
    doc["paths"]["/api/openapi.json"]["get"] = {
        {"tags", json::array({"system"})},
        {"summary", "Get OpenAPI document"},
        {"responses", {{"200", {{"description", "OpenAPI JSON"}, {"content", jsonContent(json {{"type", "object"}})}}}}},
    };
    doc["paths"]["/api/health"]["get"] = {
        {"tags", json::array({"system"})},
        {"summary", "Health check"},
        {"responses", {{"200", {{"description", "Service is healthy"}, {"content", jsonContent(ref("#/components/schemas/HealthResponse"))}}}}},
    };

    return doc.dump(2);
}

} // namespace orchestrator::modules::crawler::api::controllers
