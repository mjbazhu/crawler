#include "ResourceBaselineCli.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceBaseline.h"
#include <nlohmann/json.hpp>
#include <iostream>
#include <array>
#include <cstddef>
#include <set>
#include <stdexcept>

namespace orchestrator::modules::crawler::infrastructure::app {
int resourceBaselineCommand(std::string_view command, const std::map<std::string, std::string> &args, std::shared_ptr<music::persistence::mysql::MySqlConnectionPool> pool)
{
    namespace model = application::model;
    const bool maintain = command == "baseline-maintenance";
    const bool activate = command == "baseline-activate";
    std::set<std::string> allowed = activate ? std::set<std::string> {"--database", "--baseline-id", "--sha256", "--destinations", "--writers-stopped"} : maintain ? std::set<std::string> {"--database", "--baseline-id", "--sha256", "--writers-stopped"} :
                                                                                                                                                                     std::set<std::string> {"--database", "--baseline-id", "--song-run", "--singer-run", "--album-run", "--artifact-run", "--sha256", "--writers-stopped"};
    if (!activate && !maintain) {
        for (const auto owner : {"song", "singer", "album", "artifact"})
            for (const auto provider : {"baidu", "gdrive"}) allowed.insert("--" + std::string(owner) + "-" + provider + "-run");
    }
    for (const auto &[key, unused] : args)
        if (!allowed.contains(key)) throw std::invalid_argument("unsupported baseline option");
    const auto required = [&](const std::string &key) -> std::string { const auto found=args.find(key); if(found==args.end() || found->second.empty()) throw std::invalid_argument("missing baseline option"); return found->second; };
    music::persistence::mysql::MySqlResourceBaseline repository(std::move(pool));
    application::result::ServiceResult<model::ResourceBaselineReview> result = application::result::ServiceResult<model::ResourceBaselineReview>::failure(application::result::ServiceErrorType::InvalidParam, "invalid baseline command");
    if (maintain) {
        result = repository.maintenance(required("--baseline-id"), required("--sha256"), args.contains("--writers-stopped"));
    } else if (activate) {
        std::vector<model::ResourceDestination> destinations;
        const auto names = required("--destinations");
        std::size_t start = 0;
        do {
            const auto end = names.find(',', start);
            const auto name = names.substr(start, end == std::string::npos ? end : end - start);
            const auto parsed = model::parseResourceDestination(name);
            if (!parsed) throw std::invalid_argument("invalid baseline destination");
            destinations.push_back(*parsed);
            if (end == std::string::npos) break;
            start = end + 1;
        } while (start <= names.size());
        result = repository.activate(required("--baseline-id"), required("--sha256"), std::move(destinations), args.contains("--writers-stopped"));
    } else {
        model::ResourceBaselinePlan plan {required("--baseline-id"), {required("--song-run"), required("--singer-run"), required("--album-run"), required("--artifact-run")}, args.contains("--writers-stopped")};
        const std::array<std::string, 4> owners {"song", "singer", "album", "artifact"};
        const std::array<std::string, 2> providers {"baidu", "gdrive"};
        for (std::size_t owner = 0; owner < owners.size(); ++owner)
            for (std::size_t provider = 0; provider < providers.size(); ++provider) {
                const auto key = "--" + owners[owner] + "-" + providers[provider] + "-run";
                if (args.contains(key)) plan.cloud_supplement_runs[owner][provider] = required(key);
            }
        if (command == "baseline-review")
            result = repository.review(plan);
        else if (command == "baseline-seal")
            result = repository.seal(plan, required("--sha256"));
    }
    if (!result.isSuccess()) throw std::invalid_argument(result.message());
    const auto &value = *result.data();
    std::cout << nlohmann::json {{"baseline_sha256", value.digest}, {"resources", value.resources}, {"blocked_resources", value.blocked_resources}, {"state", value.state}, {"content_requests", 0}, {"uploads", 0}}.dump() << '\n';
    return 0;
}
} // namespace orchestrator::modules::crawler::infrastructure::app
