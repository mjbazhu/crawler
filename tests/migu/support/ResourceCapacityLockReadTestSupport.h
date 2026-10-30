#pragma once

#include "migu/support/ResourceArchiveMySqlSupport.h"
#include "../../../src/infrastructure/music/persistence/mysql/MySqlResourceTransferJobsSupport.h"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/asio/this_coro.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/mysql/connection_pool.hpp>
#include <boost/mysql/error_categories.hpp>
#include <boost/mysql/mysql_server_errc.hpp>
#include <boost/mysql/results.hpp>
#include <boost/system/system_error.hpp>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <future>
#include <initializer_list>
#include <memory>
#include <string>
#include <utility>

namespace orchestrator::modules::crawler::tests::capacity_lock_read {
namespace mysql = infrastructure::music::persistence::mysql;
namespace jobs = mysql::transfer_jobs;
namespace model = application::model;
// 两条实际连接用有界异步信号排序，不阻塞连接池的 I/O 线程。
struct Signals {
    std::atomic<bool> writer_locked {false};   // 第一连接持有目标锁
    std::atomic<bool> old_snapshot {false};    // 第二连接已建立旧 RR 快照
    std::atomic<bool> counted {false};         // 第二连接取得锁并完成当前计数
    std::atomic<bool> locks_checked {false};   // 第一连接确认基表行锁仍保留
    std::atomic<bool> reader_released {false}; // 第二连接已回滚释放锁
    std::atomic<std::uint64_t> writer_id {0};  // 实际服务器连接 ID
};
// 只在本机独立库等待本测试自己的另一个协程，失败也有截止。
inline boost::asio::awaitable<void> waitFor(const std::atomic<bool> &signal)
{
    boost::asio::steady_timer timer(co_await boost::asio::this_coro::executor);
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(10);
    while (!signal.load()) {
        archive::require(std::chrono::steady_clock::now() < deadline, "capacity lock-read handshake timeout");
        timer.expires_after(std::chrono::milliseconds(1));
        co_await timer.async_wait(boost::asio::use_awaitable);
    }
}
// SQL 只由本测试固定语句构成，输出仍为不含连接信息的断言。
inline boost::asio::awaitable<std::uint64_t> scalar(boost::mysql::pooled_connection &connection, std::string text)
{
    boost::mysql::results rows;
    co_await connection->async_execute(text, rows, boost::asio::use_awaitable);
    archive::require(rows.rows().size() == 1 && rows.rows().front().size() == 1, "capacity lock-read scalar missing");
    co_return mysql::MySqlValueReader::uint64At(rows.rows().front(), 0);
}
// 直接执行生产外置 SQL，保留与生产一致的 maximum+1 参数和当前锁读。
inline boost::asio::awaitable<std::uint64_t> boundedCount(boost::mysql::pooled_connection &connection, std::string table, std::uint64_t maximum)
{
    const auto rows = co_await jobs::query(connection, table + "/queue_occupancy_count", std::string("gdrive"), maximum + 1);
    archive::require(rows.rows().size() == 1 && rows.rows().front().size() == 1, "capacity locked count not scalar");
    co_return mysql::MySqlValueReader::uint64At(rows.rows().front(), 0);
}
// 本测试选择此前未使用的 Gdrive；零行也必须返回一行整数0。
inline boost::asio::awaitable<bool> emptyCounts(std::shared_ptr<mysql::MySqlConnectionPool> pool)
{
    auto connection = co_await pool->coAcquire();
    boost::mysql::results ignored;
    co_await connection->async_execute("START TRANSACTION", ignored, boost::asio::use_awaitable);
    for (const auto *table : {"resource_transfer_tasks", "resource_producer_tasks"})
        archive::require(co_await boundedCount(connection, table, 1) == 0, "requires unused capacity lock-read destination");
    co_await connection->async_execute("ROLLBACK", ignored, boost::asio::use_awaitable);
    co_return true;
}
// 提交新工作后用 NOWAIT 的真实3572证明行锁仍保留；回滚后同查询必须成功。
inline boost::asio::awaitable<bool> writer(std::shared_ptr<mysql::MySqlConnectionPool> pool, std::shared_ptr<Signals> signals, model::ResourceTransferRequest input)
{
    auto connection = co_await pool->coAcquire();
    boost::mysql::results rows;
    signals->writer_id.store(co_await scalar(connection, "SELECT CONNECTION_ID()"));
    co_await connection->async_execute("START TRANSACTION", rows, boost::asio::use_awaitable);
    (void)co_await jobs::targets(connection);
    signals->writer_locked.store(true);
    co_await waitFor(signals->old_snapshot);
    (void)co_await jobs::enqueueOnConnection(connection, std::move(input), archive::profiles(true));
    co_await connection->async_execute("COMMIT", rows, boost::asio::use_awaitable);
    co_await waitFor(signals->counted);
    for (const auto *table : {"resource_transfer_tasks", "resource_producer_tasks"}) {
        co_await connection->async_execute("START TRANSACTION", rows, boost::asio::use_awaitable);
        bool blocked = false;
        try {
            co_await connection->async_execute("SELECT * FROM " + std::string(table) + " WHERE destination='gdrive' FOR UPDATE NOWAIT", rows, boost::asio::use_awaitable);
        } catch (const boost::system::system_error &error) {
            blocked = error.code().category() == boost::mysql::get_mysql_server_category() && error.code().value() == boost::mysql::mysql_server_errc::er_lock_nowait;
        }
        co_await connection->async_execute("ROLLBACK", rows, boost::asio::use_awaitable);
        archive::require(blocked, "capacity count failed to retain base-row locks");
    }
    signals->locks_checked.store(true);
    co_await waitFor(signals->reader_released);
    for (const auto *table : {"resource_transfer_tasks", "resource_producer_tasks"}) {
        co_await connection->async_execute("START TRANSACTION", rows, boost::asio::use_awaitable);
        co_await connection->async_execute("SELECT * FROM " + std::string(table) + " WHERE destination='gdrive' FOR UPDATE NOWAIT", rows, boost::asio::use_awaitable);
        archive::require(rows.rows().size() == 3, "capacity row locks not released after rollback");
        co_await connection->async_execute("ROLLBACK", rows, boost::asio::use_awaitable);
    }
    co_return true;
}
// 已有 RR 普通快照保持0，但等待目标锁后的内层 FOR UPDATE 必须看到新3行。
inline boost::asio::awaitable<bool> reader(std::shared_ptr<mysql::MySqlConnectionPool> pool, std::shared_ptr<Signals> signals)
{
    co_await waitFor(signals->writer_locked);
    auto connection = co_await pool->coAcquire();
    boost::mysql::results ignored;
    archive::require(co_await scalar(connection, "SELECT CONNECTION_ID()") != signals->writer_id.load(), "capacity lock-read needs distinct connections");
    co_await connection->async_execute("SET TRANSACTION ISOLATION LEVEL REPEATABLE READ", ignored, boost::asio::use_awaitable);
    co_await connection->async_execute("START TRANSACTION", ignored, boost::asio::use_awaitable);
    for (const auto *table : {"resource_transfer_tasks", "resource_producer_tasks"})
        archive::require(co_await scalar(connection, "SELECT COUNT(*) FROM " + std::string(table) + " WHERE destination='gdrive'") == 0, "capacity prior RR snapshot not empty");
    signals->old_snapshot.store(true);
    (void)co_await jobs::targets(connection);
    for (const auto *table : {"resource_transfer_tasks", "resource_producer_tasks"}) {
        archive::require(co_await scalar(connection, "SELECT COUNT(*) FROM " + std::string(table) + " WHERE destination='gdrive'") == 0, "capacity RR snapshot unexpectedly refreshed");
        archive::require(co_await boundedCount(connection, table, 1) == 2, "capacity maximum-plus-one not saturated");
        archive::require(co_await boundedCount(connection, table, 10000) == 3, "capacity locking count used stale RR snapshot");
    }
    signals->counted.store(true);
    co_await waitFor(signals->locks_checked);
    co_await connection->async_execute("ROLLBACK", ignored, boost::asio::use_awaitable);
    signals->reader_released.store(true);
    co_return true;
}
// 嵌入既有独立容量测试尾部，不改已有容量/producer回归断言。
inline void verify(const std::shared_ptr<mysql::MySqlConnectionPool> &pool)
{
    archive::require(migu::support::runOnExecutor(pool->executor(), emptyCounts(pool), "capacity zero count"), "capacity zero count failed");
    auto input = archive::request(model::ResourceOwner::Song, archive::mrc(pool, 9000), "capacity-rr-current-read", {model::ResourceDestination::Gdrive});
    input.resources.push_back({model::ResourceOwner::Song, archive::mrc(pool, 9001)});
    input.resources.push_back({model::ResourceOwner::Song, archive::mrc(pool, 9002)});
    auto signals = std::make_shared<Signals>();
    auto writing = std::async(std::launch::async, [=] { return migu::support::runOnExecutor(pool->executor(), writer(pool, signals, input), "capacity current-read writer"); });
    auto reading = std::async(std::launch::async, [=] { return migu::support::runOnExecutor(pool->executor(), reader(pool, signals), "capacity current-read reader"); });
    archive::require(writing.get() && reading.get(), "capacity locked scalar count regression failed");
}
} // namespace orchestrator::modules::crawler::tests::capacity_lock_read
