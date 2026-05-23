#include "crawler/infrastructure/platform/Environment.h"

#include "crawler/infrastructure/music/persistence/sql/SqlFileLoader.h"

#include <algorithm>
#include <cctype>
#include <cstddef>
#include <cstdlib>
#include <fstream>
#include <sstream>
#include <string>
#include <utility>

#ifdef _WIN32
#include <stdlib.h>
#endif

namespace orchestrator::modules::crawler::infrastructure::platform {

namespace {

namespace sql_loader = orchestrator::modules::crawler::infrastructure::music::persistence::sql;

// 去除两端空白，保留值内部空格。
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

// 转换为小写，用于 bool 值解析。
std::string lower(std::string value)
{
    std::transform(value.begin(), value.end(), value.begin(), [](unsigned char ch) {
        return static_cast<char>(std::tolower(ch));
    });
    return value;
}

// 判断 .env 键是否合法，避免把异常行写入进程环境。
bool isValidKey(std::string_view key)
{
    if (key.empty()) {
        return false;
    }

    for (char ch : key) {
        const auto uch = static_cast<unsigned char>(ch);
        if (std::isalnum(uch) == 0 && ch != '_') {
            return false;
        }
    }
    return true;
}

// 去掉包裹值的一层单引号或双引号。
std::string unquote(std::string value)
{
    if (value.size() < 2) {
        return value;
    }

    const char first = value.front();
    const char last = value.back();
    if ((first == '"' && last == '"') || (first == '\'' && last == '\'')) {
        value = value.substr(1, value.size() - 2);
    }
    return value;
}

// 写入当前进程环境，供后续配置读取使用。
bool setProcessEnv(const std::string &key, const std::string &value)
{
#ifdef _WIN32
    return _putenv_s(key.c_str(), value.c_str()) == 0;
#else
    return setenv(key.c_str(), value.c_str(), 1) == 0;
#endif
}

} // namespace

bool Environment::loadDotEnvFile(std::string_view path, bool overwrite_existing)
{
    const auto resolved = sql_loader::SqlFileLoader::resolvePath(path);
    if (!resolved) {
        return false;
    }

    std::ifstream file(*resolved, std::ios::in | std::ios::binary);
    if (!file) {
        return false;
    }

    std::string line;
    while (std::getline(file, line)) {
        line = trim(line);
        if (line.empty() || line.front() == '#') {
            continue;
        }

        if (line.rfind("export ", 0) == 0) {
            line = trim(std::string_view(line).substr(7));
        }

        const auto eq_pos = line.find('=');
        if (eq_pos == std::string::npos) {
            continue;
        }

        std::string key = trim(std::string_view(line).substr(0, eq_pos));
        std::string value = unquote(trim(std::string_view(line).substr(eq_pos + 1)));
        if (!isValidKey(key)) {
            continue;
        }
        if (!overwrite_existing && getString(key)) {
            continue;
        }

        (void)setProcessEnv(key, value);
    }

    return true;
}

std::optional<std::string> Environment::getString(std::string_view key)
{
    std::string key_text(key);
#ifdef _WIN32
    char *raw_value = nullptr;
    std::size_t raw_size = 0;
    if (_dupenv_s(&raw_value, &raw_size, key_text.c_str()) != 0 || raw_value == nullptr) {
        return std::nullopt;
    }

    std::string value(raw_value);
    std::free(raw_value);
    if (value.empty()) {
        return std::nullopt;
    }
    return value;
#else
    const char *value = std::getenv(key_text.c_str());
    if (value == nullptr || *value == '\0') {
        return std::nullopt;
    }
    return std::string(value);
#endif
}

std::string Environment::getStringOr(std::string_view key, std::string fallback)
{
    return getString(key).value_or(std::move(fallback));
}

int Environment::getIntOr(std::string_view key, int fallback)
{
    auto value = getString(key);
    if (!value) {
        return fallback;
    }

    try {
        std::size_t parsed = 0;
        const int number = std::stoi(*value, &parsed);
        return parsed == value->size() ? number : fallback;
    } catch (...) {
        return fallback;
    }
}

unsigned short Environment::getPortOr(std::string_view key, unsigned short fallback)
{
    const int value = getIntOr(key, fallback);
    if (value <= 0 || value > 65535) {
        return fallback;
    }
    return static_cast<unsigned short>(value);
}

bool Environment::getBoolOr(std::string_view key, bool fallback)
{
    auto value = getString(key);
    if (!value) {
        return fallback;
    }

    const std::string normalized = lower(trim(*value));
    if (normalized == "true" || normalized == "1" || normalized == "yes" || normalized == "on") {
        return true;
    }
    if (normalized == "false" || normalized == "0" || normalized == "no" || normalized == "off") {
        return false;
    }
    return fallback;
}

} // namespace orchestrator::modules::crawler::infrastructure::platform
