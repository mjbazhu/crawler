#pragma once
#include <string_view>
#include <cstdint>
#include <sqlite3.h>
#include <string>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::sqlite {

class Statement {
public:
    Statement(sqlite3 *db, std::string_view sql);

    // 新增：构造借用式 Statement (不拥有 stmt 所有权)
    // 用于配合 StatementCache 使用
    Statement(sqlite3 *db, sqlite3_stmt *stmt);

    ~Statement();

    // 禁止拷贝，允许移动
    Statement(const Statement &) = delete;
    Statement &operator=(const Statement &) = delete;
    Statement(Statement &&other) noexcept;
    Statement &operator=(Statement &&other) noexcept;

    // 获取原始句柄（仅供内部或必要时使用）
    sqlite3_stmt *get() const;

    // 参数绑定 (返回 SQLite 错误码)
    int bindInt64(int index, int64_t value);
    int bindText(int index, std::string_view value);

    // 获取列数据 (索引从 0 开始)
    int64_t getColumnInt64(int col_index);
    std::string getColumnString(int col_index);

    // 执行控制
    int step();
    int reset();
    int clearBindings();

private:
    sqlite3 *db_ = nullptr; // 用于获取错误信息
    sqlite3_stmt *stmt_ = nullptr;
    bool owned_ = true; // 标记是否拥有 stmt 资源
};

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::sqlite
