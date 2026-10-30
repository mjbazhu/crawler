#pragma once

#include "MySqlResourceTransferJobsSupport.h"

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {
// 在任何operation锁之前调用；独立额外平台及v2操作不受旧并发设置约束。
boost::asio::awaitable<bool> legacyClaimAllowed(boost::mysql::pooled_connection &connection, const std::string &operation_id, application::model::ResourceDestination destination);
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
