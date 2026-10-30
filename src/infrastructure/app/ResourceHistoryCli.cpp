#include "ResourceHistoryCli.h"
#include "ResourceArchiveCliSupport.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceHistoryAdoption.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlLegacyResourceImport.h"
#include "crawler/infrastructure/storage/filesystem/ResourceHistoryManifest.h"
#include "crawler/infrastructure/storage/filesystem/ResourceCloudImportManifest.h"
#include <filesystem>
#include <fstream>
#include <iostream>
#include <utility>
namespace orchestrator::modules::crawler::infrastructure::app {
namespace {
namespace files = storage::filesystem;
namespace mysql = music::persistence::mysql;
namespace model = application::model;
using Json = nlohmann::json;
using namespace archive_cli;
// 原双端导入和新单端导入明确返回独立控制状态。
Json progressJson(const model::ResourceHistoryAdoptionProgress &v)
{
    return {{"state", v.state}, {"control_state", v.control_state}, {"control_generation", v.control_generation}, {"cursor", v.cursor}, {"processed", v.processed}, {"accepted", v.imported}, {"conflicts", v.conflicts}, {"content_requests", 0}, {"uploads", 0}, {"baseline_activated", false}};
}
// 输出独立清单，失败不把.part伪装成可apply的完整文件。
int preview(const Options &args)
{
    allowed(args, {"--database", "--report", "--report-sha256", "--run-id", "--output-dir"});
    files::ResourceHistoryFileSnapshot copy(std::filesystem::u8path(required(args, "--report")), required(args, "--report-sha256"));
    const auto directory = std::filesystem::u8path(required(args, "--output-dir"));
    if (!std::filesystem::create_directory(directory)) throw std::invalid_argument("history output directory exists");
    const auto partial = directory / "manifest.jsonl.part", output = directory / "manifest.jsonl";
    std::ifstream input(copy.path(), std::ios::binary);
    std::ofstream manifest(partial, std::ios::binary);
    if (!input || !manifest) throw std::runtime_error("history preview file unavailable");
    const auto plan = files::prepareResourceHistoryManifest(input, copy.sha256(), required(args, "--run-id"), manifest);
    manifest.close();
    if (!manifest) throw std::runtime_error("history preview flush failed");
    std::filesystem::rename(partial, output);
    std::cout << Json {{"manifest_sha256", files::resourceCloudManifestSha256(output)}, {"report_sha256", plan.report_hash}, {"destination", model::resourceDestinationName(plan.destination)}, {"resources", plan.report_count}, {"review_required", true}, {"policies_changed", 0}, {"content_requests", 0}, {"uploads", 0}}.dump() << '\n';
    return 0;
}
// 控制旧双端声明run，清单身份、scope和预期控制代次同时绑定。
int importControl(const Options &args, std::shared_ptr<mysql::MySqlConnectionPool> pool)
{
    allowed(args, {"--database", "--manifest", "--sha256", "--action", "--expected-generation"});
    files::ResourceHistoryFileSnapshot copy(std::filesystem::u8path(required(args, "--manifest")), required(args, "--sha256"));
    std::ifstream input(copy.path(), std::ios::binary);
    auto header = files::readResourceCloudManifestHeader(input, copy.sha256());
    header.plan.control_generation = number(required(args, "--expected-generation"));
    const auto value = get(mysql::MySqlLegacyResourceImport(std::move(pool)).control(header.plan, header.scope, required(args, "--action")));
    std::cout << Json {{"state", value.state}, {"control_state", value.control_state}, {"control_generation", value.control_generation}, {"cursor", value.cursor}, {"processed", value.processed_resources}, {"accepted", value.imported_resources}, {"conflicts", value.conflicts}, {"content_requests", 0}, {"uploads", 0}}.dump() << '\n';
    return 0;
}
} // namespace
int resourceHistoryCommand(std::string_view command, const std::map<std::string, std::string> &args, std::shared_ptr<music::persistence::mysql::MySqlConnectionPool> pool)
{
    using namespace archive_cli;
    if (command == "history-preview") return preview(args);
    if (command == "import-control") return importControl(args, std::move(pool));
    const bool apply = command == "history-apply", control = command == "history-control";
    if (apply)
        allowed(args, {"--database", "--manifest", "--sha256", "--attest-destination", "--writers-stopped"});
    else if (control)
        allowed(args, {"--database", "--manifest", "--sha256", "--action", "--expected-generation"});
    else if (command == "history-status")
        allowed(args, {"--database", "--manifest", "--sha256"});
    else
        throw std::invalid_argument("unknown history command");
    files::ResourceHistoryFileSnapshot copy(std::filesystem::u8path(required(args, "--manifest")), required(args, "--sha256"));
    std::ifstream input(copy.path(), std::ios::binary);
    auto plan = files::readResourceHistoryManifest(input, copy.sha256());
    // 在任何写入之前验证完整副本，内存始终受单页/单行约束。
    files::validateResourceHistoryManifest(input, plan);
    input.close();
    mysql::MySqlResourceHistoryAdoption repository(std::move(pool));
    auto status = get(repository.progress(plan));
    if (control) {
        plan.control_generation = number(required(args, "--expected-generation"));
        status = get(repository.control(plan, required(args, "--action")));
    } else if (apply) {
        const auto destination = model::parseCloudProvider(required(args, "--attest-destination"));
        if (!destination || *destination != plan.destination || !args.contains("--writers-stopped")) throw std::invalid_argument("history destination attestation and stopped writers required");
        if (status.control_state != "active") throw std::invalid_argument("history adoption is paused or cancelled");
        plan.operator_attested_destination = true;
        plan.writers_stopped = true;
        plan.control_generation = status.control_generation;
        const auto execution_generation = plan.control_generation;
        std::ifstream pages(copy.path(), std::ios::binary);
        (void)files::readResourceHistoryManifest(pages, copy.sha256());
        std::uint64_t after = 0;
        bool any = false;
        while (const auto page = files::readResourceHistoryPage(pages, plan, after)) {
            any = true;
            after = page->next_id;
            if (page->next_id > status.cursor) {
                status = get(repository.apply(plan, *page));
                if (status.control_state != "active" || status.control_generation != execution_generation) throw std::invalid_argument("history execution control changed");
            }
        }
        if (!any && plan.high_water_id == 0 && status.state == "not_started") status = get(repository.apply(plan, {}));
        status = get(repository.progress(plan));
    }
    std::cout << progressJson(status).dump() << '\n';
    return control ? 0 : status.conflicts ? 2 :
                                            0;
}
} // namespace orchestrator::modules::crawler::infrastructure::app
