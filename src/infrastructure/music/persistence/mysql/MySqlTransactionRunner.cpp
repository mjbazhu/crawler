#include "crawler/infrastructure/music/persistence/mysql/MySqlTransactionRunner.h"

#include "crawler/infrastructure/music/persistence/sql/SqlFileLoader.h"

#include <boost/asio/use_awaitable.hpp>
#include <boost/mysql/results.hpp>

#include <iostream>
#include <stdexcept>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {

namespace sql_loader = orchestrator::modules::crawler::infrastructure::music::persistence::sql;

std::vector<std::string> MySqlTransactionRunner::loadStatementsOrThrow(const std::string &path, std::string_view component)
{
    auto statements = sql_loader::SqlFileLoader::splitStatements(sql_loader::SqlFileLoader::loadOrThrow(path));
    if (statements.empty()) {
        throw std::runtime_error("[" + std::string(component) + "] SQL template has no executable statements: " + path);
    }
    return statements;
}

boost::asio::awaitable<void> MySqlTransactionRunner::executeStatements(boost::mysql::pooled_connection &connection, const std::vector<std::string> &statements)
{
    boost::mysql::results result;
    for (const auto &statement : statements) {
        co_await connection->async_execute(statement, result, boost::asio::use_awaitable);
    }
}

boost::asio::awaitable<void> MySqlTransactionRunner::rollbackQuietly(boost::mysql::pooled_connection &connection, const std::vector<std::string> &rollback_statements, std::string_view component)
{
    try {
        co_await executeStatements(connection, rollback_statements);
    } catch (const std::exception &rollback_error) {
        std::cerr << "[" << component << "] rollback failed: " << rollback_error.what() << std::endl;
    }
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
