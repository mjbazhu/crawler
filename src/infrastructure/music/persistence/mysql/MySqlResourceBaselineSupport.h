#pragma once
#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceBaseline.h"
#include "MySqlLegacyResourceImportSupport.h"
#include "MySqlResourceTransferJobsSupport.h"
#include <nlohmann/json.hpp>
#include <array>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <string>
#include <string_view>
#include <vector>
#include <boost/asio/awaitable.hpp>
#include <boost/mysql/connection_pool.hpp>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::baseline {
namespace model = application::model;
// 维护事务内部的完整审核结果；JSON 不含路径或凭据。
struct Review {
    model::ResourceBaselineReview result;               // 可对外输出的计数和摘要
    nlohmann::json snapshot;                            // 封存的清单及目标身份
    std::array<model::LegacyResourceTarget, 3> targets; // 当前维护目标
};
// 单端补充的固定范围及本次完整覆盖计数。
struct CloudSupplement {
    std::string run_id;                   // 空时沿用原双端声明
    std::string manifest;                 // 已审阅单端清单SHA256
    std::uint64_t control_generation = 0; // 显式控制代次
    std::uint64_t expected_imported = 0;  // 应采信skip的数量
    std::uint64_t imported = 0;           // 已核对skip数量
    std::uint64_t allowed_transfers = 0;  // 已明确审阅的missing正常传输策略数
};
// 只接受同库/owner/完整高水位、当前目标且完成零冲突的补充run。
boost::asio::awaitable<std::array<CloudSupplement, 2>> readSupplements(boost::mysql::pooled_connection &connection, const model::ResourceBaselinePlan &plan, std::size_t owner_index, const Review &review, std::uint64_t high, std::uint64_t processed);
// 与原范围同页核对完整审计和本端策略，不伪造另一平台或本地skip。
boost::asio::awaitable<void> checkSupplementPage(boost::mysql::pooled_connection &connection, model::ResourceOwner owner, const std::vector<model::LegacyResourceSnapshot> &sources, std::uint64_t after, std::uint64_t end, std::array<CloudSupplement, 2> &supplements);
// 对规范 JSON 计算 SHA-256。
std::string digest(std::string_view value);
// 在同一事务检查四类完整历史范围；可写排除策略但不放行目标。
boost::asio::awaitable<Review> inspect(boost::mysql::pooled_connection &connection, const model::ResourceBaselinePlan &plan, bool block_excluded);
// 封存或激活的持久操作，边界内无外部 I/O。
boost::asio::awaitable<model::ResourceBaselineReview> seal(std::shared_ptr<MySqlConnectionPool> pool, model::ResourceBaselinePlan plan, std::string expected);
// 激活前重核固定范围，只有同一审阅事实才允许原子改变三端状态。
boost::asio::awaitable<model::ResourceBaselineReview> activate(std::shared_ptr<MySqlConnectionPool> pool, std::string id, std::string expected, std::vector<model::ResourceDestination> enabled, bool stopped);
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::baseline
