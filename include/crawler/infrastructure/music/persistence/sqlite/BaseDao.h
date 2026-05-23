#pragma once

#include "crawler/infrastructure/music/persistence/sqlite/ConnectionPool.h"

#include <cstdint>
#include <string>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::sqlite {

// 模板类：提供通用的数据库操作接口
template <typename T>
class BaseDao {
public:
    explicit BaseDao(ConnectionPool &pool);
    virtual ~BaseDao() = default;

    // 公共接口：自动获取连接并调用 doSave
    // 内部实现：从池中获取连接 -> 执行 doSave
    bool save(const T &item);

    // 批量操作：自动获取连接并开启事务
    // 失败策略：若任一 item 保存失败，Transaction 析构自动回滚
    virtual bool saveBatch(const std::vector<T> &items);

    // 获取所有记录
    std::vector<T> getAll();

    // 分页获取记录
    std::vector<T> getList(int offset, int limit);

    // 获取记录总数
    uint64_t count();

protected:
    // 子类需实现的具体保存逻辑，使用传入的连接
    virtual bool doSave(PooledConnection &conn, const T &item) = 0;

    virtual std::string getTableName() = 0;
    virtual T mapRowToEntity(Statement &stmt) = 0;

    ConnectionPool &pool_;
};

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::sqlite

#include "crawler/infrastructure/music/persistence/sqlite/BaseDao.tpp"
