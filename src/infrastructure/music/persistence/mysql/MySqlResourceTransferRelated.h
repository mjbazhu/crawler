#pragma once

#include "MySqlResourceTransferJobsSupport.h"

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::transfer_jobs {
// 按祖先操作、同歌曲全部资源、targets的固定顺序预锁关联范围。
boost::asio::awaitable<void> lockRelatedScope(boost::mysql::pooled_connection &connection, const model::ResourceTransferJob &job, bool force_song_scope = false);
// 上传意图同事务保存合法响应的种类，修复不重新请求listen。
boost::asio::awaitable<void> recordRelated(boost::mysql::pooled_connection &connection, const model::ResourceTransferJob &job, std::uint8_t mask);
// 只有已确认父audio在完成事务内创建当前端子操作，其他端独立处理。
boost::asio::awaitable<void> enqueueRelated(boost::mysql::pooled_connection &connection, const model::ResourceTransferJob &job);
// 显式related修复复制原精确意图的发现事实，不复制已受理子操作。
boost::asio::awaitable<void> inheritRelated(boost::mysql::pooled_connection &connection, std::uint64_t old_task_id, std::uint64_t new_task_id);
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::transfer_jobs
