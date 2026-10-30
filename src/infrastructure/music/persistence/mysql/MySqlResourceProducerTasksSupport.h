#pragma once

#include "MySqlResourceTransferJobsSupport.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceProducerTasks.h"

#include <boost/asio/awaitable.hpp>
#include <boost/mysql/connection_pool.hpp>
#include <cstdint>
#include <optional>
#include <string>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::producer_jobs {
namespace model = application::model;
// 调用方先锁真实父操作、资源和目标；本函数只读取/锁 producer 行及原父 task。
boost::asio::awaitable<std::optional<model::ResourceProducerTask>> read(boost::mysql::pooled_connection &connection, std::uint64_t id, bool lock = true);
// 完整版本/目标/取消边界，可用于认领前检查而不要求 producer 已 running。
boost::asio::awaitable<bool> current(boost::mysql::pooled_connection &connection, const model::ResourceProducerTask &value, bool running = true);
// enqueue 原事务中登记缺少的计划；有效 skip 和 busy 永远不生成。
boost::asio::awaitable<void> registerPlans(boost::mysql::pooled_connection &connection, const std::string &operation_id, const std::vector<model::ResourceTransferTargetProfile> &profiles);
// 结束或暂存本次认领，旧 worker 不得改变新认领结果。
boost::asio::awaitable<bool> finish(boost::mysql::pooled_connection &connection, const model::ResourceProducerTask &value, std::string state, std::string code, std::string category = {}, std::string child = {});
// 取消已锁定父操作下匹配目标的所有生成计划，包括父传输已成功的情况。
boost::asio::awaitable<void> cancel(boost::mysql::pooled_connection &connection, const std::string &operation_id, const std::vector<model::ResourceDestination> &destinations);
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::producer_jobs
