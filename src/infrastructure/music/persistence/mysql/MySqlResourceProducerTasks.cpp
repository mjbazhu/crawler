#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceProducerTasks.h"
#include "MySqlResourceProducerTasksSupport.h"
#include "MySqlLegacyResourceTransferClaim.h"

#include <algorithm>
#include <chrono>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {
namespace jobs = transfer_jobs;
namespace producers = producer_jobs;
namespace model = application::model;

MySqlResourceProducerTasks::MySqlResourceProducerTasks(std::shared_ptr<MySqlConnectionPool> pool) : pool_(std::move(pool))
{
    if (!pool_) throw std::invalid_argument("producer pool required");
}

application::result::ServiceResult<std::optional<model::ResourceProducerTask>> MySqlResourceProducerTasks::claim(model::ResourceDestination destination, std::string fingerprint, std::string adapter, std::string processor_version, int schema_version, bool key_available)
{
    auto find = [](std::shared_ptr<MySqlConnectionPool> pool, model::ResourceDestination destination, std::string fingerprint, std::string adapter, std::string version, int schema, bool key) -> boost::asio::awaitable<std::optional<model::ResourceProducerTask>> {
        if (model::resourceDestinationName(destination).empty() || !jobs::hash(fingerprint) || !model::supportsResourceTransferAdapter(destination, adapter) || version.empty() || schema < 1) throw std::invalid_argument("invalid producer worker");
        std::optional<model::ResourceProducerTask> candidate;
        {
            auto connection = co_await pool->coAcquire();
            const auto rows = co_await jobs::query(connection, "resource_producer_tasks/next", std::string(model::resourceDestinationName(destination)), key ? 1 : 0);
            if (rows.rows().empty()) co_return std::nullopt;
            candidate = co_await producers::read(connection, MySqlValueReader::uint64At(rows.rows().front(), 0), false);
        }
        if (!candidate) co_return std::nullopt;
        auto work = [value = *candidate, fingerprint, adapter, version, schema, key](boost::mysql::pooled_connection &connection) -> boost::asio::awaitable<std::optional<model::ResourceProducerTask>> {
            if (!(co_await legacyClaimAllowed(connection, value.source_job.operation_id, value.source_job.destination))) co_return std::nullopt;
            const bool valid = co_await producers::current(connection, value, false);
            const auto actual = co_await producers::read(connection, value.producer_id);
            if (!actual || (actual->state != "pending" && actual->state != "retry" && actual->state != "waiting_input")) co_return std::nullopt;
            const auto targets = co_await jobs::targets(connection);
            for (const auto &target : targets)
                if (target.state == "maintenance") co_return std::nullopt;
            if (targets.at(static_cast<std::size_t>(actual->source_job.destination)).state != "active") co_return std::nullopt;
            if (!valid) {
                const bool cancelled = actual->cancel_requested || actual->source_job.cancel_requested || actual->source_job.state == "cancelled";
                (void)co_await producers::finish(connection, *actual, cancelled ? "cancelled" : "blocked", cancelled ? "cancelled" : "producer_version_changed");
                co_return std::nullopt;
            }
            if (actual->processor_version != version || actual->schema_version != schema || actual->source_job.adapter != adapter || actual->source_job.target_fingerprint != fingerprint) {
                (void)co_await producers::finish(connection, *actual, "blocked", "producer_configuration_changed");
                co_return std::nullopt;
            }
            if (!key) {
                (void)co_await producers::finish(connection, *actual, "waiting_input", "producer_key_required");
                co_return std::nullopt;
            }
            const auto changed = co_await jobs::query(connection, "resource_producer_tasks/claim", actual->producer_id, actual->claim_generation);
            if (!changed.affected_rows()) co_return std::nullopt;
            co_return co_await producers::read(connection, actual->producer_id);
        };
        co_return co_await jobs::transaction<std::optional<model::ResourceProducerTask>>(pool, std::move(work), true);
    };
    return jobs::run(pool_, find(pool_, destination, std::move(fingerprint), std::move(adapter), std::move(processor_version), schema_version, key_available), true, std::chrono::seconds(5));
}

application::result::ServiceResult<bool> MySqlResourceProducerTasks::shouldStop(const model::ResourceProducerTask &value)
{
    auto work = [value](boost::mysql::pooled_connection &connection) -> boost::asio::awaitable<bool> { co_return !(co_await producers::current(connection, value)); };
    return jobs::run(pool_, jobs::transaction<bool>(pool_, std::move(work)), false, std::chrono::milliseconds((std::clamp)(value.policy.timeout_ms, 100, 1000)));
}

application::result::ServiceResult<std::vector<model::ResourceProducerTask>> MySqlResourceProducerTasks::list(std::string operation_id)
{
    auto read = [](std::shared_ptr<MySqlConnectionPool> pool, std::string id) -> boost::asio::awaitable<std::vector<model::ResourceProducerTask>> {
        if (id.empty() || id.size() > 64) throw std::invalid_argument("invalid producer parent");
        auto connection = co_await pool->coAcquire();
        const auto rows = co_await jobs::query(connection, "resource_producer_tasks/list", id);
        if (rows.rows().size() > 600) throw std::logic_error("unbounded producer list");
        std::vector<model::ResourceProducerTask> values;
        for (const auto row : rows.rows()) {
            const auto value = co_await producers::read(connection, MySqlValueReader::uint64At(row, 0), false);
            if (value) values.push_back(*value);
        }
        co_return values;
    };
    return jobs::run(pool_, read(pool_, std::move(operation_id)), false, std::chrono::seconds(5));
}
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
