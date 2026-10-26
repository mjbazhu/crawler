#pragma once

#include <boost/mysql/field_view.hpp>
#include <boost/mysql/row_view.hpp>

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {

// MySQL 行字段读取辅助，负责把 Boost.MySQL 的字段视图转换为领域模型基础类型。
class MySqlValueReader {
public:
    // 按列号读取字符串；NULL 字段返回空字符串。
    static std::string stringAt(boost::mysql::row_view row, std::size_t index);

    // 按列号读取无符号整数；NULL 或不匹配字段返回 0。
    static std::uint64_t uint64At(boost::mysql::row_view row, std::size_t index);

    // 按列号读取有符号整数；NULL 或不匹配字段返回 0。
    static std::int64_t int64At(boost::mysql::row_view row, std::size_t index);

    // 按列号读取 int；NULL 或不匹配字段返回 0。
    static int intAt(boost::mysql::row_view row, std::size_t index);

    // 按列号读取可空 int；NULL 或不匹配字段返回空。
    static std::optional<int> optionalIntAt(boost::mysql::row_view row, std::size_t index);
};

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
