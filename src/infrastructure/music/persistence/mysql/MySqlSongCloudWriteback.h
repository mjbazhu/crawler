#pragma once

#include "crawler/application/model/SongResourceQueue.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlConnectionPool.h"
#include <boost/asio/awaitable.hpp>
#include <memory>
#include <optional>
#include <string>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::song_writeback {
// 工具与平台分离；rclone无明确平台时不能制造Google回执。
std::optional<std::string> provider(const application::model::SongResourceSuccessUpdate &update, const std::string &configured);
// 本地更新与旧工具报告在同一事务；只报告弱云端事实，不提升核验等级。
boost::asio::awaitable<bool> persist(std::shared_ptr<MySqlConnectionPool> pool, application::model::SongResourceSuccessUpdate update, std::string provider, std::string local_sql);
// 提交响应未知时读取实际本地及云端报告身份，不能只凭本地路径推定两者成功。
boost::asio::awaitable<bool> inspect(std::shared_ptr<MySqlConnectionPool> pool, application::model::SongResourceSuccessUpdate update, std::string provider);
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::song_writeback
