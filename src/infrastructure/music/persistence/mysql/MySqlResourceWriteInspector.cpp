#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceWriteInspector.h"
#include "MySqlResourceTransferJobsSupport.h"
#include <utility>
namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {
MySqlResourceWriteInspector::MySqlResourceWriteInspector(std::shared_ptr<MySqlConnectionPool> pool) : pool_(std::move(pool))
{}
std::optional<application::model::ResourceWriteDisposition> MySqlResourceWriteInspector::inspect(std::string owner, std::uint64_t resource, std::uint64_t generation, std::string path)
{
    using State = application::model::ResourceWriteDisposition;
    auto work = [](std::shared_ptr<MySqlConnectionPool> pool, std::string owner, std::uint64_t resource, std::uint64_t generation, std::string path) -> boost::asio::awaitable<std::optional<State>> {
        const auto identity = application::model::parseResourceOwner(owner);
        if (!identity || path.empty()) co_return std::nullopt;
        auto connection = co_await pool->coAcquire();
        const auto current = co_await transfer_jobs::query(connection, transfer_jobs::table(*identity) + "/transfer_write_receipt", resource);
        if (!current.rows().empty()) {
            const auto row = current.rows().front();
            if (MySqlValueReader::stringAt(row, 2) == path) co_return State::Committed;
            if (MySqlValueReader::intAt(row, 0) == 1 && MySqlValueReader::uint64At(row, 1) == generation) co_return State::Active;
        }
        const auto active = co_await transfer_jobs::query(connection, "resource_transfer_tasks/active_write", owner, resource, path, generation);
        if (!active.rows().empty()) co_return State::Active;
        // 原身份可能被删除/合并；只有没有任何当前引用才允许回收。
        const auto referenced = co_await transfer_jobs::query(connection, "resource_transfer_tasks/referenced_write", path, path, path, path);
        co_return !referenced.rows().empty() ? State::Committed : State::Discard;
    };
    const auto checked = transfer_jobs::run(pool_, work(pool_, std::move(owner), resource, generation, std::move(path)));
    return checked.isSuccess() ? *checked.data() : std::nullopt;
}
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
