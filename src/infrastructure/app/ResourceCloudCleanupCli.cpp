#include "ResourceCloudCleanupCli.h"
#include "ResourceArchiveCliSupport.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceCloudCleanup.h"
#include <iostream>
#include <optional>
#include <vector>
#include <cstdint>
#include <cstddef>
#include <stdexcept>
#include <utility>
#include <nlohmann/json.hpp>
namespace orchestrator::modules::crawler::infrastructure::app {
int resourceCloudCleanupCommand(std::string_view command, const std::map<std::string, std::string> &args, std::shared_ptr<music::persistence::mysql::MySqlConnectionPool> pool)
{
    using namespace archive_cli;
    music::persistence::mysql::MySqlResourceCloudCleanup cleanup(std::move(pool));
    if (command == "cleanup-capture") {
        allowed(args, {"--database", "--owner", "--resource-ids", "--writers-stopped"});
        const auto owner = model::parseResourceOwner(required(args, "--owner"));
        if (!owner) throw std::invalid_argument("invalid cleanup owner");
        if (!args.contains("--writers-stopped")) throw std::invalid_argument("cleanup capture requires stopped writers attestation");
        const auto &ids = required(args, "--resource-ids");
        if (ids.size() > 4200) throw std::invalid_argument("cleanup scope exceeds bound");
        std::vector<model::ResourceIdentity> identities;
        std::size_t begin = 0;
        for (;;) {
            const auto end = ids.find(',', begin);
            const auto id = number(ids.substr(begin, end == std::string::npos ? end : end - begin));
            if (!id || identities.size() >= 200) throw std::invalid_argument("cleanup identities exceed bound");
            identities.push_back({*owner, id});
            if (end == std::string::npos) break;
            begin = end + 1;
        }
        const auto captured = get(cleanup.capture(std::move(identities), true));
        std::cout << nlohmann::json {{"candidate_ids", captured.candidate_ids}, {"inserted", captured.inserted}, {"resources_missing", captured.resources_missing}, {"resources_without_receipts", captured.resources_without_receipts}, {"evidence_complete", false}, {"remote_delete_allowed", false}, {"resource_deletions", 0}, {"remote_deletions", 0}, {"source_content_requests", 0}}.dump() << '\n';
        return 0;
    }
    if (command != "cleanup-preview" && command != "cleanup-status") throw std::invalid_argument("unknown cleanup command");
    allowed(args, {"--database", "--provider", "--after-id", "--limit"});
    std::optional<model::ResourceDestination> provider;
    if (args.contains("--provider")) {
        provider = model::parseCloudProvider(args.at("--provider"));
        if (!provider) throw std::invalid_argument("invalid cleanup provider");
    }
    const auto after = args.contains("--after-id") ? number(args.at("--after-id")) : 0;
    const auto limit = bounded(args, "--limit", 50, 200);
    const auto page = get(cleanup.preview(provider, after, static_cast<std::size_t>(limit)));
    auto items = nlohmann::json::array();
    std::map<std::string, std::size_t> counts;
    for (const auto &c : page.items) {
        ++counts[c.result_code];
        items.push_back({{"candidate_id", c.id}, {"receipt_id", c.receipt_id}, {"owner_type", model::resourceOwnerName(c.resource.owner)}, {"resource_id", c.resource.resource_id}, {"provider", model::resourceDestinationName(c.provider)}, {"content_generation", c.content_generation}, {"target_revision", c.target_revision}, {"target_fingerprint", c.target_fingerprint}, {"cloud_path", c.cloud_path}, {"path_sha256", c.path_hash}, {"size_bytes", c.size_bytes}, {"checksum", c.checksum}, {"verification", c.verification}, {"captured_at", c.captured_at}, {"result_code", c.result_code}, {"online_verification_required", true}});
    }
    std::cout << nlohmann::json {{"items", std::move(items)}, {"counts", counts}, {"next_after_id", page.next_after_id}, {"may_have_more", page.may_have_more}, {"dry_run", true}, {"evidence_complete", false}, {"remote_delete_allowed", false}, {"resource_deletions", 0}, {"remote_deletions", 0}, {"source_content_requests", 0}}.dump() << '\n';
    return 0;
}
} // namespace orchestrator::modules::crawler::infrastructure::app
