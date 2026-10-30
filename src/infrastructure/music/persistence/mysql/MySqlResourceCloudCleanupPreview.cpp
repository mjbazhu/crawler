#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceCloudCleanup.h"
#include "MySqlResourceTransferJobsSupport.h"
#include <stdexcept>
#include <utility>
namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {
namespace jobs = transfer_jobs;
namespace model = application::model;
application::result::ServiceResult<model::ResourceCloudCleanupPage> MySqlResourceCloudCleanup::preview(std::optional<model::ResourceDestination> provider, std::uint64_t after, std::size_t limit)
{
    auto work = [provider, after, limit](boost::mysql::pooled_connection &connection) -> boost::asio::awaitable<model::ResourceCloudCleanupPage> {
        if (!limit || limit > 200 || (provider && *provider != model::ResourceDestination::Baidu && *provider != model::ResourceDestination::Gdrive)) throw std::invalid_argument("invalid cleanup page");
        const auto selected = provider ? std::string(model::resourceDestinationName(*provider)) : std::string {};
        const auto rows = co_await jobs::query(connection, "resource_cloud_cleanup_candidates/preview", after, selected, selected, static_cast<std::uint64_t>(limit));
        model::ResourceCloudCleanupPage page;
        page.next_after_id = after;
        for (const auto row : rows.rows()) {
            model::ResourceCloudCleanupCandidate c;
            const auto owner = model::parseResourceOwner(MySqlValueReader::stringAt(row, 2));
            const auto dest = model::parseCloudProvider(MySqlValueReader::stringAt(row, 5));
            if (!owner || !dest) throw std::logic_error("invalid cleanup identity");
            c.id = MySqlValueReader::uint64At(row, 0);
            c.receipt_id = MySqlValueReader::uint64At(row, 1);
            c.resource = {*owner, MySqlValueReader::uint64At(row, 3)};
            c.content_generation = MySqlValueReader::uint64At(row, 4);
            c.provider = *dest;
            c.target_revision = MySqlValueReader::uint64At(row, 6);
            c.target_fingerprint = MySqlValueReader::stringAt(row, 7);
            c.confirmed_content_generation = MySqlValueReader::uint64At(row, 8);
            c.confirmed_target_revision = MySqlValueReader::uint64At(row, 9);
            c.cloud_path = MySqlValueReader::stringAt(row, 10);
            c.path_hash = MySqlValueReader::stringAt(row, 11);
            c.object_key = MySqlValueReader::stringAt(row, 12);
            c.size_bytes = MySqlValueReader::uint64At(row, 13);
            c.checksum = MySqlValueReader::stringAt(row, 14);
            c.verification = MySqlValueReader::stringAt(row, 15);
            c.receipt_state = MySqlValueReader::stringAt(row, 16);
            c.captured_at = MySqlValueReader::uint64At(row, 17);
            model::ResourceCloudCleanupFacts facts;
            facts.target_matches = jobs::hash(c.target_fingerprint) && c.target_revision == MySqlValueReader::uint64At(row, 18) && c.target_fingerprint == MySqlValueReader::stringAt(row, 19);
            facts.source_exists = MySqlValueReader::intAt(row, 20) != 0;
            facts.maintenance = MySqlValueReader::intAt(row, 21) != 0;
            facts.shared_reference = MySqlValueReader::intAt(row, 22) != 0;
            facts.active_intent = MySqlValueReader::intAt(row, 23) != 0;
            facts.alias_reference = MySqlValueReader::intAt(row, 24) != 0;
            c.result_code = model::classifyResourceCloudCleanup(c, facts);
            // 无效旧路径可能携带敏感URL，只保留其摘要，不回显原始值。
            if (!model::safeResourceCloudCleanupPath(c.cloud_path)) c.cloud_path.clear();
            page.next_after_id = c.id;
            page.items.push_back(std::move(c));
        }
        page.may_have_more = page.items.size() == limit;
        co_return page;
    };
    return jobs::run(pool_, jobs::transaction<model::ResourceCloudCleanupPage>(pool_, std::move(work)));
}
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
