#include "ResourceRecoveryCli.h"

#include "crawler/application/model/ResourceTransfer.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceTransferJobs.h"

#include <charconv>
#include <iostream>
#include <set>
#include <stdexcept>
#include <system_error>
#include <utility>

#include <nlohmann/json.hpp>

namespace orchestrator::modules::crawler::infrastructure::app {

int resourceRecoveryCommand(const std::map<std::string, std::string> &options, std::shared_ptr<music::persistence::mysql::MySqlConnectionPool> pool)
{
    const std::set<std::string> expected {"--database", "--destination", "--stale-seconds", "--writers-stopped"};
    if (options.size() != expected.size()) throw std::invalid_argument("recover-tasks requires database, destination, stale-seconds and writers-stopped");
    for (const auto &[key, value] : options)
        if (!expected.contains(key) || value.empty()) throw std::invalid_argument("invalid recover-tasks option");
    if (options.at("--writers-stopped") != "true") throw std::invalid_argument("recovery requires all writers stopped");
    const auto destination = application::model::parseResourceDestination(options.at("--destination"));
    if (!destination) throw std::invalid_argument("invalid recovery destination");
    const auto &window = options.at("--stale-seconds");
    int stale_seconds = 0;
    const auto parsed = std::from_chars(window.data(), window.data() + window.size(), stale_seconds);
    if (parsed.ec != std::errc {} || parsed.ptr != window.data() + window.size() || stale_seconds < 1 || stale_seconds > 604800) throw std::invalid_argument("stale-seconds must be within 1..604800");
    music::persistence::mysql::MySqlResourceTransferJobs repository(std::move(pool));
    const auto result = repository.recover(*destination, stale_seconds);
    if (!result.isSuccess()) throw std::invalid_argument(result.message());
    std::cout << nlohmann::json {{"destination", application::model::resourceDestinationName(*destination)}, {"recovered_tasks", *result.data()}, {"page_limit", 200}, {"may_have_more", *result.data() == 200}, {"content_requests", 0}, {"uploads", 0}}.dump() << '\n';
    return 0;
}

} // namespace orchestrator::modules::crawler::infrastructure::app
