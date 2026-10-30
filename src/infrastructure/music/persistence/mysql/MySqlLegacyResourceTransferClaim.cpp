#include "MySqlLegacyResourceTransferClaim.h"

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {
boost::asio::awaitable<bool> legacyClaimAllowed(boost::mysql::pooled_connection &connection, const std::string &operation_id, application::model::ResourceDestination destination)
{
    const auto name = std::string(application::model::resourceDestinationName(destination));
    const auto matched = co_await transfer_jobs::query(connection, "resource_legacy_batches/claim_batch", operation_id);
    if (matched.rows().empty()) co_return true;
    const auto row = matched.rows().front();
    const auto id = MySqlValueReader::stringAt(row, 0);
    const auto owner = MySqlValueReader::stringAt(row, 1), provider = MySqlValueReader::stringAt(row, 2);
    if (name != "local" && (owner == "song" || name != provider)) co_return true;
    const auto locked = co_await transfer_jobs::query(connection, "resource_legacy_batches/claim_lock", id);
    if (locked.rows().empty()) co_return false;
    const auto limit = MySqlValueReader::uint64At(locked.rows().front(), 0);
    const auto running = co_await transfer_jobs::query(connection, "resource_legacy_batches/claim_running", id, owner, provider);
    co_return !running.rows().empty() && MySqlValueReader::uint64At(running.rows().front(), 0) < limit;
}
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
