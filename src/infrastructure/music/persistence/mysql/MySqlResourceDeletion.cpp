#include "MySqlResourceDeletion.h"

#include "MySqlResourceCloudCleanupCapture.h"
#include "MySqlResourceTransferJobsSupport.h"
#include <stdexcept>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::cleanup {
namespace jobs = transfer_jobs;
namespace model = application::model;

boost::asio::awaitable<void> captureResourceTree(boost::mysql::pooled_connection &connection, std::vector<model::ResourceIdentity> resources)
{
    const auto parents = resources;
    for (const auto resource : parents) {
        if (resource.owner != model::ResourceOwner::Song) continue;
        const auto rows = co_await jobs::query(connection, "song_resource_artifacts/delete_scope", resource.resource_id);
        for (const auto row : rows.rows()) resources.push_back({model::ResourceOwner::SongArtifact, MySqlValueReader::uint64At(row, 0)});
        if (resources.size() > 10000) throw std::invalid_argument("resource deletion scope exceeds bound");
    }
    if (!resources.empty()) (void)co_await captureOnConnection(connection, std::move(resources), CaptureReason::ResourceDeletion);
}

boost::asio::awaitable<bool> deleteEntity(std::shared_ptr<MySqlConnectionPool> pool, model::ResourceOwner owner, std::uint64_t id, std::string delete_sql)
{
    auto work = [owner, id, delete_sql = std::move(delete_sql)](boost::mysql::pooled_connection &connection) -> boost::asio::awaitable<bool> {
        if (!id || (owner != model::ResourceOwner::Song && owner != model::ResourceOwner::Singer && owner != model::ResourceOwner::Album)) throw std::invalid_argument("invalid parent deletion");
        const auto table = owner == model::ResourceOwner::Song ? "songs" : owner == model::ResourceOwner::Singer ? "singers" :
                                                                                                                   "albums";
        const auto locked = co_await jobs::query(connection, std::string(table) + "/delete_lock", id);
        if (locked.rows().empty()) co_return false;
        const auto sources = co_await jobs::query(connection, std::string(table) + "/delete_sources", id);
        if (sources.rows().size() > 10000) throw std::invalid_argument("parent deletion scope exceeds bound");
        std::vector<model::ResourceIdentity> resources;
        for (const auto source : sources.rows()) {
            const auto rows = co_await jobs::query(connection, jobs::table(owner) + "/delete_scope", MySqlValueReader::uint64At(source, 0));
            for (const auto row : rows.rows()) resources.push_back({owner, MySqlValueReader::uint64At(row, 0)});
            if (resources.size() > 10000) throw std::invalid_argument("resource deletion scope exceeds bound");
        }
        co_await captureResourceTree(connection, std::move(resources));
        const auto statement = co_await connection->async_prepare_statement(delete_sql, boost::asio::use_awaitable);
        boost::mysql::results result;
        co_await connection->async_execute(statement.bind(id), result, boost::asio::use_awaitable);
        co_return result.affected_rows() != 0;
    };
    co_return co_await jobs::transaction<bool>(std::move(pool), std::move(work), true);
}
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::cleanup
