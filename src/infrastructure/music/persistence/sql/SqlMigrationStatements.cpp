#include "crawler/infrastructure/music/persistence/sql/SqlFileLoader.h"

#include <algorithm>
#include <cctype>
#include <stdexcept>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::sql {
namespace {

// 仅裁剪 SQL 外部空白，不改变语句中的正文。
std::string_view trimmed(std::string_view value)
{
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.front()))) value.remove_prefix(1);
    while (!value.empty() && std::isspace(static_cast<unsigned char>(value.back()))) value.remove_suffix(1);
    return value;
}

// 检查客户端分隔命令，其他 SQL 关键字不在此解释。
bool delimiterCommand(std::string_view value)
{
    constexpr std::string_view command = "delimiter";
    if (value.size() < command.size()) return false;
    for (std::size_t i = 0; i < command.size(); ++i)
        if (std::tolower(static_cast<unsigned char>(value[i])) != command[i]) return false;
    return value.size() == command.size() || std::isspace(static_cast<unsigned char>(value[command.size()]));
}

// 分隔符只能是短标点串，避免把注释、引号或任意命令解释成语句边界。
bool validDelimiter(std::string_view value)
{
    if (value.empty() || value.size() > 16 || value.starts_with("--") || value.starts_with("/*") || value.starts_with("#")) return false;
    return std::all_of(value.begin(), value.end(), [](unsigned char ch) {
        return ch >= 33 && ch <= 126 && !std::isalnum(ch) && ch != '\'' && ch != '"' && ch != '`' && ch != '\\';
    });
}
} // namespace

std::vector<std::string> SqlFileLoader::splitMigrationStatements(std::string_view script)
{
    if (script.starts_with("\xef\xbb\xbf")) script.remove_prefix(3);
    std::vector<std::string> statements;
    std::string current;
    std::string delimiter = ";";
    char quote = 0;
    bool line_comment = false, block_comment = false, line_start = true;
    const auto finish = [&] {
        const auto value = trimmed(current);
        if (!value.empty()) statements.emplace_back(value);
        current.clear();
    };
    for (std::size_t i = 0; i < script.size();) {
        if (line_start && !quote && !line_comment && !block_comment) {
            const auto end = script.find('\n', i);
            const auto line = trimmed(script.substr(i, end == std::string_view::npos ? script.size() - i : end - i));
            if (delimiterCommand(line)) {
                if (!trimmed(current).empty()) throw std::runtime_error("SQL migration delimiter changed inside a statement");
                const auto token = trimmed(line.substr(9));
                if (!validDelimiter(token)) throw std::runtime_error("SQL migration delimiter is invalid");
                delimiter.assign(token);
                i = end == std::string_view::npos ? script.size() : end + 1;
                continue;
            }
        }
        const char ch = script[i];
        const char next = i + 1 < script.size() ? script[i + 1] : '\0';
        line_start = ch == '\n';
        if (line_comment) {
            if (ch == '\n') {
                line_comment = false;
                current.push_back('\n');
            }
            ++i;
            continue;
        }
        if (block_comment) {
            if (ch == '*' && next == '/') {
                block_comment = false;
                i += 2;
            } else
                ++i;
            continue;
        }
        if (quote) {
            current.push_back(ch);
            ++i;
            if (ch == '\\' && quote != '`' && i < script.size()) {
                current.push_back(script[i++]);
            } else if (ch == quote) {
                if (next == quote) {
                    current.push_back(next);
                    ++i;
                } else
                    quote = 0;
            }
            continue;
        }
        if (script.substr(i).starts_with(delimiter)) {
            finish();
            i += delimiter.size();
            continue;
        }
        if ((ch == '-' && next == '-' && (i + 2 == script.size() || std::isspace(static_cast<unsigned char>(script[i + 2])))) || ch == '#') {
            line_comment = true;
            current.push_back(' ');
            ++i;
            continue;
        }
        if (ch == '/' && next == '*') {
            block_comment = true;
            current.push_back(' ');
            i += 2;
            continue;
        }
        if (ch == '\'' || ch == '"' || ch == '`') quote = ch;
        current.push_back(ch);
        ++i;
    }
    if (quote || block_comment) throw std::runtime_error("SQL migration has an unterminated quote or comment");
    if (!trimmed(current).empty() && delimiter != ";") throw std::runtime_error("SQL migration compound statement has no closing delimiter");
    finish();
    return statements;
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::sql
