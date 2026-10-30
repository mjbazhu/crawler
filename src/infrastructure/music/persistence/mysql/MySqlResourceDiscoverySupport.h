#pragma once
#include "crawler/application/model/ResourceDiscovery.h"
#include "MySqlResourceTransferJobsSupport.h"
#include <nlohmann/json.hpp>
#include <optional>
#include <string>
namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::discovery {
namespace model = application::model;
// 规范定义/观察的唯一序列化，不传递原始远端响应。
nlohmann::json definitionJson(const model::ResourceDiscoveryDefinition &value);
model::ResourceDiscoveryDefinition definitionFromJson(const nlohmann::json &value);
nlohmann::json observationJson(const model::ResourceDiscoveryObservation &value);
model::ResourceDiscoveryObservation observationFromJson(const nlohmann::json &value);
// SHA256固定同ID定义，不能重用到别的范围。
std::string digest(const nlohmann::json &value);
// 空计数固定所有证据等级。
std::map<std::string, std::uint64_t> emptyCounts();
// 可选锁读控制行，不持有连接逃逸。
boost::asio::awaitable<std::optional<model::ResourceDiscoveryRun>> read(boost::mysql::pooled_connection &connection, const std::string &id, bool lock);
// 认领、状态、定义与截止全部匹配才允许写页。
bool owns(const model::ResourceDiscoveryRun &actual, const model::ResourceDiscoveryRun &lease);
// 目标与库身份围栏；锁序为目标再run，不访问源站。
boost::asio::awaitable<void> target(boost::mysql::pooled_connection &connection, const model::ResourceDiscoveryDefinition &definition, bool lock);
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::discovery
