#include "crawler/api/http/ResourceLegacyMigrationGuide.h"
#include <nlohmann/json.hpp>
#include <string>
#include <utility>
namespace orchestrator::modules::crawler::api::http {
HttpResponse resourceLegacyMigrationResponse(std::string_view path)
{
    using Json = nlohmann::json;
    Json guide = {{"supported", false}, {"equivalent", false}, {"original_semantics", "legacy_writer"}, {"reason", "writer_not_migrated"}, {"alternative", nullptr}, {"required", Json::array({"upgrade_caller", "complete_baseline_admission"})}};
    if (path == "/api/migu/resources/retry" || path == "/api/migu/resources/reset") {
        guide["original_semantics"] = path.ends_with("retry") ? "reset_selected_failed_status_without_transfer" : "reset_selected_status_and_errors_without_transfer";
        guide["reason"] = "status_reset_is_not_transfer_admission";
        guide["alternative"] = {{"type", "http"}, {"method", "POST"}, {"path", "/api/v2/migu/resources/process"}, {"action", "retry"}, {"starts_transfer", true}, {"preserves_history_skip", true}};
        guide["required"] = Json::array({"explicit_song_file_resource_ids", "explicit_destinations", "explicitly_accept_transfer_start", "idempotency_key"});
    } else if (path == "/api/migu/resources/recover-stale" || path == "/api/migu/covers/recover-stale") {
        guide["original_semantics"] = "reset_selected_legacy_stale_claims";
        guide["reason"] = "legacy_filters_do_not_identify_destination_or_prove_stopped_writers";
        guide["alternative"] = {{"type", "maintenance_cli"}, {"command", "crawler_cloud_receipts recover-tasks"}, {"starts_transfer", false}, {"may_schedule_retry", true}, {"preserves_uncertain_intents", true}};
        guide["required"] = Json::array({"explicit_database", "explicit_destination", "bounded_stale_seconds", "writers_stopped_attestation", "review_legacy_claims_before_baseline"});
    } else if (path == "/api/migu/maintenance/recover-stale") {
        guide["original_semantics"] = "recover_mixed_metadata_backfill_and_resource_claims";
        guide["reason"] = "mixed_recovery_requires_separate_metadata_and_destination_plans";
        guide["required"] = Json::array({"metadata_recovery_not_migrated", "destination_scoped_recovery", "writers_stopped_attestation"});
    } else if (path == "/api/migu/resources/process" || path == "/api/migu/covers/process") {
        guide["original_semantics"] = "legacy_resource_batch";
        guide["reason"] = "legacy_projection_not_configured";
        guide["required"] = Json::array({"configure_legacy_view_provider", "enable_required_local_target", "complete_baseline_admission"});
    } else {
        guide["reason"] = "metadata_or_governance_writer_not_migrated";
        guide["required"] = Json::array({"dedicated_metadata_writer_migration", "do_not_bypass_parallel_guard"});
    }
    const std::string message = "legacy writer requires explicit caller migration in parallel mode";
    auto response = HttpResponse::json(503, Json {{"error_code", "legacy_parallel_unsupported"}, {"message", message}, {"error", message}, {"migration", std::move(guide)}}.dump());
    response.headers["Access-Control-Allow-Origin"] = "*";
    return response;
}
} // namespace orchestrator::modules::crawler::api::http
