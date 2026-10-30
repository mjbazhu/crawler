#pragma once

#include <boost/asio/awaitable.hpp>
#include <boost/mysql/connection_pool.hpp>

#include <cstdint>
#include <string>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::migration_recovery {
// 仅识别冻结V2910完整DDL已提交但记账中断；半成品和定义冲突一律拒绝。
boost::asio::awaitable<bool> reconcileCompletedDdl(boost::mysql::pooled_connection &connection, std::uint64_t version, std::string checksum);
// 同步已校验迁移链的组件版本；未知更高组件版本不回退，写后再次核对。
boost::asio::awaitable<void> synchronizeComponent(boost::mysql::pooled_connection &connection, std::uint64_t version, std::string name);
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::migration_recovery
