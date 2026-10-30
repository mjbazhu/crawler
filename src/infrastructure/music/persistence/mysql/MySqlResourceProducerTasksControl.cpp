#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceProducerTasks.h"
#include "MySqlResourceQueueCapacitySupport.h"
#include "MySqlResourceProducerTasksSupport.h"

#include <chrono>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {
namespace jobs = transfer_jobs;
namespace producers = producer_jobs;
namespace model = application::model;

application::result::ServiceResult<int> MySqlResourceProducerTasks::recover(model::ResourceDestination destination, int stale_seconds)
{
    auto recover = [](std::shared_ptr<MySqlConnectionPool> pool, model::ResourceDestination destination, int stale_seconds) -> boost::asio::awaitable<int> {
        if (model::resourceDestinationName(destination).empty() || stale_seconds < 1 || stale_seconds > 604800) throw std::invalid_argument("invalid producer recovery window");
        const auto cutoff = jobs::now() > static_cast<std::uint64_t>(stale_seconds) ? jobs::now() - static_cast<std::uint64_t>(stale_seconds) : 0;
        std::vector<model::ResourceProducerTask> values;
        {
            auto connection = co_await pool->coAcquire();
            const auto rows = co_await jobs::query(connection, "resource_producer_tasks/stale", std::string(model::resourceDestinationName(destination)), cutoff);
            for (const auto row : rows.rows()) {
                const auto value = co_await producers::read(connection, MySqlValueReader::uint64At(row, 0), false);
                if (value) values.push_back(*value);
            }
        }
        int recovered = 0;
        for (const auto &value : values) {
            auto work = [value, cutoff](boost::mysql::pooled_connection &connection) -> boost::asio::awaitable<bool> {
                const bool valid = co_await producers::current(connection, value);
                const auto actual = co_await producers::read(connection, value.producer_id);
                if (!actual || actual->state != "running" || actual->claimed_at > cutoff || actual->claim_generation != value.claim_generation || jobs::now() < actual->claimed_at + static_cast<std::uint64_t>((actual->policy.timeout_ms + 999) / 1000) + 1) co_return false;
                const bool cancelled = actual->cancel_requested || actual->source_job.cancel_requested || actual->source_job.state == "cancelled";
                const bool retry = valid && !cancelled && actual->attempt_count <= actual->policy.retries;
                (void)co_await producers::finish(connection, *actual, cancelled ? "cancelled" : !valid ? "blocked" :
                                                                                            retry      ? "retry" :
                                                                                                         "failed",
                                                 cancelled ? "cancelled" : "producer_lease_expired");
                (void)co_await jobs::query(connection, "resource_producer_tasks/invalidate", actual->producer_id, actual->claim_generation);
                co_return true;
            };
            if (co_await jobs::transaction<bool>(pool, std::move(work))) ++recovered;
        }
        co_return recovered;
    };
    return jobs::run(pool_, recover(pool_, destination, stale_seconds), true, std::chrono::seconds(15));
}

application::result::ServiceResult<bool> MySqlResourceProducerTasks::retry(std::uint64_t id)
{
    auto retry = [](std::shared_ptr<MySqlConnectionPool> pool, std::uint64_t id) -> boost::asio::awaitable<bool> {
        if (!id) throw std::invalid_argument("invalid producer identity");
        std::optional<model::ResourceProducerTask> value;
        {
            auto connection = co_await pool->coAcquire();
            value = co_await producers::read(connection, id, false);
        }
        if (!value) co_return false;
        auto work = [value = *value](boost::mysql::pooled_connection &connection) -> boost::asio::awaitable<bool> {
            if (!(co_await producers::current(connection, value, false))) co_return false;
            const auto actual = co_await producers::read(connection, value.producer_id);
            if (!actual) co_return false;
            const auto changed = co_await jobs::query(connection, "resource_producer_tasks/retry", actual->producer_id, actual->claim_generation);
            if (changed.affected_rows() == 1) co_await jobs::enforceQueueCapacity(connection, actual->source_job.destination, false, true);
            (void)co_await jobs::aggregate(connection, actual->source_job.operation_id);
            co_return changed.affected_rows() == 1;
        };
        co_return co_await jobs::transaction<bool>(pool, std::move(work));
    };
    return jobs::run(pool_, retry(pool_, id), true, std::chrono::seconds(5));
}
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
