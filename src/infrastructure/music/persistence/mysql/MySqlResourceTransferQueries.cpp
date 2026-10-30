#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceTransferQueries.h"

#include "MySqlResourceTransferJobsSupport.h"
#include "MySqlResourceReceiptRead.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMusicCatalogQueryRepository.h"

#include <algorithm>
#include <stdexcept>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {
namespace model = application::model;
namespace result = application::result;
namespace jobs = transfer_jobs;
namespace resource_queries {
// 从同一行快照计算事实，不把弱证据或离线配置当成远端可用。
model::ResourceCloudReceiptView mapReceipt(boost::mysql::row_view row)
{
    model::ResourceCloudReceiptView value;
    value.id = MySqlValueReader::uint64At(row, 0);
    const auto owner = model::parseResourceOwner(MySqlValueReader::stringAt(row, 1));
    const auto provider = model::parseCloudProvider(MySqlValueReader::stringAt(row, 3));
    if (!owner || !provider) throw std::logic_error("invalid persisted resource receipt");
    value.resource = {*owner, MySqlValueReader::uint64At(row, 2)};
    value.provider = *provider;
    value.adapter = MySqlValueReader::stringAt(row, 4);
    value.task_state = MySqlValueReader::stringAt(row, 5);
    value.confirmed = {MySqlValueReader::uint64At(row, 6), MySqlValueReader::uint64At(row, 7)};
    value.size_bytes = MySqlValueReader::uint64At(row, 8);
    value.checksum = MySqlValueReader::stringAt(row, 9);
    value.verification = MySqlValueReader::stringAt(row, 10);
    value.verification_method = MySqlValueReader::stringAt(row, 11);
    if (!row[12].is_null()) value.uploaded_at = MySqlValueReader::uint64At(row, 12);
    if (!row[13].is_null()) value.verified_at = MySqlValueReader::uint64At(row, 13);
    value.result_code = MySqlValueReader::stringAt(row, 14);
    value.path_resolved = MySqlValueReader::uint64At(row, 15) != 0;
    value.current_version = {MySqlValueReader::uint64At(row, 16), MySqlValueReader::uint64At(row, 17)};
    const auto reference = MySqlValueReader::stringAt(row, 18);
    value.current = value.current_version.content_generation > 0 && value.current_version.target_revision > 0 && value.confirmed == value.current_version && value.path_resolved && jobs::hash(reference) && value.checksum == reference && value.size_bytes == MySqlValueReader::uint64At(row, 19) && (value.verification == "sha256_verified" || value.verification == "hash_matched");
    value.transfer_skipped = MySqlValueReader::stringAt(row, 20) == "skip" && MySqlValueReader::uint64At(row, 21) == value.current_version.content_generation && MySqlValueReader::uint64At(row, 22) == value.current_version.target_revision && !MySqlValueReader::stringAt(row, 23).empty() && !MySqlValueReader::stringAt(row, 24).empty();
    value.assumed_stored = value.verification == "operator_attested";
    const auto local = model::parseLegacyResourceChecksum(MySqlValueReader::stringAt(row, 25));
    if (!local.sha256.empty() && MySqlValueReader::uint64At(row, 26) == value.current_version.content_generation && MySqlValueReader::uint64At(row, 27) == MySqlValueReader::uint64At(row, 28) && jobs::hash(value.checksum))
        value.matches_local = local.sha256 == value.checksum && MySqlValueReader::uint64At(row, 29) == value.size_bytes;
    return value;
}
} // namespace resource_queries

MySqlResourceTransferQueries::MySqlResourceTransferQueries(std::shared_ptr<MySqlConnectionPool> pool) : pool_(std::move(pool))
{
    if (!pool_) throw std::invalid_argument("resource transfer query pool required");
}

result::ServiceResult<bool> MySqlResourceTransferQueries::compatible()
{
    try {
        return result::ServiceResult<bool>::success(MySqlMusicCatalogQueryRepository(pool_).schemaStatus().ok);
    } catch (const std::exception &) {
        return result::ServiceResult<bool>::failure(result::ServiceErrorType::InternalError, "resource_schema_unavailable");
    }
}

result::ServiceResult<model::ResourceCloudReceiptPage> MySqlResourceTransferQueries::receipts(model::ResourceCloudReceiptQuery query)
{
    auto read = [query = std::move(query)](boost::mysql::pooled_connection &connection) -> boost::asio::awaitable<model::ResourceCloudReceiptPage> {
        const auto owner = std::string(model::resourceOwnerName(query.owner));
        if (owner.empty() || query.limit < 1 || query.limit > 200 || (query.resource_id && !*query.resource_id) || (query.provider && (*query.provider == model::ResourceDestination::Local || model::resourceDestinationName(*query.provider).empty()))) throw std::invalid_argument("invalid receipt query");
        const auto known = [](const std::string &text, std::initializer_list<std::string_view> values) { return text.empty() || std::find(values.begin(), values.end(), text) != values.end(); };
        if (!known(query.task_state, {"pending", "running", "retry", "succeeded", "failed", "cancelled", "uncertain", "blocked", "skipped"}) || !known(query.verification, {"unverified", "operator_attested", "object_observed", "hash_matched", "sha256_verified", "conflict"})) throw std::invalid_argument("invalid receipt filter");
        const auto provider = query.provider ? std::string(model::resourceDestinationName(*query.provider)) : "";
        const auto rows = co_await jobs::query(connection, "resource_cloud_receipts/api_page", owner, query.resource_id.value_or(0), query.resource_id.value_or(0), provider, provider, query.task_state, query.task_state, query.verification, query.verification, query.after_id, query.limit + 1);
        model::ResourceCloudReceiptPage page;
        for (const auto row : rows.rows()) {
            if (page.items.size() == static_cast<std::size_t>(query.limit)) {
                page.next_after_id = page.items.back().id;
                break;
            }
            page.items.push_back(resource_queries::mapReceipt(row));
        }
        co_return page;
    };
    return jobs::run(pool_, jobs::transaction<model::ResourceCloudReceiptPage>(pool_, std::move(read)), false, std::chrono::seconds(10));
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
