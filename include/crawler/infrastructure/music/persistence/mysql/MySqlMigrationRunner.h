#pragma once

#include <boost/asio/awaitable.hpp>
#include <boost/mysql/connection_pool.hpp>

#include <string>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {

// MySQL migration 执行器，按 sql/mysql/migrations 中的版本文件自动升级 schema。
class MySqlMigrationRunner {
public:
    // 扫描并执行 migration；执行失败返回 false，调用方应阻止服务继续启动。
    static boost::asio::awaitable<bool> coRun(
        boost::mysql::pooled_connection &connection,
        std::string migrations_path
    );
};

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
