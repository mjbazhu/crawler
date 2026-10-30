#pragma once

#include "crawler/infrastructure/music/persistence/sql/SqlFileLoader.h"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/mysql/connection_pool.hpp>
#include <boost/mysql/pipeline.hpp>
#include <boost/mysql/results.hpp>
#include <boost/mysql/statement.hpp>

#include <array>
#include <cstdint>
#include <initializer_list>
#include <map>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::transfer_jobs {
// 仅供一个受理协程、一次连接租借使用；不跨请求或连接共享 statement。
class PreparedQueries final {
public:
    // 连接归外层事务所有，缓存不能延长或替换连接租借。
    explicit PreparedQueries(boost::mysql::pooled_connection &connection) : connection_(connection)
    {}
    PreparedQueries(const PreparedQueries &) = delete;
    PreparedQueries &operator=(const PreparedQueries &) = delete;
    PreparedQueries(PreparedQueries &&) = delete;
    PreparedQueries &operator=(PreparedQueries &&) = delete;

    // 预备不会执行 SQL；在全局目标锁前摊销多资源三端的重复 prepare。
    boost::asio::awaitable<void> prepareTransferPlan(bool process_batch)
    {
        for (const auto *path : std::array {
                 "song_resources/producer_kind", "resource_cloud_receipts/transfer_lock", "resource_baseline_blocks/find", "resource_transfer_slots/lock", "resource_transfer_tasks/latest_terminal"
             })
            (void)co_await prepare(path);
        if (process_batch) {
            for (const auto *path : {"resource_transfer_tasks/insert_process_batch", "resource_transfer_slots/insert_process_batch", "resource_cloud_receipts/ensure_process_batch"}) (void)co_await prepare(path);
        } else {
            for (const auto *path : {"resource_transfer_tasks/insert", "resource_transfer_slots/insert", "resource_cloud_receipts/transfer_ensure"}) (void)co_await prepare(path);
        }
    }

    // 仅合并原分支中必顺序执行的两条读；错误立即抛出，不使用部分结果继续登记。
    boost::asio::awaitable<std::pair<boost::mysql::results, boost::mysql::results>> slotAndLatest(std::string owner, std::uint64_t resource_id, std::string destination, std::uint64_t generation, std::uint64_t revision)
    {
        const auto slot = co_await prepare("resource_transfer_slots/lock");
        const auto latest = co_await prepare("resource_transfer_tasks/latest_terminal");
        boost::mysql::pipeline_request request;
        request.add_execute(slot, owner, resource_id, destination);
        request.add_execute(latest, owner, resource_id, destination, generation, revision);
        std::vector<boost::mysql::stage_response> responses;
        co_await connection_->async_run_pipeline(request, responses, boost::asio::use_awaitable);
        if (responses.size() != 2 || !responses[0].has_results() || !responses[1].has_results()) throw std::logic_error("invalid plan read pipeline results");
        co_return std::make_pair(std::move(responses[0]).as_results(), std::move(responses[1]).as_results());
    }

    // 参数按值属于本次协程；只复用编译后的 SQL，不缓存查询结果或绑定值。
    template <typename... Args>
    boost::asio::awaitable<boost::mysql::results> execute(std::string path, Args... args)
    {
        const auto statement = co_await prepare(std::move(path));
        boost::mysql::results rows;
        co_await connection_->async_execute(statement.bind(args...), rows, boost::asio::use_awaitable);
        co_return rows;
    }

    // 其他原有 helper 仍使用同一事务连接，不能通过缓存另借连接。
    boost::mysql::pooled_connection &connection()
    {
        return connection_;
    }

private:
    // 特殊修复路径按需预备；服务端 statement 随现有 pooled_connection 归还 reset 回收。
    // statement 析构只销毁客户端句柄，不能使用 return_without_reset 跳过回收。
    boost::asio::awaitable<boost::mysql::statement> prepare(std::string path)
    {
        if (const auto found = statements_.find(path); found != statements_.end()) co_return found->second;
        const auto text = sql::SqlFileLoader::loadOrThrow("sql/mysql/" + path + ".sql");
        auto statement = co_await connection_->async_prepare_statement(text, boost::asio::use_awaitable);
        statements_.emplace(std::move(path), statement);
        co_return statement;
    }
    boost::mysql::pooled_connection &connection_;               // 外层独占事务连接
    std::map<std::string, boost::mysql::statement> statements_; // 本次受理的固定 SQL 句柄
};
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::transfer_jobs
