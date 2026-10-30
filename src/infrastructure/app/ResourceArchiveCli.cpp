#include "ResourceArchiveCli.h"
#include "ResourceArchiveCliSupport.h"
#include "crawler/application/use_cases/ResourceDiscoveryWorkflow.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlLegacyResourceImport.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceQueueCapacity.h"
#include <iostream>
#include <utility>
namespace orchestrator::modules::crawler::infrastructure::app {
namespace mysql = music::persistence::mysql;
namespace model = application::model;
namespace {
// 单端容量与有界占用（超限时为下界）同时输出。
nlohmann::json capacity(const model::ResourceQueueCapacity &v)
{
    return {{"destination", model::resourceDestinationName(v.destination)}, {"version", v.version}, {"max_transfers", v.transfer_limit}, {"max_producers", v.producer_limit}, {"transfers", v.transfers}, {"producers", v.producers}, {"transfers_exact", v.transfers_exact}, {"producers_exact", v.producers_exact}};
}
} // namespace
int resourceArchiveCommand(std::string_view command, const std::map<std::string, std::string> &args, std::shared_ptr<mysql::MySqlConnectionPool> pool)
{
    using namespace archive_cli;
    if (command == "queue-status") {
        allowed(args, {"--database"});
        nlohmann::json values = nlohmann::json::array();
        for (const auto &v : get(mysql::MySqlResourceQueueCapacity(pool).status())) values.push_back(capacity(v));
        std::cout << values.dump() << '\n';
        return 0;
    }
    if (command == "queue-limits") {
        allowed(args, {"--database", "--destination", "--expected-version", "--max-transfers", "--max-producers"});
        const auto dest = model::parseResourceDestination(required(args, "--destination"));
        if (!dest) throw std::invalid_argument("invalid destination");
        const auto value = get(mysql::MySqlResourceQueueCapacity(pool).configure(*dest, number(required(args, "--expected-version")), number(required(args, "--max-transfers")), number(required(args, "--max-producers"))));
        std::cout << capacity(value).dump() << '\n';
        return 0;
    }
    mysql::MySqlResourceDiscovery runs(pool);
    mysql::MySqlLegacyResourceImport sources(pool);
    const auto &id = required(args, "--run-id");
    if (command == "discover-start") {
        allowed(args, {"--database", "--run-id", "--owner", "--destination", "--path-source", "--strip-prefix", "--page-size", "--concurrency", "--timeout-ms"});
        model::ResourceDiscoveryDefinition d;
        d.run_id = id;
        const auto owner = model::parseResourceOwner(required(args, "--owner"));
        const auto dest = model::parseResourceDestination(required(args, "--destination"));
        if (!owner || !dest) throw std::invalid_argument("invalid discovery scope");
        d.owner = *owner;
        d.destination = *dest;
        if (args.contains("--path-source")) d.path_source = args.at("--path-source");
        if (args.contains("--strip-prefix")) d.strip_prefix = args.at("--strip-prefix");
        d.page_size = bounded(args, "--page-size", 50, 200);
        d.concurrency = bounded(args, "--concurrency", 4, 8);
        d.object_timeout_ms = bounded(args, "--timeout-ms", 5000, 10000);
        model::validateResourceDiscoveryDefinition(d);
        // 同ID重放保留原高水位，不把新增资源偷偷并入原范围。
        const auto previous = runs.find(id);
        if (!previous.isSuccess() && previous.error() != application::result::ServiceErrorType::NotFound) throw std::invalid_argument(previous.message());
        const auto page = get(sources.preview(d.owner, 0, previous.isSuccess() ? previous.data()->definition.high_water_id : 0, 1));
        d.database_identity = page.database_identity;
        d.high_water_id = previous.isSuccess() ? previous.data()->definition.high_water_id : page.high_water_id;
        const auto &target = page.targets.at(static_cast<std::size_t>(d.destination));
        d.target_revision = target.revision;
        d.target_fingerprint = target.fingerprint;
        std::cout << runJson(get(runs.start(std::move(d)))).dump() << '\n';
        return 0;
    }
    if (command == "discover-export") {
        allowed(args, {"--database", "--run-id", "--output-dir"});
        return exportReport(runs, get(runs.find(id)), required(args, "--output-dir"));
    }
    if (command == "discover-run") {
        allowed(args, {"--database", "--run-id", "--pages"});
        auto current = get(runs.find(id));
        const auto pages = bounded(args, "--pages", 1, 1000);
        if (current.state == "running") {
            auto remote = probe(current.definition.destination);
            application::use_cases::ResourceDiscoveryWorkflow workflow(runs, sources, *remote);
            for (int i = 0; i < pages && current.state == "running"; ++i) {
                const auto before = current.cursor;
                current = get(workflow.step(id));
                if (current.cursor == before) break;
            }
        }
        std::cout << runJson(current).dump() << '\n';
        return 0;
    }
    allowed(args, {"--database", "--run-id"});
    if (command == "discover-status")
        std::cout << runJson(get(runs.find(id))).dump() << '\n';
    else if (command == "discover-pause" || command == "discover-resume" || command == "discover-cancel")
        std::cout << runJson(get(runs.control(id, std::string(command.substr(9))))).dump() << '\n';
    else
        throw std::invalid_argument("unknown archive command");
    return 0;
}
} // namespace orchestrator::modules::crawler::infrastructure::app
