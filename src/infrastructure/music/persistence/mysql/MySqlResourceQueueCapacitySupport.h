#pragma once
#include "MySqlResourceTransferJobsSupport.h"
#include "crawler/application/model/ResourceQueueCapacity.h"
#include <string>
namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::transfer_jobs {
// 锁读当前占用，不能使用等待目标锁之前的RR快照。
boost::asio::awaitable<application::model::ResourceQueueCapacity> queueCapacity(boost::mysql::pooled_connection &connection, application::model::ResourceDestination destination, bool lock);
// 新受理与补排共享容量检查；调用方已持有目标锁。
boost::asio::awaitable<void> enforceQueueCapacity(boost::mysql::pooled_connection &connection, application::model::ResourceDestination destination, bool transfers, bool producers);
// 只检查本次新产生的活动工作，全skip/busy不因旧积压被拒绝。
boost::asio::awaitable<void> enforceOperationCapacity(boost::mysql::pooled_connection &connection, const std::string &operation_id);
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::transfer_jobs
