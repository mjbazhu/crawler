#include "crawler/infrastructure/music/persistence/sql/SqlFileLoader.h"

#include <cctype>
#include <fstream>
#include <sstream>
#include <stdexcept>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::sql {

namespace {

// 去除 SQL 语句两端空白，避免把空语句交给数据库执行。
std::string trim(std::string_view value)
{
    std::size_t begin = 0;
    while (begin < value.size() && std::isspace(static_cast<unsigned char>(value[begin])) != 0) {
        ++begin;
    }

    std::size_t end = value.size();
    while (end > begin && std::isspace(static_cast<unsigned char>(value[end - 1])) != 0) {
        --end;
    }

    return std::string(value.substr(begin, end - begin));
}

// 去除 UTF-8 BOM，避免某些数据库把 BOM 当作 SQL token。
std::string stripUtf8Bom(std::string value)
{
    if (value.size() >= 3 && static_cast<unsigned char>(value[0]) == 0xEF && static_cast<unsigned char>(value[1]) == 0xBB && static_cast<unsigned char>(value[2]) == 0xBF) {
        value.erase(0, 3);
    }
    return value;
}

} // namespace

std::optional<std::filesystem::path> SqlFileLoader::resolvePath(std::string_view path)
{
    if (path.empty()) {
        return std::nullopt;
    }

    std::filesystem::path requested(path);
    if (requested.is_absolute() && std::filesystem::exists(requested)) {
        return requested;
    }
    if (std::filesystem::exists(requested)) {
        return std::filesystem::absolute(requested);
    }

    std::filesystem::path current = std::filesystem::current_path();
    for (int depth = 0; depth < 8; ++depth) {
        const std::filesystem::path candidate = current / requested;
        if (std::filesystem::exists(candidate)) {
            return candidate;
        }
        if (!current.has_parent_path() || current == current.parent_path()) {
            break;
        }
        current = current.parent_path();
    }

    return std::nullopt;
}

std::optional<std::string> SqlFileLoader::load(std::string_view path)
{
    const auto resolved = resolvePath(path);
    if (!resolved) {
        return std::nullopt;
    }

    std::ifstream file(*resolved, std::ios::in | std::ios::binary);
    if (!file) {
        return std::nullopt;
    }

    std::ostringstream buffer;
    buffer << file.rdbuf();
    return stripUtf8Bom(buffer.str());
}

std::string SqlFileLoader::loadOrThrow(std::string_view path)
{
    auto sql = load(path);
    if (!sql || trim(*sql).empty()) {
        throw std::runtime_error("[SqlFileLoader] SQL file not found or empty: " + std::string(path));
    }
    return *sql;
}

std::vector<std::string> SqlFileLoader::splitStatements(std::string_view script)
{
    std::vector<std::string> statements;
    std::string current;
    current.reserve(script.size());

    bool in_single_quote = false;
    bool in_double_quote = false;
    bool in_line_comment = false;
    bool in_block_comment = false;

    for (std::size_t i = 0; i < script.size(); ++i) {
        const char ch = script[i];
        const char next = (i + 1 < script.size()) ? script[i + 1] : '\0';

        if (in_line_comment) {
            if (ch == '\n') {
                in_line_comment = false;
                current.push_back(ch);
            }
            continue;
        }

        if (in_block_comment) {
            if (ch == '*' && next == '/') {
                in_block_comment = false;
                ++i;
            }
            continue;
        }

        if (!in_single_quote && !in_double_quote && ch == '-' && next == '-') {
            in_line_comment = true;
            ++i;
            continue;
        }

        if (!in_single_quote && !in_double_quote && ch == '#') {
            in_line_comment = true;
            continue;
        }

        if (!in_single_quote && !in_double_quote && ch == '/' && next == '*') {
            in_block_comment = true;
            ++i;
            continue;
        }

        current.push_back(ch);

        if (ch == '\'' && !in_double_quote) {
            if (next == '\'') {
                current.push_back(next);
                ++i;
                continue;
            }
            in_single_quote = !in_single_quote;
            continue;
        }

        if (ch == '"' && !in_single_quote) {
            if (next == '"') {
                current.push_back(next);
                ++i;
                continue;
            }
            in_double_quote = !in_double_quote;
            continue;
        }

        if (ch == ';' && !in_single_quote && !in_double_quote) {
            current.pop_back();
            std::string statement = trim(current);
            if (!statement.empty()) {
                statements.push_back(std::move(statement));
            }
            current.clear();
        }
    }

    std::string tail = trim(current);
    if (!tail.empty()) {
        statements.push_back(std::move(tail));
    }

    return statements;
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::sql
