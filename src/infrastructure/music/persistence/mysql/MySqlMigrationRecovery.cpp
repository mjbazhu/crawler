#include "MySqlMigrationRecovery.h"

#include "crawler/infrastructure/music/persistence/mysql/MySqlValueReader.h"
#include "crawler/infrastructure/music/persistence/sql/SqlFileLoader.h"

#include <boost/asio/use_awaitable.hpp>
#include <boost/mysql/results.hpp>
#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <map>
#include <stdexcept>
#include <string_view>
#include <utility>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::migration_recovery {
namespace {
using Json = nlohmann::json;
// 诊断只含固定标识，不输出数据库元数据或业务值。
void require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}
// 元数据的SQL NULL只允许转换为空字符串，不能掩盖其他类型错误。
std::string text(const Json &row, const char *key)
{
    return row.at(key).is_null() ? std::string {} : row.at(key).get<std::string>();
}
// 只规范SQL标识符/空白及字符集前缀；字符串字面量逐字保留，防错误JSON路径被归一。
std::string expression(std::string_view value)
{
    // MySQL元数据可统一转义字面量分隔符；只接受全量同形，不吞字面量内部转义。
    std::string unescaped;
    if (value.find("\\'") != std::string_view::npos) {
        for (std::size_t i = 0; i < value.size(); ++i) {
            if (value[i] == '\\') {
                require(i + 1 < value.size() && value[i + 1] == '\'', "migration_2910_expression_escape_unsupported");
                unescaped += value[++i];
            } else {
                require(value[i] != '\'', "migration_2910_expression_mixed_quotes");
                unescaped += value[i];
            }
        }
        value = unescaped;
    }
    std::string result;
    for (std::size_t i = 0; i < value.size();) {
        const unsigned char current = value[i];
        if (std::isspace(current)) {
            ++i;
            continue;
        }
        if (current == '\'') {
            result += value[i++];
            bool closed = false;
            while (i < value.size()) {
                require(value[i] != '\\', "migration_2910_expression_escape_unsupported");
                result += value[i];
                if (value[i++] == '\'') {
                    if (i < value.size() && value[i] == '\'') {
                        result += value[i++];
                        continue;
                    }
                    closed = true;
                    break;
                }
            }
            require(closed, "migration_2910_expression_unterminated");
        } else if (current == '`') {
            ++i;
            while (i < value.size() && value[i] != '`') result += static_cast<char>(std::tolower(static_cast<unsigned char>(value[i++])));
            require(i < value.size(), "migration_2910_identifier_unterminated");
            ++i;
        } else if (std::isalnum(current) || current == '_') {
            std::string token;
            while (i < value.size() && (std::isalnum(static_cast<unsigned char>(value[i])) || value[i] == '_')) token += static_cast<char>(std::tolower(static_cast<unsigned char>(value[i++])));
            auto next = i;
            while (next < value.size() && std::isspace(static_cast<unsigned char>(value[next]))) ++next;
            const bool charset = token == "_utf8mb4" || token == "_utf8mb3" || token == "_utf8" || token == "_ascii";
            if (!(charset && next < value.size() && value[next] == '\'')) result += token;
        } else
            result += value[i++];
    }
    // MySQL可为完整表达式额外包一层括号，不删除函数内部括号。
    while (result.size() > 1 && result.front() == '(' && result.back() == ')') {
        int depth = 0;
        bool complete = true, quoted = false;
        for (std::size_t i = 0; i < result.size(); ++i) {
            if (result[i] == '\'') quoted = !quoted;
            if (quoted) continue;
            if (result[i] == '(') ++depth;
            if (result[i] == ')' && --depth == 0 && i + 1 != result.size()) {
                complete = false;
                break;
            }
        }
        if (!complete) break;
        result = result.substr(1, result.size() - 2);
    }
    return result;
}
// 整数显示宽度在MySQL8版本间不同，不影响列存储；其他类型必须完整匹配。
std::string columnType(std::string value)
{
    value = expression(value);
    if (value.starts_with("bigint(") || value.starts_with("tinyint(")) {
        const auto begin = value.find('('), end = value.find(')', begin);
        if (end != std::string::npos && std::all_of(value.begin() + begin + 1, value.begin() + end, [](unsigned char c) { return std::isdigit(c); })) value.erase(begin, end - begin + 1);
    }
    return value;
}
// 固定字段定义包含类型、可空、字符集、默认值、生成属性和位置。
bool column(const Json &row, std::string_view type, bool nullable, std::string_view charset, unsigned ordinal, std::string_view generated = {})
{
    return columnType(text(row, "type")) == type && text(row, "nullable") == (nullable ? "YES" : "NO") && text(row, "charset") == charset && text(row, "collation") == (charset.empty() ? "" : "ascii_bin") && row.at("default").is_null() && row.at("ordinal") == ordinal && expression(text(row, "extra")) == (generated.empty() ? "" : "storedgenerated") && expression(text(row, "expression")) == expression(generated);
}
// 元数据集合缺失表示空，不接受其他形状或隐式类型转换。
std::vector<Json> rows(const Json &metadata, const char *key)
{
    const auto &value = metadata.at(key);
    if (value.is_null()) return {};
    require(value.is_array(), "migration_2910_metadata_shape_invalid");
    return value.get<std::vector<Json>>();
}
// 新增索引必须是完整可见BTREE，不接受前缀、反序或其他额外索引段。
bool index(const Json &row, std::string_view name, std::string_view field, unsigned ordinal, bool unique)
{
    return text(row, "name") == name && text(row, "column") == field && row.at("ordinal") == ordinal && row.at("non_unique") == (unique ? 0 : 1) && row.at("prefix").is_null() && text(row, "type") == "BTREE" && text(row, "direction") == "A" && text(row, "visible") == "YES";
}
// CREATE related已完成而ALTER尚未执行可安全重跑；其他半成品不自动修复。
bool completed(const Json &metadata)
{
    std::map<std::string, Json> tables, operations, related;
    for (const auto &row : rows(metadata, "tables")) tables.emplace(text(row, "name"), row);
    require(tables.contains("resource_transfer_operations") && text(tables.at("resource_transfer_operations"), "engine") == "InnoDB", "migration_2910_base_table_invalid");
    for (const auto &row : rows(metadata, "columns")) (text(row, "table") == "resource_transfer_operations" ? operations : related).emplace(text(row, "name"), row);
    std::vector<Json> operation_indexes, related_indexes;
    for (const auto &row : rows(metadata, "indexes")) (text(row, "table") == "resource_transfer_operations" ? operation_indexes : related_indexes).push_back(row);
    if (tables.contains("resource_transfer_related")) {
        const auto &table = tables.at("resource_transfer_related");
        require(text(table, "engine") == "InnoDB" && text(table, "type") == "BASE TABLE" && text(table, "collation") == "utf8mb4_unicode_ci" && related.size() == 6, "migration_2910_related_table_invalid");
        require(column(related.at("parent_task_id"), "bigintunsigned", false, "", 1) && column(related.at("discovered_mask"), "tinyintunsigned", false, "", 2) && column(related.at("claim_generation"), "bigintunsigned", false, "", 3) && column(related.at("child_operation_id"), "varchar(64)", true, "ascii", 4) && column(related.at("created_at"), "bigintunsigned", false, "", 5) && column(related.at("updated_at"), "bigintunsigned", false, "", 6), "migration_2910_related_columns_invalid");
        require(related_indexes.size() == 1 && index(related_indexes[0], "PRIMARY", "parent_task_id", 1, true), "migration_2910_related_index_invalid");
        const auto constraints = rows(metadata, "constraints"), foreign = rows(metadata, "foreign_keys"), checks = rows(metadata, "checks");
        require(constraints.size() == 3 && foreign.size() == 1 && checks.size() == 1, "migration_2910_related_constraints_invalid");
        std::map<std::string, std::string> kinds;
        for (const auto &row : constraints) {
            require(text(row, "enforced") == "YES", "migration_2910_constraint_not_enforced");
            kinds.emplace(text(row, "name"), text(row, "type"));
        }
        require(kinds == std::map<std::string, std::string> {{"PRIMARY", "PRIMARY KEY"}, {"fk_related_parent_task", "FOREIGN KEY"}, {"ck_related_mask", "CHECK"}}, "migration_2910_constraint_definition_invalid");
        const auto &fk = foreign[0];
        require(text(fk, "name") == "fk_related_parent_task" && text(fk, "column") == "parent_task_id" && fk.at("ordinal") == 1 && text(fk, "schema") == metadata.at("database").get<std::string>() && text(fk, "table") == "resource_transfer_tasks" && text(fk, "referenced") == "task_id" && text(fk, "delete") == "CASCADE" && (text(fk, "update") == "RESTRICT" || text(fk, "update") == "NO ACTION"), "migration_2910_foreign_key_invalid");
        require(text(checks[0], "name") == "ck_related_mask" && expression(text(checks[0], "expression")) == "discovered_mask<=15", "migration_2910_check_invalid");
    } else
        require(related.empty() && related_indexes.empty(), "migration_2910_related_metadata_invalid");
    if (operations.empty() && operation_indexes.empty()) return false;
    require(tables.contains("resource_transfer_related") && operations.size() == 2 && operation_indexes.size() == 2, "migration_2910_partial_schema_requires_repair");
    require(column(operations.at("parent_operation_id"), "varchar(64)", true, "ascii", 11, "NULLIF(JSON_UNQUOTE(JSON_EXTRACT(request_snapshot,'$.parent_operation_id')),'')") && column(operations.at("parent_task_id"), "bigintunsigned", true, "", 12, "CAST(JSON_UNQUOTE(JSON_EXTRACT(request_snapshot,'$.parent_task_id')) AS UNSIGNED)"), "migration_2910_generated_definition_invalid");
    std::sort(operation_indexes.begin(), operation_indexes.end(), [](const Json &a, const Json &b) { return a.at("ordinal") < b.at("ordinal"); });
    require(index(operation_indexes[0], "idx_transfer_parent_operation", "parent_operation_id", 1, false) && index(operation_indexes[1], "idx_transfer_parent_operation", "operation_id", 2, false), "migration_2910_parent_index_invalid");
    return true;
}
// 固定控制查询只读组件标记；应用表数据不在此处读取或修改。
boost::asio::awaitable<std::uint64_t> componentVersion(boost::mysql::pooled_connection &connection)
{
    boost::mysql::results result;
    co_await connection->async_execute("SELECT schema_version FROM crawler_schema_versions WHERE component='crawler_mysql'", result, boost::asio::use_awaitable);
    co_return result.rows().empty() ? 0 : MySqlValueReader::uint64At(result.rows().front(), 0);
}
} // namespace

