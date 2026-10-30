#pragma once

#include "migu/support/ResourceArchiveMySqlSupport.h"
#include "crawler/infrastructure/app/ResourceTargetVerifier.h"

#include <nlohmann/json.hpp>
#include <chrono>
#include <filesystem>
#include <iostream>
#include <string>

namespace orchestrator::modules::crawler::tests::service_rehearsal {
namespace mysql = infrastructure::music::persistence::mysql;
namespace model = application::model;
using Json = nlohmann::json;

// 记录仅包含隔离样本身份和状态，认证及来源URL不进入输出。
inline void output(const Json &value)
{
    std::cout << value.dump() << std::endl;
}

// 新库必须不存在；不允许把既有库的空业务表误认为空环境。
inline boost::asio::awaitable<bool> createDatabase(mysql::MySqlConfig config)
{
    const auto executor = co_await boost::asio::this_coro::executor;
    boost::mysql::any_connection connection(executor);
    boost::mysql::connect_params parameters;
    parameters.server_address = boost::mysql::host_and_port {config.host, config.port};
    parameters.username = config.username;
    parameters.password = config.password;
    parameters.ssl = config.ssl_mode;
    co_await connection.async_connect(parameters, boost::asio::use_awaitable);
    boost::mysql::results result;
    co_await connection.async_execute("CREATE DATABASE `" + config.database + "` CHARACTER SET utf8mb4 COLLATE utf8mb4_unicode_ci", result, boost::asio::use_awaitable);
    co_await connection.async_close(boost::asio::use_awaitable);
    co_return true;
}

// 专属连接持有命名锁；父进程关闭stdin或发送release后才释放完成回执屏障。
inline boost::asio::awaitable<bool> barrier(mysql::MySqlConfig config, std::uint64_t resource)
{
    const auto executor = co_await boost::asio::this_coro::executor;
    boost::mysql::any_connection connection(executor);
    boost::mysql::connect_params parameters;
    parameters.server_address = boost::mysql::host_and_port {config.host, config.port};
    parameters.username = config.username;
    parameters.password = config.password;
    parameters.database = config.database;
    parameters.ssl = config.ssl_mode;
    co_await connection.async_connect(parameters, boost::asio::use_awaitable);
    boost::mysql::results result;
    const auto name = "CONCAT('svc_',LEFT(SHA2(CONCAT(DATABASE(),':'," + std::to_string(resource) + "),256),48))";
    co_await connection.async_execute("SELECT GET_LOCK(" + name + ",0)", result, boost::asio::use_awaitable);
    archive::require(mysql::MySqlValueReader::uint64At(result.rows().front(), 0) == 1, "barrier already held");
    output({{"event", "barrier_ready"}, {"resource_id", resource}});
    std::string command;
    std::getline(std::cin, command);
    archive::require(command == "release", "barrier release required");
    co_await connection.async_execute("SELECT RELEASE_LOCK(" + name + ")", result, boost::asio::use_awaitable);
    archive::require(mysql::MySqlValueReader::uint64At(result.rows().front(), 0) == 1, "barrier release failed");
    co_await connection.async_close(boost::asio::use_awaitable);
    output({{"event", "barrier_released"}, {"resource_id", resource}});
    co_return true;
}
} // namespace orchestrator::modules::crawler::tests::service_rehearsal
