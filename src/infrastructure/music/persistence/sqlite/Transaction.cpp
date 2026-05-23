#include "crawler/infrastructure/music/persistence/sqlite/Transaction.h"
#include <sqlite3.h>
#include <iostream>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::sqlite {

Transaction::Transaction(sqlite3 *db) : db_(db), committed_(false), active_(false)
{
    if (!db_) return;

    char *errMsg = nullptr;
    int rc = sqlite3_exec(db_, "BEGIN TRANSACTION;", nullptr, nullptr, &errMsg);
    if (rc == SQLITE_OK) {
        active_ = true;
    } else {
        std::cerr << "[DB Error] Begin transaction failed: " << (errMsg ? errMsg : "Unknown") << std::endl;
        if (errMsg) sqlite3_free(errMsg);
    }
}

Transaction::~Transaction()
{
    // 若事务处于活动状态且未提交，则自动回滚
    if (active_ && !committed_) {
        rollback();
    }
}

bool Transaction::commit()
{
    if (!active_ || committed_ || !db_) return false;

    char *errMsg = nullptr;
    int rc = sqlite3_exec(db_, "COMMIT;", nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "[DB Error] Commit failed: " << (errMsg ? errMsg : "Unknown") << std::endl;
        if (errMsg) sqlite3_free(errMsg);
        return false;
    }

    committed_ = true;
    active_ = false;
    return true;
}

void Transaction::rollback()
{
    if (!active_ || !db_) return;

    char *errMsg = nullptr;
    int rc = sqlite3_exec(db_, "ROLLBACK;", nullptr, nullptr, &errMsg);
    if (rc != SQLITE_OK) {
        std::cerr << "[DB Error] Rollback failed: " << (errMsg ? errMsg : "Unknown") << std::endl;
        if (errMsg) sqlite3_free(errMsg);
    }
    active_ = false;
}

bool Transaction::isOK() const
{
    return active_;
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::sqlite
