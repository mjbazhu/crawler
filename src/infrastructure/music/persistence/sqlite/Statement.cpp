#include "crawler/infrastructure/music/persistence/sqlite/Statement.h"
#include <sqlite3.h>
#include <iostream>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::sqlite {

Statement::Statement(sqlite3 *db, std::string_view sql) : db_(db), stmt_(nullptr), owned_(true)
{
    if (!db) return;

    // 使用长度参数，支持 string_view 非空终止的情况
    int rc = sqlite3_prepare_v2(db, sql.data(), static_cast<int>(sql.size()), &stmt_, nullptr);
    if (rc != SQLITE_OK) {
        std::cerr << "[DB Error] Prepare failed: " << sqlite3_errmsg(db) << "\nSQL: " << sql << std::endl;
        stmt_ = nullptr;
    }
}

Statement::Statement(sqlite3 *db, sqlite3_stmt *stmt) : db_(db), stmt_(stmt), owned_(false)
{
}

Statement::~Statement()
{
    if (stmt_) {
        if (owned_) {
            sqlite3_finalize(stmt_);
        } else {
            // 借用模式：归还前重置状态，供缓存复用
            sqlite3_reset(stmt_);
            sqlite3_clear_bindings(stmt_);
        }
        stmt_ = nullptr;
    }
}

Statement::Statement(Statement &&other) noexcept
    : db_(other.db_), stmt_(other.stmt_), owned_(other.owned_)
{
    other.db_ = nullptr;
    other.stmt_ = nullptr;
    other.owned_ = true;
}

Statement &Statement::operator=(Statement &&other) noexcept
{
    if (this != &other) {
        if (stmt_) {
            if (owned_) {
                sqlite3_finalize(stmt_);
            } else {
                sqlite3_reset(stmt_);
                sqlite3_clear_bindings(stmt_);
            }
        }
        db_ = other.db_;
        stmt_ = other.stmt_;
        owned_ = other.owned_;
        other.db_ = nullptr;
        other.stmt_ = nullptr;
        other.owned_ = true;
    }
    return *this;
}

sqlite3_stmt *Statement::get() const
{
    return stmt_;
}

int Statement::bindInt64(int index, int64_t value)
{
    if (!stmt_) return SQLITE_ERROR;
    return sqlite3_bind_int64(stmt_, index, value);
}

int Statement::bindText(int index, std::string_view value)
{
    if (!stmt_) return SQLITE_ERROR;
    // SQLITE_TRANSIENT: SQLite 会拷贝数据，string_view 生命周期结束不影响绑定
    return sqlite3_bind_text(stmt_, index, value.data(), static_cast<int>(value.size()), SQLITE_TRANSIENT);
}

int64_t Statement::getColumnInt64(int col_index)
{
    if (!stmt_) return 0;
    return sqlite3_column_int64(stmt_, col_index);
}

std::string Statement::getColumnString(int col_index)
{
    if (!stmt_) return "";
    const unsigned char *text = sqlite3_column_text(stmt_, col_index);
    if (!text) return "";
    int len = sqlite3_column_bytes(stmt_, col_index);
    return std::string(reinterpret_cast<const char *>(text), len);
}

int Statement::step()
{
    if (!stmt_) return SQLITE_ERROR;
    return sqlite3_step(stmt_);
}

int Statement::reset()
{
    if (!stmt_) return SQLITE_ERROR;
    return sqlite3_reset(stmt_);
}

int Statement::clearBindings()
{
    if (!stmt_) return SQLITE_ERROR;
    return sqlite3_clear_bindings(stmt_);
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::sqlite