boost::asio::awaitable<bool> reconcileCompletedDdl(boost::mysql::pooled_connection &connection, std::uint64_t version, std::string checksum)
{
    if (version != 2026102910) co_return false;
    require(checksum == "840cc00d07e8a0881b63c2411a8090ec", "migration_2910_frozen_checksum_mismatch");
    boost::mysql::results result;
    co_await connection->async_execute(sql::SqlFileLoader::loadOrThrow("sql/mysql/schema/migration_2910_postconditions.sql"), result, boost::asio::use_awaitable);
    require(result.rows().size() == 1 && result.rows().front().size() == 1, "migration_2910_metadata_missing");
    co_return completed(Json::parse(MySqlValueReader::stringAt(result.rows().front(), 0)));
}

boost::asio::awaitable<void> synchronizeComponent(boost::mysql::pooled_connection &connection, std::uint64_t version, std::string name)
{
    require(co_await componentVersion(connection) <= version, "migration_component_version_ahead");
    auto statement = co_await connection->async_prepare_statement(sql::SqlFileLoader::loadOrThrow("sql/mysql/schema/synchronize_component_version.sql"), boost::asio::use_awaitable);
    boost::mysql::results result;
    co_await connection->async_execute(statement.bind(version, name), result, boost::asio::use_awaitable);
    require(co_await componentVersion(connection) == version, "migration_component_version_not_synchronized");
}
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::migration_recovery
