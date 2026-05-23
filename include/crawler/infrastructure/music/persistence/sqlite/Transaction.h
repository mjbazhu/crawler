#pragma once

#include <sqlite3.h>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::sqlite {

class Transaction {
public:
    explicit Transaction(sqlite3 *db);
    ~Transaction();

    // 提交事务
    bool commit();

    // 显式回滚（析构函数也会自动调用）
    void rollback();

    // 检查事务是否成功开启
    bool isOK() const;

private:
    sqlite3 *db_;
    bool committed_ = false;
    bool active_ = false;
};

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::sqlite
