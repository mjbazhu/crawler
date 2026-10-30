#pragma once
#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceHistoryAdoption.h"
#include "MySqlResourceDiscoverySupport.h"
#include "MySqlLegacyResourceImportSupport.h"
#include <nlohmann/json.hpp>
#include <string>
namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::history {
namespace model = application::model;
// 校验不可变范围及格式，不执行副作用。
void validate(const model::ResourceHistoryAdoptionPlan &plan);
// 构造共用backfill主键，策略类型进一步防止ID碰撞。
std::string key(const model::ResourceHistoryAdoptionPlan &plan);
// 读取并验证清单绑定，可选行锁。
boost::asio::awaitable<model::ResourceHistoryAdoptionProgress> read(boost::mysql::pooled_connection &connection, const model::ResourceHistoryAdoptionPlan &plan, bool lock);
// 验证库、目标和原发现任务，要求维护状态时锁定目标。
boost::asio::awaitable<void> scope(boost::mysql::pooled_connection &connection, const model::ResourceHistoryAdoptionPlan &plan, bool maintained);
// 单行仅改变选定端策略；返回false表示来源/占用/证据冲突。
boost::asio::awaitable<bool> adopt(boost::mysql::pooled_connection &connection, const model::ResourceHistoryAdoptionPlan &plan, const model::ResourceHistoryAdoptionRow &row, const model::LegacyResourceSnapshot &source);
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::history
