#pragma once

#include <nlohmann/json.hpp>
#include <string>
#include <string_view>
#include <vector>
#include <optional>

namespace orchestrator::modules::crawler::infrastructure::json {

class JsonUtils {
public:
    using json = nlohmann::json;

    // 安全解析：不抛出异常，失败返回 std::nullopt
    static std::optional<json> parse(const std::vector<char> &data)
    {
        if (data.empty()) return std::nullopt;
        // allow_exceptions = false
        json j = json::parse(data.begin(), data.end(), nullptr, false);
        if (j.is_discarded()) return std::nullopt;
        return j;
    }

    // 安全获取字符串 (支持数字自动转字符串)
    static std::string getString(const json &j, const std::string &key, const std::string &default_val = "")
    {
        if (!j.is_object() || !j.contains(key)) return default_val;
        const auto &val = j[key];

        if (val.is_string()) return val.get<std::string>();
        if (val.is_number()) return std::to_string(val.get<int64_t>());
        return default_val;
    }

    // 安全获取整数 (支持字符串自动转数字)
    static int64_t getInt(const json &j, const std::string &key, int64_t default_val = 0)
    {
        if (!j.is_object() || !j.contains(key)) return default_val;
        const auto &val = j[key];

        if (val.is_number()) return val.get<int64_t>();
        if (val.is_string()) {
            try {
                return std::stoll(val.get<std::string>());
            } catch (...) {}
        }
        return default_val;
    }

    // 安全获取对象指针
    static const json *getObject(const json &j, const std::string &key)
    {
        if (j.is_object() && j.contains(key)) {
            const auto &val = j[key];
            if (val.is_object()) return &val;
        }
        return nullptr;
    }

    // 安全获取数组指针
    static const json *getArray(const json &j, const std::string &key)
    {
        if (j.is_object() && j.contains(key)) {
            const auto &val = j[key];
            if (val.is_array()) return &val;
        }
        return nullptr;
    }

    // 安全获取数组元素指针
    static const json *getArrayItem(const json &j, size_t index)
    {
        if (j.is_array() && index < j.size()) {
            return &j[index];
        }
        return nullptr;
    }
};

} // namespace orchestrator::modules::crawler::infrastructure::json