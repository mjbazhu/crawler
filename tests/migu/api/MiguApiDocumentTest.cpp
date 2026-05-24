#include "crawler/api/controllers/OpenApiDocumentFactory.h"

#include <nlohmann/json.hpp>

#include <cassert>
#include <iostream>
#include <string>

namespace {

using orchestrator::modules::crawler::api::controllers::OpenApiDocumentFactory;

// 断言 OpenAPI 文档包含指定路径。
void requirePath(const nlohmann::json &doc, const std::string &path)
{
    assert(doc.contains("paths"));
    assert(doc["paths"].contains(path));
}

// 断言文档片段包含指定语义文本。
void requireDumpContains(const nlohmann::json &value, const std::string &needle)
{
    assert(value.dump().find(needle) != std::string::npos);
}

} // namespace

int main()
{
    const auto doc = nlohmann::json::parse(OpenApiDocumentFactory::miguDocumentJson());

    requirePath(doc, "/api/migu/singers");
    requirePath(doc, "/api/migu/singers/{source_singer_id}");
    requirePath(doc, "/api/migu/singers/{source_singer_id}/songs");
    requirePath(doc, "/api/migu/singers/{source_singer_id}/albums");
    requirePath(doc, "/api/migu/albums");
    requirePath(doc, "/api/migu/albums/{source_album_id}");
    requirePath(doc, "/api/migu/albums/{source_album_id}/songs");
    requirePath(doc, "/api/migu/songs");
    requirePath(doc, "/api/migu/songs/{source_song_id}");
    requirePath(doc, "/api/migu/resources/queue");
    requirePath(doc, "/api/migu/resources/preview");
    requirePath(doc, "/api/migu/resources/retry");
    requirePath(doc, "/api/migu/resources/reset");
    requirePath(doc, "/api/migu/resources/recover-stale");
    requirePath(doc, "/api/migu/resources/process");
    requirePath(doc, "/api/migu/dictionaries");
    requirePath(doc, "/api/migu/singer-songs/crawl-from-db/preview");
    requirePath(doc, "/api/migu/singer-albums/crawl-from-db/preview");
    requirePath(doc, "/api/migu/backfill/{type}/preview");
    requirePath(doc, "/api/migu/backfill/singer-songs/retry");
    requirePath(doc, "/api/migu/backfill/album-songs/reset");
    requirePath(doc, "/api/migu/integrity");
    requirePath(doc, "/api/migu/schema/status");
    requirePath(doc, "/api/migu/maintenance/recover-stale");
    requirePath(doc, "/api/migu/tasks/summary");
    requirePath(doc, "/api/migu/tasks/{batch_id}/rerun-failed");
    requirePath(doc, "/api/migu/tasks/{batch_id}/clone");
    requirePath(doc, "/api/migu/tasks/{batch_id}/backfill-retry");
    requirePath(doc, "/api/migu/tasks/{batch_id}/archive");
    requirePath(doc, "/api/migu/governance/events");
    requirePath(doc, "/api/migu/pacmtoken/refresh");
    requirePath(doc, "/api/migu/pacmtoken/state");
    requirePath(doc, "/api/migu/pacmtoken/current");

    const auto &item_properties = doc["components"]["schemas"]["BatchTaskItem"]["properties"];
    assert(item_properties.contains("error_category"));
    assert(item_properties.contains("result_code"));
    assert(item_properties["result_code"]["enum"].is_array());

    const auto &schemas = doc["components"]["schemas"];
    assert(schemas.contains("SingerRecord"));
    assert(schemas.contains("AlbumRecord"));
    assert(schemas.contains("SongRecord"));
    assert(schemas.contains("ResourceQueueRecord"));
    assert(schemas.contains("ResourceActionRequest"));
    assert(schemas.contains("ResourceActionItem"));
    assert(schemas.contains("ResourceActionResponse"));
    assert(schemas.contains("ResourcePreviewItem"));
    assert(schemas.contains("ResourcePreviewResponse"));
    assert(schemas.contains("ResourceProcessRequest"));
    assert(schemas.contains("ResourceProcessOptions"));
    assert(schemas.contains("ResourceProcessAcceptedResponse"));
    assert(schemas.contains("ResourceProcessItem"));
    assert(schemas.contains("ResourceProcessResponse"));
    assert(schemas.contains("SingerFromDbPreviewItem"));
    assert(schemas.contains("SingerFromDbPreviewResponse"));
    assert(schemas.contains("IntegritySummary"));
    assert(schemas.contains("BackfillActionItem"));
    assert(schemas.contains("BackfillQueueTarget"));
    assert(schemas.contains("BackfillQueueTargetPage"));
    assert(schemas.contains("BackfillPreviewResponse"));
    assert(schemas.contains("BackfillResetResponse"));
    assert(schemas.contains("DictionaryResponse"));
    assert(schemas.contains("SyncStatus"));
    assert(schemas.contains("StaleRecoveryRequest"));
    assert(schemas.contains("StaleRecoveryResponse"));
    assert(schemas.contains("SchemaStatus"));
    assert(schemas.contains("SingerRecordPage"));
    assert(schemas.contains("AlbumRecordPage"));
    assert(schemas.contains("SongRecordPage"));
    assert(schemas.contains("TaskSummary"));
    assert(schemas.contains("BatchGovernanceRequest"));
    assert(schemas.contains("BatchGovernanceResponse"));
    assert(schemas.contains("GovernanceEvent"));
    assert(schemas.contains("GovernanceEventPage"));
    assert(schemas.contains("PacmtokenRefreshRequest"));
    assert(schemas.contains("PacmtokenRefreshResponse"));
    assert(schemas.contains("PacmtokenStateResponse"));
    assert(schemas.contains("PacmtokenCurrentResponse"));
    assert(schemas["SyncStatus"]["enum"].is_array());

    const auto &singer_index_limit = schemas["SingerIndexCrawlRequest"]["properties"]["limit"];
    assert(singer_index_limit["minimum"] == 0);
    assert(!singer_index_limit.contains("maximum"));
    requireDumpContains(singer_index_limit, "all singers");

    const auto &singer_ids_limit = schemas["SingerIdsCrawlRequest"]["properties"]["limit"];
    assert(singer_ids_limit["minimum"] == 0);
    assert(singer_ids_limit["maximum"] == 500);
    assert(singer_ids_limit["default"] == 0);
    requireDumpContains(singer_ids_limit, "all items");

    const auto &integrity_properties = schemas["IntegritySummary"]["properties"];
    assert(integrity_properties.contains("singers_songs_running_stale"));
    assert(integrity_properties.contains("singers_albums_running_stale"));
    assert(integrity_properties.contains("albums_details_running_stale"));
    assert(integrity_properties.contains("albums_songs_running_stale"));
    assert(integrity_properties.contains("running_batches_stale"));
    requireDumpContains(integrity_properties["singers_without_songs"], "song_artist_refs");
    requireDumpContains(integrity_properties["singers_without_songs"], "not only songs_sync_status");
    requireDumpContains(integrity_properties["singers_without_albums"], "album_source_refs.source_artist_id");
    requireDumpContains(integrity_properties["singers_without_albums"], "not only albums_sync_status");

    const auto &overview_properties = schemas["Overview"]["properties"];
    assert(overview_properties.contains("singer_song_relations"));
    assert(overview_properties.contains("singer_album_relations"));
    assert(overview_properties.contains("album_song_relations"));

    const auto &schema_status_properties = schemas["SchemaStatus"]["properties"];
    assert(schema_status_properties.contains("schema_version"));
    assert(schema_status_properties.contains("latest_applied_version"));
    assert(schema_status_properties.contains("latest_available_version"));
    assert(schema_status_properties.contains("pending_migrations"));
    assert(schema_status_properties.contains("checksum_mismatches"));
    assert(schema_status_properties.contains("missing_tables"));
    assert(schema_status_properties.contains("missing_columns"));

    const auto &resource_properties = schemas["ResourceQueueRecord"]["properties"];
    assert(resource_properties.contains("error_category"));
    assert(resource_properties.contains("result_code"));
    assert(resource_properties.contains("last_batch_id"));
    assert(resource_properties.contains("running_batch_id"));
    assert(resource_properties.contains("last_processed_at"));

    const auto &resource_process_item_properties = schemas["ResourceProcessItem"]["properties"];
    assert(resource_process_item_properties.contains("error_category"));
    assert(resource_process_item_properties.contains("related"));
    assert(resource_process_item_properties.contains("synthetic"));
    assert(resource_process_item_properties.contains("source_content_id"));
    assert(schemas["ResourceProcessItem"]["required"].dump().find("error_category") != std::string::npos);
    assert(schemas["ResourceProcessItem"]["required"].dump().find("related") != std::string::npos);
    assert(schemas["ResourceProcessItem"]["required"].dump().find("synthetic") != std::string::npos);
    assert(schemas["ResourceProcessRequest"]["properties"].contains("async"));
    assert(schemas["ResourceProcessRequest"]["properties"].contains("options"));
    assert(schemas["ResourceProcessOptions"]["properties"].contains("resource_download_concurrency"));
    assert(schemas["ResourceProcessOptions"]["properties"].contains("pacmtoken_refresh_interval_seconds"));
    assert(schemas["ResourceProcessResponse"]["properties"].contains("synthetic_related_skipped_count"));
    assert(doc["paths"]["/api/migu/resources/process"]["post"]["responses"].contains("202"));
    assert(doc["paths"]["/api/migu/singer-songs/crawl-from-db/preview"]["post"]["responses"].contains("200"));
    assert(schemas["SingerFromDbPreviewItem"]["properties"].contains("singer_id"));
    assert(schemas["SingerFromDbPreviewItem"]["properties"].contains("source_name"));

    const auto &settings_update = schemas["SettingsUpdate"]["properties"];
    assert(settings_update.contains("resource_download_concurrency"));
    assert(settings_update.contains("resource_download_retries"));
    assert(settings_update.contains("resource_download_timeout_ms"));
    assert(settings_update.contains("pacmtoken_refresh_interval_seconds"));
    assert(settings_update.contains("auto_refresh_pacmtoken"));
    assert(settings_update.contains("refresh_pacmtoken_on_440013"));
    assert(doc["paths"]["/api/migu/settings"].contains("patch"));

    const auto &stale_recovery_properties = schemas["StaleRecoveryResponse"]["properties"];
    assert(stale_recovery_properties.contains("resources_recovered"));
    assert(schemas["StaleRecoveryRequest"].dump().find("resources") != std::string::npos);

    const auto &song_properties = schemas["SongRecord"]["properties"];
    assert(song_properties.contains("source_duration_seconds"));
    assert(!song_properties.contains("duration_seconds"));
    assert(song_properties.contains("trc_url"));

    const auto &singer_properties = schemas["SingerRecord"]["properties"];
    assert(singer_properties.contains("songs_count"));
    assert(singer_properties.contains("albums_count"));
    requireDumpContains(singer_properties["songs_sync_count"], "song_artist_refs");
    requireDumpContains(singer_properties["albums_sync_count"], "album_source_refs.source_artist_id");
    requireDumpContains(singer_properties["songs_sync_status"], "real song_artist_refs");
    requireDumpContains(singer_properties["songs_sync_status"], "completed (2)");
    requireDumpContains(singer_properties["albums_sync_status"], "album_source_refs.source_artist_id");
    requireDumpContains(singer_properties["albums_sync_status"], "completed (2)");

    const auto &album_properties = schemas["AlbumRecord"]["properties"];
    assert(album_properties.contains("songs_count"));
    assert(album_properties.contains("album_songs_count"));
    requireDumpContains(album_properties["album_songs_count"], "album_song_refs");

    const auto &backfill_target_properties = schemas["BackfillQueueTarget"]["properties"];
    requireDumpContains(backfill_target_properties["sync_status"], "same real-relation derived status as SingerRecord");
    requireDumpContains(backfill_target_properties["sync_count"], "song_artist_refs");
    requireDumpContains(backfill_target_properties["sync_count"], "album_source_refs.source_artist_id");

    const auto &batch_properties = schemas["BatchTask"]["properties"];
    assert(batch_properties.contains("archived_at"));
    assert(batch_properties.contains("archived_reason"));
    assert(batch_properties.contains("parent_batch_id"));
    assert(batch_properties.contains("display_name"));
    assert(batch_properties.contains("target_summary"));
    assert(batch_properties.contains("target_keywords"));
    assert(batch_properties.contains("error_category"));
    assert(batch_properties.contains("result_code"));

    const auto &task_properties = schemas["TaskResponse"]["properties"];
    assert(task_properties.contains("claimed_count"));
    assert(task_properties.contains("skipped_count"));
    assert(task_properties.contains("success_count"));
    assert(task_properties.contains("failed_count"));
    assert(task_properties.contains("items"));

    const auto &singer_params = doc["paths"]["/api/migu/singers"]["get"]["parameters"];
    const std::string singer_params_dump = singer_params.dump();
    assert(singer_params_dump.find("songs_sync_status") != std::string::npos);
    assert(singer_params_dump.find("albums_sync_status") != std::string::npos);
    assert(singer_params_dump.find("min_songs_count") != std::string::npos);
    assert(singer_params_dump.find("max_songs_count") != std::string::npos);
    assert(singer_params_dump.find("min_albums_count") != std::string::npos);
    assert(singer_params_dump.find("max_albums_count") != std::string::npos);
    requireDumpContains(doc["paths"]["/api/migu/singers"]["get"], "Status filters use the same exposed status");
    requireDumpContains(doc["paths"]["/api/migu/singers/{source_singer_id}"]["get"], "real relationship summary counts");
    requireDumpContains(doc["paths"]["/api/migu/integrity"]["get"], "relationship existence");

    const auto &album_params = doc["paths"]["/api/migu/albums"]["get"]["parameters"];
    const std::string album_params_dump = album_params.dump();
    assert(album_params_dump.find("min_songs_count") != std::string::npos);
    assert(album_params_dump.find("max_songs_count") != std::string::npos);
    assert(album_params_dump.find("min_album_songs_count") != std::string::npos);
    assert(album_params_dump.find("max_album_songs_count") != std::string::npos);

    const auto &task_params = doc["paths"]["/api/migu/tasks"]["get"]["parameters"];
    const std::string task_params_dump = task_params.dump();
    assert(task_params_dump.find("include_archived") != std::string::npos);
    assert(task_params_dump.find("display_name") != std::string::npos);
    assert(task_params_dump.find("target_summary") != std::string::npos);
    assert(task_params_dump.find("target_keywords") != std::string::npos);
    assert(task_params_dump.find("last_error") != std::string::npos);
    assert(task_params_dump.find("error_category") != std::string::npos);
    assert(task_params_dump.find("result_code") != std::string::npos);

    const auto &item_params = doc["paths"]["/api/migu/tasks/{batch_id}/items"]["get"]["parameters"];
    const std::string item_params_dump = item_params.dump();
    assert(item_params_dump.find("error_category") != std::string::npos);
    assert(item_params_dump.find("result_code") != std::string::npos);

    const auto &queue_response = doc["paths"]["/api/migu/backfill/singer-songs/queue"]["get"]["responses"]["200"]["content"]["application/json"]["schema"];
    assert(queue_response["$ref"] == "#/components/schemas/BackfillQueueTargetPage");
    requireDumpContains(doc["paths"]["/api/migu/backfill/singer-songs/queue"]["get"], "real song_artist_refs summary");
    requireDumpContains(doc["paths"]["/api/migu/backfill/singer-songs/queue"]["get"], "not returned by the default missing-song queue");
    requireDumpContains(doc["paths"]["/api/migu/backfill/singer-albums/queue"]["get"], "real album_source_refs.source_artist_id summary");
    requireDumpContains(doc["paths"]["/api/migu/backfill/singer-albums/queue"]["get"], "not returned by the default missing-album queue");

    const auto &error_properties = schemas["ErrorResponse"]["properties"];
    assert(error_properties.contains("error_code"));
    assert(error_properties.contains("message"));

    const auto &pacmtoken_response = schemas["PacmtokenRefreshResponse"]["properties"];
    assert(pacmtoken_response.contains("pacmtoken"));
    assert(pacmtoken_response.contains("pacmtoken_length"));
    assert(schemas["PacmtokenStateResponse"]["properties"].contains("pacmtoken_length"));
    assert(!schemas["PacmtokenStateResponse"]["properties"].contains("pacmtoken"));
    assert(schemas["PacmtokenCurrentResponse"]["properties"].contains("pacmtoken"));

    std::cout << "MiguApiDocumentTest passed." << std::endl;
    return 0;
}
