#pragma once

#include <vector>
#include <mutex>
#include <condition_variable>
#include <string>
#include <string_view>
#include <memory>
#include <sqlite3.h>

#include "crawler/infrastructure/music/persistence/sqlite/Statement.h"
#include "crawler/infrastructure/music/persistence/sqlite/StatementCache.h"

namespace orchestrator::modules::crawler::infrastructure::music::persistence::sqlite {

class ConnectionPool;

// 内部资源聚合结构：绑定连接与缓存
struct DbConnection {
    sqlite3 *db = nullptr;
    std::unique_ptr<StatementCache> cache;
    // 具体的构造与析构在 .cpp 中实现
};

/**
 * @brief 自动管理的连接句柄 (RAII)
 * 析构时自动将连接归还给所属的 ConnectionPool
 * 线程安全：本对象非线程安全，严禁跨线程共享
 */
class PooledConnection {
public:
    // 仅允许移动，禁止拷贝
    PooledConnection(DbConnection *conn, ConnectionPool &pool);
    ~PooledConnection();

    PooledConnection(const PooledConnection &) = delete;
    PooledConnection &operator=(const PooledConnection &) = delete;

    PooledConnection(PooledConnection &&other) noexcept;
    PooledConnection &operator=(PooledConnection &&other) noexcept;

    sqlite3 *get() const;

    // 新增：从当前连接的缓存中创建 Statement
    Statement createStatement(std::string_view sql);

private:
    DbConnection *conn_ = nullptr;
    ConnectionPool *pool_ = nullptr;
};

/**
 * @brief SQLite 连接池
 * 线程安全，支持阻塞式获取连接
 */
class ConnectionPool {
public:
    // 创建连接池；schema_path 指向项目 sql 目录中的 SQLite schema 文件。
    ConnectionPool(std::string_view db_path, int pool_size, std::string_view schema_path = "sql/sqlite/schema.sql");
    ~ConnectionPool();

    // 获取一个连接，若池为空则阻塞等待
    PooledConnection acquire();

    // 归还连接（通常由 PooledConnection 自动调用）
    void release(DbConnection *conn);

private:
    std::string db_path_;
    std::string schema_path_;
    int pool_size_;

    std::vector<DbConnection *> connections_; // 存储聚合资源
    std::mutex mutex_;
    std::condition_variable cv_;
    bool shutdown_ = false;
};

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::sqlite
