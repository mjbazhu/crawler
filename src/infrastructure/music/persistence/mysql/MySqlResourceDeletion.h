#pragma once

#include "crawler/application/model/ResourceTransfer.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlConnectionPool.h"
#include <boost/asio/awaitable.hpp>
#include <boost/mysql/connection_pool.hpp>
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::cleanup {
// 同一事务捕获文件及其派生产物；不提交、不读文件、不调用网盘。
boost::asio::awaitable<void> captureResourceTree(boost::mysql::pooled_connection &connection, std::vector<application::model::ResourceIdentity> resources);
// 同一事务锁定来源及资源，捕获回执后删除父实体。
boost::asio::awaitable<bool> deleteEntity(std::shared_ptr<MySqlConnectionPool> pool, application::model::ResourceOwner owner, std::uint64_t id, std::string delete_sql);
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::cleanup
