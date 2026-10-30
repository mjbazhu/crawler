#include "ResourceArchiveCliSupport.h"
#include "crawler/infrastructure/storage/filesystem/ResourceCloudImportManifest.h"
#include <filesystem>
#include <fstream>
#include <iostream>
namespace orchestrator::modules::crawler::infrastructure::app::archive_cli {
nlohmann::json runJson(const model::ResourceDiscoveryRun &run)
{
    const auto &d = run.definition;
    return {{"definition", {{"run_id", d.run_id}, {"owner_type", model::resourceOwnerName(d.owner)}, {"destination", model::resourceDestinationName(d.destination)}, {"database_identity", d.database_identity}, {"target_revision", d.target_revision}, {"target_fingerprint", d.target_fingerprint}, {"high_water_id", d.high_water_id}, {"path_source", d.path_source}, {"strip_prefix", d.strip_prefix}, {"page_size", d.page_size}, {"concurrency", d.concurrency}, {"object_timeout_ms", d.object_timeout_ms}}}, {"definition_hash", run.definition_hash}, {"state", run.state}, {"cursor", run.cursor}, {"claim_generation", run.claim_generation}, {"lease_until", run.lease_until}, {"processed", run.processed}, {"counts", run.counts}, {"result_code", run.result_code}, {"review_required", true}, {"content_requests", 0}, {"uploads", 0}, {"policies_changed", 0}};
}
int exportReport(music::persistence::mysql::MySqlResourceDiscovery &runs, const model::ResourceDiscoveryRun &run, const std::string &directory)
{
    // 原子独占新目录：已有目录或文件一律拒绝，不覆盖其他报告。
    const auto root = std::filesystem::u8path(directory);
    if (!std::filesystem::create_directory(root)) throw std::invalid_argument("report directory already exists");
    const auto partial = root / "report.jsonl.part";
    const auto output = root / "report.jsonl";
    std::ofstream stream(partial, std::ios::binary);
    if (!stream) throw std::runtime_error("report output cannot be created");
    auto header = runJson(run);
    header["format"] = "crawler.resource-cloud-discovery.v1";
    stream << header.dump() << '\n';
    std::uint64_t after = 0, count = 0;
    std::map<std::string, std::uint64_t> counts;
    for (const auto &[grade, ignored] : run.counts) counts[grade] = 0;
    for (;;) {
        const auto page = get(runs.observations(run.definition.run_id, after, run.cursor, 500));
        for (const auto &o : page) {
            if (o.resource.resource_id <= after || o.resource.resource_id > run.cursor) throw std::runtime_error("report cursor mismatch");
            const nlohmann::json value {{"owner_type", model::resourceOwnerName(o.resource.owner)}, {"resource_id", o.resource.resource_id}, {"content_generation", o.generation}, {"source_identity", o.source_identity}, {"values_digest", o.values_digest}, {"candidate_path", o.candidate_path}, {"grade", o.grade}, {"result_code", o.result_code}, {"expected_size", o.expected_size}, {"expected_algorithm", o.expected_algorithm}, {"expected_hash", o.expected_hash}, {"observed_size", o.observed_size ? nlohmann::json(*o.observed_size) : nlohmann::json(nullptr)}, {"observed_algorithm", o.observed_algorithm}, {"observed_hash", o.observed_hash}, {"observed_at", o.observed_at}};
            stream << value.dump() << '\n';
            after = o.resource.resource_id;
            ++count;
            ++counts.at(o.grade);
        }
        if (!stream) throw std::runtime_error("report write failed");
        if (page.size() < 500) break;
    }
    if (count != run.processed || counts != run.counts) throw std::runtime_error("report committed count mismatch");
    stream.close();
    if (!stream) throw std::runtime_error("report flush failed");
    std::filesystem::rename(partial, output);
    std::cout << nlohmann::json {{"report_sha256", storage::filesystem::resourceCloudManifestSha256(output)}, {"cursor", run.cursor}, {"observations", count}, {"review_required", true}, {"policies_changed", 0}, {"uploads", 0}, {"content_requests", 0}}.dump() << '\n';
    return 0;
}
} // namespace orchestrator::modules::crawler::infrastructure::app::archive_cli
