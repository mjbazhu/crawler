#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceTransferQueries.h"
#include "MySqlResourceReceiptRead.h"

#include <nlohmann/json.hpp>
#include <chrono>
#include <stdexcept>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {
namespace model = application::model;
namespace jobs = transfer_jobs;
application::result::ServiceResult<model::ResourceStatusBatch> MySqlResourceTransferQueries::status(model::ResourceStatusQuery query)
{
    auto read = [query = std::move(query)](boost::mysql::pooled_connection &connection) mutable -> boost::asio::awaitable<model::ResourceStatusBatch> {
        model::normalizeResourceStatusQuery(query);
        auto wanted = nlohmann::json::array();
        for (const auto &identity : query.resources) wanted.push_back({{"owner_type", model::resourceOwnerName(identity.owner)}, {"resource_id", identity.resource_id}});
        const auto rows = co_await jobs::query(connection, "resource_cloud_receipts/status_batch", wanted.dump());
        if (rows.rows().size() != query.resources.size() * 3) throw std::logic_error("incomplete resource status snapshot");
        model::ResourceStatusBatch result;
        result.requested_targets = query.destinations;
        result.items.resize(query.resources.size());
        for (const auto row : rows.rows()) {
            const auto ordinal = MySqlValueReader::uint64At(row, 30);
            if (!ordinal || ordinal > result.items.size()) throw std::logic_error("invalid status ordinal");
            auto &item = result.items[ordinal - 1];
            item.resource = query.resources[ordinal - 1];
            item.found = MySqlValueReader::uint64At(row, 31) > 0;
            auto &local = item.local;
            local.status = MySqlValueReader::intAt(row, 32);
            local.confirmed = {MySqlValueReader::uint64At(row, 26), MySqlValueReader::uint64At(row, 27)};
            local.current_version = {MySqlValueReader::uint64At(row, 16), MySqlValueReader::uint64At(row, 28)};
            local.size_bytes = MySqlValueReader::uint64At(row, 29);
            local.checksum = MySqlValueReader::stringAt(row, 25);
            local.path_resolved = MySqlValueReader::uint64At(row, 33) != 0;
            const auto checksum = model::parseLegacyResourceChecksum(local.checksum);
            const auto reference = MySqlValueReader::stringAt(row, 18);
            local.current = item.found && local.current_version.target_revision > 0 && local.confirmed == local.current_version && local.path_resolved && jobs::hash(reference) && checksum.sha256 == reference && local.size_bytes > 0 && local.size_bytes == MySqlValueReader::uint64At(row, 19);
            local.transfer_skipped = item.found && local.current_version.target_revision > 0 && MySqlValueReader::uint64At(row, 34) == local.current_version.content_generation && MySqlValueReader::uint64At(row, 35) == local.current_version.target_revision && !MySqlValueReader::stringAt(row, 36).empty() && !MySqlValueReader::stringAt(row, 37).empty();
            if (!row[0].is_null()) item.cloud_receipts.push_back(resource_queries::mapReceipt(row));
        }
        co_return result;
    };
    return jobs::run(pool_, jobs::transaction<model::ResourceStatusBatch>(pool_, std::move(read)), false, std::chrono::seconds(10));
}
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
