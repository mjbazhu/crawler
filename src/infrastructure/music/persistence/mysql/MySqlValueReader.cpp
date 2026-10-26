#include "crawler/infrastructure/music/persistence/mysql/MySqlValueReader.h"

#include <limits>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {

std::string MySqlValueReader::stringAt(boost::mysql::row_view row, std::size_t index)
{
    if (index >= row.size()) {
        return {};
    }

    const boost::mysql::field_view field = row[index];
    if (field.is_null()) {
        return {};
    }
    if (field.is_string()) {
        const auto value = field.as_string();
        return std::string(value.data(), value.size());
    }
    return {};
}

std::uint64_t MySqlValueReader::uint64At(boost::mysql::row_view row, std::size_t index)
{
    if (index >= row.size()) {
        return 0;
    }

    const boost::mysql::field_view field = row[index];
    if (field.is_null()) {
        return 0;
    }
    if (field.is_uint64()) {
        return field.as_uint64();
    }
    if (field.is_int64()) {
        const auto value = field.as_int64();
        return value < 0 ? 0 : static_cast<std::uint64_t>(value);
    }
    return 0;
}

std::int64_t MySqlValueReader::int64At(boost::mysql::row_view row, std::size_t index)
{
    if (index >= row.size()) {
        return 0;
    }

    const boost::mysql::field_view field = row[index];
    if (field.is_null()) {
        return 0;
    }
    if (field.is_int64()) {
        return field.as_int64();
    }
    if (field.is_uint64()) {
        const auto value = field.as_uint64();
        return value > static_cast<std::uint64_t>((std::numeric_limits<std::int64_t>::max)()) ? 0 : static_cast<std::int64_t>(value);
    }
    return 0;
}

int MySqlValueReader::intAt(boost::mysql::row_view row, std::size_t index)
{
    const auto value = int64At(row, index);
    if (value > (std::numeric_limits<int>::max)() || value < (std::numeric_limits<int>::min)()) {
        return 0;
    }
    return static_cast<int>(value);
}

std::optional<int> MySqlValueReader::optionalIntAt(boost::mysql::row_view row, std::size_t index)
{
    if (index >= row.size() || row[index].is_null()) {
        return std::nullopt;
    }

    const auto value = int64At(row, index);
    if (value > (std::numeric_limits<int>::max)() || value < (std::numeric_limits<int>::min)()) {
        return std::nullopt;
    }
    return static_cast<int>(value);
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
