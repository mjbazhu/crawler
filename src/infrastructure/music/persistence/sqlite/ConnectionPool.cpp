#include "crawler/infrastructure/music/persistence/sqlite/ConnectionPool.h"

#include "crawler/infrastructure/music/persistence/sqlite/SchemaManager.h"

#include <sqlite3.h>

#include <iostream>
#include <stdexcept>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::sqlite {

// ==========================================
// PooledConnection Implementation
// ==========================================

PooledConnection::PooledConnection(DbConnection *conn, ConnectionPool &pool) : conn_(conn), pool_(&pool)
{
}

PooledConnection::~PooledConnection()
{
    if (conn_ && pool_) {
        pool_->release(conn_);
    }
}

PooledConnection::PooledConnection(PooledConnection &&other) noexcept
    : conn_(other.conn_), pool_(other.pool_)
{
    other.conn_ = nullptr;
    other.pool_ = nullptr;
}

PooledConnection &PooledConnection::operator=(PooledConnection &&other) noexcept
{
    if (this != &other) {
        if (conn_ && pool_) {
            pool_->release(conn_);
        }
        conn_ = other.conn_;
        pool_ = other.pool_;
        other.conn_ = nullptr;
        other.pool_ = nullptr;
    }
    return *this;
}

sqlite3 *PooledConnection::get() const
{
    return conn_ ? conn_->db : nullptr;
}

Statement PooledConnection::createStatement(std::string_view sql)
{
    if (!conn_ || !conn_->db || !conn_->cache) {
        throw std::runtime_error("[PooledConnection] Connection is invalid");
    }

    // 从缓存获取 stmt (Lazy Loading)
    sqlite3_stmt *stmt = conn_->cache->get(sql);
    if (!stmt) {
        throw std::runtime_error("[PooledConnection] Failed to create statement");
    }

    // 返回借用模式的 Statement (不拥有 stmt 所有权)
    return Statement(conn_->db, stmt);
}

// ==========================================
// ConnectionPool Implementation
// ==========================================

ConnectionPool::ConnectionPool(std::string_view db_path, int pool_size, std::string_view schema_path) : db_path_(db_path),
                                                                                                        schema_path_(schema_path),
                                                                                                        pool_size_(pool_size)
{
    connections_.reserve(pool_size_);

    for (int i = 0; i < pool_size_; ++i) {
        sqlite3 *db = nullptr;
        // 使用 SQLITE_OPEN_NOMUTEX 模式，因为连接由池分配给单线程独占使用
        // 配合 WAL 模式实现高并发
        int rc = sqlite3_open_v2(
            db_path_.c_str(),
            &db,
            SQLITE_OPEN_READWRITE | SQLITE_OPEN_CREATE | SQLITE_OPEN_NOMUTEX,
            nullptr
        );

        if (rc != SQLITE_OK) {
            std::string err_msg = db ? sqlite3_errmsg(db) : "Unknown error";
            if (db) sqlite3_close(db);
            throw std::runtime_error("[ConnectionPool] Failed to open database: " + err_msg);
        }

        // 配置 WAL 模式 (Write-Ahead Logging)
        char *errMsg = nullptr;
        rc = sqlite3_exec(db, "PRAGMA journal_mode=WAL;", nullptr, nullptr, &errMsg);
        if (rc != SQLITE_OK) {
            std::string err = errMsg ? errMsg : "Unknown";
            sqlite3_free(errMsg);
            sqlite3_close(db);
            throw std::runtime_error("[ConnectionPool] Failed to set WAL mode: " + err);
        }

        // 设置忙等待超时，防止 SQLITE_BUSY 错误 (5000ms)
        sqlite3_busy_timeout(db, 5000);

        // 使用第一个连接初始化数据库表结构
        if (i == 0) {
            if (!SchemaManager::initialize(db, schema_path_)) {
                sqlite3_close(db);
                throw std::runtime_error("[ConnectionPool] Failed to initialize database schema");
            }
        }

        // 创建聚合资源对象
        auto conn = new DbConnection();
        conn->db = db;
        conn->cache = std::make_unique<StatementCache>(db);

        connections_.push_back(conn);
    }
}

ConnectionPool::~ConnectionPool()
{
    {
        std::unique_lock<std::mutex> lock(mutex_);
        shutdown_ = true;
        cv_.notify_all(); // 唤醒所有等待获取连接的线程

        // 等待所有活跃连接归还，防止悬垂指针
        cv_.wait(lock, [this] { return connections_.size() == static_cast<size_t>(pool_size_); });
    }

    // 关闭所有连接
    for (DbConnection *conn : connections_) {
        // 必须先销毁 StatementCache (finalize statements)
        conn->cache.reset();
        // 再关闭数据库连接
        sqlite3_close(conn->db);
        delete conn;
    }
    connections_.clear();
}

PooledConnection ConnectionPool::acquire()
{
    std::unique_lock<std::mutex> lock(mutex_);
    // 等待直到有可用连接或池关闭
    cv_.wait(lock, [this] { return !connections_.empty() || shutdown_; });

    if (shutdown_ || connections_.empty()) {
        throw std::runtime_error("[ConnectionPool] Pool is shutting down or empty");
    }

    DbConnection *conn = connections_.back();
    connections_.pop_back();

    return PooledConnection(conn, *this);
}

void ConnectionPool::release(DbConnection *conn)
{
    if (!conn) return;

    // 归还前检查连接状态：若处于事务中（非自动提交模式），则强制回滚
    // 防止脏连接污染下一次获取
    if (sqlite3_get_autocommit(conn->db) == 0) {
        char *errMsg = nullptr;
        if (sqlite3_exec(conn->db, "ROLLBACK;", nullptr, nullptr, &errMsg) != SQLITE_OK) {
            std::cerr << "[ConnectionPool] Failed to rollback dirty connection: " << (errMsg ? errMsg : "Unknown") << std::endl;
            if (errMsg) sqlite3_free(errMsg);
        }
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        connections_.push_back(conn);
    }
    cv_.notify_one();
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::sqlite
