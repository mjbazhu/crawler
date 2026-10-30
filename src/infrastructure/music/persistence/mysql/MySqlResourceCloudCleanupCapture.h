#pragma once

#include "crawler/application/model/ResourceCloudCleanup.h"
#include <boost/asio/awaitable.hpp>
#include <boost/mysql/connection_pool.hpp>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::cleanup {
// 别名归一可废止旧本地认领；普通删除必须等待全部认领排空。
enum class CaptureReason { ExplicitMaintenance,
                           ResourceDeletion,
                           AlbumAlias };
// 所属事务内捕获；删除模式核对占用，显式治理模式要求全维护。
boost::asio::awaitable<application::model::ResourceCloudCleanupCapture> captureOnConnection(boost::mysql::pooled_connection &connection, std::vector<application::model::ResourceIdentity> resources, CaptureReason reason);
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::cleanup
