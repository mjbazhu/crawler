#include "crawler/infrastructure/storage/filesystem/ResourceHistoryManifest.h"
#include "crawler/infrastructure/storage/filesystem/ResourceCloudImportManifest.h"
#include "infrastructure/storage/support/CloudTransferTestSupport.h"
#include <nlohmann/json.hpp>
#include <fstream>
#include <iostream>
#include <sstream>
#include <stdexcept>
namespace c = orchestrator::modules::crawler;
namespace test = c::tests::cloud_transfer;
namespace files = c::infrastructure::storage::filesystem;
using Json = nlohmann::json;
namespace {
// 白名单字段fixture，不包含认证或原始来源URL。
Json observation(int id, std::string path = "歌手/歌曲.flac")
{
    return {{"owner_type", "song"}, {"resource_id", id}, {"content_generation", 1}, {"source_identity", std::string(64, 'd')}, {"values_digest", std::string(64, 'e')}, {"candidate_path", std::move(path)}, {"grade", id == 2 ? "missing" : id == 3 ? "unknown" :
                                                                                                                                                                                                                                                       "observed"},
            {"result_code", "fixture"},
            {"expected_size", 3},
            {"expected_algorithm", "md5"},
            {"expected_hash", std::string(32, 'f')},
            {"observed_size", id == 2 || id == 3 ? Json(nullptr) : Json(3)},
            {"observed_algorithm", "md5"},
            {"observed_hash", std::string(32, 'f')},
            {"observed_at", 100}};
}
// 形成多页报告，验证转换与预检始终有界。
std::string report(int count = 405)
{
    const Json header {{"format", "crawler.resource-cloud-discovery.v1"}, {"definition", {{"run_id", "discovery-fixture"}, {"owner_type", "song"}, {"destination", "baidu"}, {"database_identity", std::string(64, 'a')}, {"target_revision", 1}, {"target_fingerprint", std::string(64, 'b')}}}, {"definition_hash", std::string(64, 'c')}, {"cursor", count}, {"processed", count}, {"counts", {{"observed", count - 2}, {"missing", 1}, {"unknown", 1}}}};
    std::ostringstream out;
    out << header.dump() << '\n';
    for (int i = 1; i <= count; ++i) out << observation(i).dump() << '\n';
    return out.str();
}
// 格式拒绝不应泄露原始文件内容。
template <class F>
void rejected(F work, const char *message)
{
    bool failed = false;
    try {
        work();
    } catch (const std::exception &) {
        failed = true;
    }
    test::require(failed, message);
}
} // namespace
int main()
{
    try {
        const auto original = report();
        std::istringstream input(original);
        std::ostringstream output;
        const auto prepared = files::prepareResourceHistoryManifest(input, std::string(64, '9'), "history-fixture", output);
        test::require(prepared.report_count == 405 && prepared.high_water_id == 405 && !prepared.operator_attested_destination && prepared.manifest_hash.empty(), "preview implicitly attested report or reused report SHA as manifest SHA");
        const auto manifest = output.str();
        std::istringstream parsed(manifest);
        const auto plan = files::readResourceHistoryManifest(parsed, std::string(64, '8'));
        test::require(plan.report_hash == std::string(64, '9') && plan.manifest_hash == std::string(64, '8') && plan.destination == c::application::model::ResourceDestination::Baidu, "reviewed scope or external SHA lost");
        auto page = files::readResourceHistoryPage(parsed, plan, 0);
        test::require(page && page->rows.size() == 200 && page->rows[0].action == "adopt" && page->rows[1].action == "review" && page->rows[2].action == "review", "preview adopted negative/unknown evidence");
        test::require(page->rows[0].observation.expected_algorithm == "md5" && page->rows[0].observation.expected_hash.size() == 32 && page->rows[0].observation.observed_hash.size() == 32, "legacy metadata hash was promoted to SHA256");
        page = files::readResourceHistoryPage(parsed, plan, page->next_id);
        test::require(page && page->rows.size() == 200, "second bounded history page lost");
        page = files::readResourceHistoryPage(parsed, plan, page->next_id);
        test::require(page && page->rows.size() == 5 && page->next_id == 405 && !files::readResourceHistoryPage(parsed, plan, 405), "history tail or EOF incorrect");
        std::istringstream full(manifest);
        const auto checked = files::readResourceHistoryManifest(full, std::string(64, '8'));
        files::validateResourceHistoryManifest(full, checked);
        auto selected = manifest;
        const auto at = selected.find("\"action\":\"review\"");
        test::require(at != std::string::npos, "missing decision fixture absent");
        selected.replace(at, std::string("\"action\":\"review\"").size(), "\"action\":\"allow_transfer\"");
        std::istringstream selected_input(selected);
        const auto selected_plan = files::readResourceHistoryManifest(selected_input, std::string(64, '7'));
        test::require(files::readResourceHistoryPage(selected_input, selected_plan, 0)->rows[1].action == "allow_transfer", "explicit missing review decision was lost");
        auto truncated = manifest;
        truncated.erase(truncated.rfind('\n', truncated.size() - 2) + 1);
        rejected([&] {std::istringstream in(truncated);const auto p=files::readResourceHistoryManifest(in,std::string(64,'8'));files::validateResourceHistoryManifest(in,p); }, "truncated review manifest passed full preflight");
        auto invalid = manifest;
        invalid.replace(invalid.find("\"action\":\"adopt\""), std::string("\"action\":\"adopt\"").size(), "\"action\":\"download\"");
        rejected([&] {std::istringstream in(invalid);const auto p=files::readResourceHistoryManifest(in,std::string(64,'8'));files::validateResourceHistoryManifest(in,p); }, "arbitrary action accepted");
        rejected([&] {std::istringstream in(original);(void)files::readResourceHistoryManifest(in,std::string(64,'8')); }, "raw discovery report accepted as review manifest");
        auto unsafe = original;
        const auto row = observation(1).dump();
        unsafe.replace(unsafe.find(row), row.size(), observation(1, "../outside").dump());
        rejected([&] {std::istringstream in(unsafe);std::ostringstream out;files::prepareResourceHistoryManifest(in,std::string(64,'9'),"history-fixture",out); }, "unsafe report path copied to review manifest");
        test::Root root;
        const auto source = root.path / "review.jsonl";
        {
            std::ofstream out(source, std::ios::binary);
            out << manifest;
        }
        const auto expected = files::resourceCloudManifestSha256(source);
        std::filesystem::path private_path;
        {
            files::ResourceHistoryFileSnapshot copy(source, expected);
            private_path = copy.path();
            {
                std::ofstream out(source, std::ios::binary | std::ios::trunc);
                out << "changed";
            }
            test::require(copy.sha256() == expected && files::resourceCloudManifestSha256(copy.path()) == expected, "source replacement changed validated snapshot");
        }
        test::require(!std::filesystem::exists(private_path) && std::filesystem::exists(source), "snapshot cleaned another input or leaked its private file");
        rejected([&] { files::ResourceHistoryFileSnapshot copy(source, std::string(64, '0')); }, "wrong reviewed checksum accepted");
        std::size_t files_left = 0;
        for (const auto &entry : std::filesystem::directory_iterator(root.path)) {
            (void)entry;
            ++files_left;
        }
        test::require(files_left == 1, "failed snapshot leaked private copy");
        std::cout << "PASS: bounded reviewed manifests, explicit decisions, legacy hashes, truncation/path checks and immutable file snapshots\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
