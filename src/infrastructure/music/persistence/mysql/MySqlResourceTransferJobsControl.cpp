#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceTransferJobs.h"

#include "MySqlResourceTransferJobsSupport.h"
#include "MySqlResourceProducerTasksSupport.h"

#include <algorithm>
#include <set>
#include <tuple>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {
namespace jobs = transfer_jobs;
namespace model = application::model;

boost::asio::awaitable<bool> jobs::cancelOnConnection(boost::mysql::pooled_connection &connection, std::string id, std::vector<model::ResourceDestination> destinations)
{
    if (id.empty() || id.size() > 64 || destinations.size() > 3) throw std::invalid_argument("invalid cancel");
    for (const auto destination : destinations)
        if (model::resourceDestinationName(destination).empty()) throw std::invalid_argument("invalid cancel target");
    const auto parent = co_await jobs::operation(connection, id, true);
    if (!parent) throw jobs::MissingOperation();
    auto owned_tasks = parent->tasks;
    std::vector<std::string> operation_ids {id};
    std::set<std::string> visited {id};
    std::vector<std::string> level {id};
    for (int depth = 0; depth < 2; ++depth) {
        std::vector<std::string> next;
        for (const auto &ancestor : level) {
            const auto children = co_await jobs::query(connection, "resource_transfer_operations/children", ancestor);
            if (children.rows().size() > 600) throw std::logic_error("unbounded child operations");
            for (const auto row : children.rows()) {
                const auto child_id = MySqlValueReader::stringAt(row, 0);
                if (!visited.insert(child_id).second || visited.size() > 1201) throw std::logic_error("invalid child operation graph");
                const auto child = co_await jobs::operation(connection, child_id, true);
                if (!child || child->parent_operation_id != ancestor) throw std::logic_error("child operation changed");
                owned_tasks.insert(owned_tasks.end(), child->tasks.begin(), child->tasks.end());
                if (owned_tasks.size() > 6000) throw std::logic_error("unbounded child tasks");
                operation_ids.push_back(child_id);
                next.push_back(child_id);
            }
        }
        level = std::move(next);
    }
    // 多资源先按统一身份顺序取锁，避免与另一操作的登记产生锁序反转。
    std::set<std::pair<model::ResourceOwner, std::uint64_t>> resources;
    for (const auto &item : owned_tasks)
        if (destinations.empty() || std::find(destinations.begin(), destinations.end(), item.destination) != destinations.end())
            resources.emplace(item.resource.owner, item.resource.resource_id);
    for (const auto &[owner, resource_id] : resources) (void)co_await jobs::snapshot(connection, {owner, resource_id});
    (void)co_await jobs::targets(connection);
    for (const auto &item : owned_tasks) {
        if (!destinations.empty() && std::find(destinations.begin(), destinations.end(), item.destination) == destinations.end()) continue;
        const auto actual = co_await jobs::task(connection, item.task_id);
        if (!actual) continue;
        if (actual->state == "pending" || actual->state == "retry" || actual->state == "waiting_input") {
            (void)co_await jobs::query(connection, "resource_transfer_tasks/request_cancel", actual->task_id);
            // 修复任务可能尚未认领便携带旧对象意图；取消不能释放其未决副作用槽。
            const bool uncertain = !actual->attempt_path.empty();
            (void)co_await jobs::finish(connection, *actual, uncertain ? "uncertain" : "cancelled", uncertain ? "reconcile_required" : "cancelled", uncertain ? "persistence" : "", uncertain);
        } else if (actual->state == "running")
            (void)co_await jobs::query(connection, "resource_transfer_tasks/request_cancel", actual->task_id);
    }
    for (const auto &operation_id : operation_ids) co_await producer_jobs::cancel(connection, operation_id, destinations);
    for (auto it = operation_ids.rbegin(); it != operation_ids.rend(); ++it) (void)co_await jobs::aggregate(connection, *it);
    co_return true;
}

application::result::ServiceResult<bool> MySqlResourceTransferJobs::cancel(std::string operation_id, std::vector<model::ResourceDestination> destinations)
{
    auto work = [id = std::move(operation_id), destinations = std::move(destinations)](boost::mysql::pooled_connection &connection) -> boost::asio::awaitable<bool> {
        co_return co_await jobs::cancelOnConnection(connection, id, destinations);
    };
    return jobs::run(pool_, jobs::transaction<bool>(pool_, std::move(work)), true);
}

application::result::ServiceResult<int> MySqlResourceTransferJobs::recover(model::ResourceDestination destination, int stale_seconds)
{
    auto recover = [](std::shared_ptr<MySqlConnectionPool> pool, model::ResourceDestination destination, int stale_seconds) -> boost::asio::awaitable<int> {
        if (model::resourceDestinationName(destination).empty() || stale_seconds < 1 || stale_seconds > 604800) throw std::invalid_argument("invalid stale window");
        const auto cutoff = jobs::now() > static_cast<std::uint64_t>(stale_seconds) ? jobs::now() - static_cast<std::uint64_t>(stale_seconds) : 0;
        std::vector<std::pair<std::uint64_t, std::string>> candidates;
        {
            auto connection = co_await pool->coAcquire();
            const auto rows = co_await jobs::query(connection, "resource_transfer_tasks/stale", std::string(model::resourceDestinationName(destination)), cutoff);
            for (const auto row : rows.rows()) candidates.emplace_back(MySqlValueReader::uint64At(row, 0), MySqlValueReader::stringAt(row, 1));
        }
        int recovered = 0;
        for (const auto &[id, operation_id] : candidates) {
            auto work = [id, operation_id, cutoff, destination](boost::mysql::pooled_connection &connection) -> boost::asio::awaitable<bool> {
                const auto parent = co_await jobs::operation(connection, operation_id, true);
                if (!parent) co_return false;
                const auto found = std::find_if(parent->tasks.begin(), parent->tasks.end(), [id](const auto &item) { return item.task_id == id; });
                if (found == parent->tasks.end()) co_return false;
                (void)co_await jobs::snapshot(connection, found->resource);
                (void)co_await jobs::targets(connection);
                const auto actual = co_await jobs::task(connection, id);
                if (!actual || actual->destination != destination || actual->state != "running" || actual->claimed_at > cutoff) co_return false;
                const bool uncertain = !actual->attempt_path.empty();
                const bool retry = !uncertain && !actual->cancel_requested && actual->attempt_count <= actual->policy.retries;
                const std::string state = uncertain ? "uncertain" : actual->cancel_requested ? "cancelled" :
                                                                retry                        ? "retry" :
                                                                                               "failed";
                const std::string code = uncertain ? "reconcile_required" : actual->cancel_requested ? "cancelled" :
                                                                                                       "lease_expired";
                (void)co_await jobs::finish(connection, *actual, state, code, "persistence", uncertain);
                (void)co_await jobs::query(connection, "resource_transfer_tasks/invalidate", id, actual->claim_generation);
                co_return true;
            };
            if (co_await jobs::transaction<bool>(pool, std::move(work))) ++recovered;
        }
        co_return recovered;
    };
    return jobs::run(pool_, recover(pool_, destination, stale_seconds), true);
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
