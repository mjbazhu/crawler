#pragma once

#include <boost/asio/awaitable.hpp>
#include <boost/mysql/connection_pool.hpp>

#include <string>
#include <string_view>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {

// MySQL 事务 SQL 辅助器，集中处理多语句模板加载和顺序执行。
class MySqlTransactionRunner {
public:
    // 加载可包含多条语句的 SQL 模板，空模板视为配置错误。
    static std::vector<std::string> loadStatementsOrThrow(const std::string &path, std::string_view component);

    // 顺序执行一组 SQL 语句，调用方负责事务边界。
    static boost::asio::awaitable<void> executeStatements(boost::mysql::pooled_connection &connection, const std::vector<std::string> &statements);

    // 尝试执行回滚语句，回滚失败只记录日志。
    static boost::asio::awaitable<void> rollbackQuietly(boost::mysql::pooled_connection &connection, const std::vector<std::string> &rollback_statements, std::string_view component);
};

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
