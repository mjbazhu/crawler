#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceCloudCleanup.h"

#include "MySqlResourceCloudCleanupCapture.h"
#include "MySqlResourceTransferJobsSupport.h"
#include <stdexcept>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {
namespace jobs = transfer_jobs;
namespace model = application::model;
MySqlResourceCloudCleanup::MySqlResourceCloudCleanup(std::shared_ptr<MySqlConnectionPool> pool) : pool_(std::move(pool))
{
    if (!pool_) throw std::invalid_argument("cleanup pool required");
}
application::result::ServiceResult<model::ResourceCloudCleanupCapture> MySqlResourceCloudCleanup::capture(std::vector<model::ResourceIdentity> resources, bool writers_stopped)
{
    auto work = [resources = std::move(resources), writers_stopped](boost::mysql::pooled_connection &connection) mutable -> boost::asio::awaitable<model::ResourceCloudCleanupCapture> {
        if (!writers_stopped) throw std::invalid_argument("explicit stopped cleanup scope required");
        co_return co_await cleanup::captureOnConnection(connection, std::move(resources), cleanup::CaptureReason::ExplicitMaintenance);
    };
    return jobs::run(pool_, jobs::transaction<model::ResourceCloudCleanupCapture>(pool_, std::move(work), true), true);
}
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
