#pragma once

#include "crawler/infrastructure/music/persistence/mysql/MySqlLegacyResourceImport.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlValueReader.h"
#include "crawler/infrastructure/music/persistence/sql/SqlFileLoader.h"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/mysql/connection_pool.hpp>
#include <boost/mysql/row_view.hpp>
#include <boost/mysql/statement.hpp>

#include <algorithm>
#include <array>
#include <chrono>
#include <cstdint>
#include <future>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::legacy_import {
namespace model = application::model;
namespace result = application::result;

// 仅提交请求发出后的响应不明归为未知，普通 SQL 拒绝仍保留失败语义。
class CommitUnknown final : public std::runtime_error {
public:
    // 使用固定消息，不携带 SQL、路径或凭据。
    CommitUnknown() : std::runtime_error("resource import commit response is unknown")
    {}
};

// 固定表名来自枚举，不使用清单文本拼接 SQL 标识符。
inline std::string table(model::ResourceOwner owner)
{
    switch (owner) {
    case model::ResourceOwner::Song: return "song_resources";
    case model::ResourceOwner::Singer: return "singer_resources";
    case model::ResourceOwner::Album: return "album_resources";
    case model::ResourceOwner::SongArtifact: return "song_resource_artifacts";
    }
    throw std::invalid_argument("invalid resource owner");
}

// 外置运行期 SQL 的统一入口。
inline std::string load(std::string_view name)
{
    return sql::SqlFileLoader::loadOrThrow(std::string("sql/mysql/") + std::string(name) + ".sql");
}

// 只校验指纹格式，不将格式正确当作内容核验。
inline bool hash(std::string_view value)
{
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](char ch) { return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'); });
}

// 解析数据库自己生成的白名单资源快照。
inline model::LegacyResourceSnapshot snapshot(model::ResourceOwner owner, boost::mysql::row_view row)
{
    model::LegacyResourceSnapshot value;
    value.identity = {owner, MySqlValueReader::uint64At(row, 0)};
    value.generation = MySqlValueReader::uint64At(row, 1);
    value.source_identity = MySqlValueReader::stringAt(row, 2);
    value.values_digest = MySqlValueReader::stringAt(row, 3);
    value.status = MySqlValueReader::intAt(row, 4);
    value.cloud_backend = MySqlValueReader::stringAt(row, 5);
    value.cloud_path = MySqlValueReader::stringAt(row, 6);
    value.local_path = MySqlValueReader::stringAt(row, 7);
    value.size_bytes = MySqlValueReader::uint64At(row, 8);
    value.checksum = MySqlValueReader::stringAt(row, 9);
    value.legacy_values = MySqlValueReader::stringAt(row, 10);
    return value;
}

// 同步适配边界有界等待；写入超时继续保留未知并由 run/cursor 核对。
template <typename T>
result::ServiceResult<T> run(std::shared_ptr<MySqlConnectionPool> pool, boost::asio::awaitable<T> operation, bool write = false)
{
    try {
        auto pending = boost::asio::co_spawn(pool->executor(), std::move(operation), boost::asio::use_future);
        if (pending.wait_for(std::chrono::seconds(60)) != std::future_status::ready)
            return result::ServiceResult<T>::failure(write ? result::ServiceErrorType::CommitUncertain : result::ServiceErrorType::InternalError, "resource import database operation timed out");
        return result::ServiceResult<T>::success(pending.get());
    } catch (const std::invalid_argument &) {
        return result::ServiceResult<T>::failure(result::ServiceErrorType::InvalidParam, "invalid resource import plan");
    } catch (const std::logic_error &) {
        return result::ServiceResult<T>::failure(result::ServiceErrorType::Conflict, "resource import snapshot, target or cursor changed");
    } catch (const CommitUnknown &) {
        return result::ServiceResult<T>::failure(result::ServiceErrorType::CommitUncertain, "resource import commit needs reconciliation");
    } catch (const std::exception &) {
        return result::ServiceResult<T>::failure(result::ServiceErrorType::InternalError, "resource import database operation failed");
    }
}

// 读取数据库及实例身份，避免将清单应用到另一目标库。
boost::asio::awaitable<std::string> databaseIdentity(boost::mysql::pooled_connection &connection);
// 同一连接读取三个目标，可选行锁。
boost::asio::awaitable<std::array<model::LegacyResourceTarget, 3>> targets(boost::mysql::pooled_connection &connection, bool lock);
// 写入页的协程入口，所有值按拷贝持有直到结束。
boost::asio::awaitable<model::LegacyResourceImportResult> apply(std::shared_ptr<MySqlConnectionPool> pool, model::LegacyResourceImportPlan plan, model::LegacyResourceImportPage page);

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::legacy_import
