#pragma once

#include "crawler/application/model/MetadataCover.h"
#include "crawler/application/result/ServiceResult.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlConnectionPool.h"
#include "crawler/infrastructure/music/persistence/sql/SqlFileLoader.h"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/use_future.hpp>

#include <stdexcept>
#include <string>

#include <exception>
#include <memory>
#include <string_view>
#include <utility>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::cover_detail {

namespace model = application::model;

// 同步 port 桥接池执行器，异常只返回固定诊断，不暴露连接信息。
template <typename T>
application::result::ServiceResult<T> run(const std::shared_ptr<MySqlConnectionPool> &pool, boost::asio::awaitable<T> operation)
{
    using Result = application::result::ServiceResult<T>;
    try {
        return Result::success(boost::asio::co_spawn(pool->executor(), std::move(operation), boost::asio::use_future).get());
    } catch (const std::exception &) {
        return Result::failure(application::result::ServiceErrorType::InternalError, "metadata cover database operation failed");
    }
}

// 只有受限枚举可决定表级 SQL 路径。
inline std::string load(model::CoverOwner owner, const std::string &name)
{
    return sql::SqlFileLoader::loadOrThrow(std::string("sql/mysql/") + (owner == model::CoverOwner::Singer ? "singer_resources/" : "album_resources/") + name + ".sql");
}

// 数字 ID 列表来自强类型参数，仅替换显式的筛选占位符。
template <typename T>
std::string inList(const std::string &column, const std::vector<T> &values)
{
    if (values.empty()) return {};
    if (values.size() > 200) throw std::invalid_argument("too many cover filters");
    std::string filter = " AND " + column + " IN (";
    for (std::size_t i = 0; i < values.size(); ++i) {
        if (i != 0) filter += ',';
        filter += std::to_string(values[i]);
    }
    return filter + ')';
}

// 将统一逐端状态投影到旧封面状态码，不能把历史skip或旧代次确认伪装为本次成功。
inline std::string cloudStatus()
{
    return "CASE c.task_state WHEN 'running' THEN 1 WHEN 'uncertain' THEN 4 WHEN 'failed' THEN 3 WHEN 'succeeded' THEN IF(c.confirmed_content_generation=r.content_generation AND c.confirmed_target_revision=d.target_revision AND c.size_bytes=r.size_bytes AND BINARY c.checksum=BINARY COALESCE(r.checksum,''),2,0) ELSE 0 END";
}

// 含字符串的来源条件仍通过 prepared statement 绑定。
inline std::string filtered(model::CoverOwner owner, const std::string &name, const model::MetadataCoverQuery &query, bool statuses = true)
{
    auto statement = load(owner, name);
    const std::string ref = owner == model::CoverOwner::Singer ? "r.singer_source_ref_id" : "r.album_source_ref_id";
    std::string filter = inList("r.id", query.resource_ids) + inList(ref, query.source_ref_ids);
    if (statuses) filter += inList("r.status", query.statuses) + inList(cloudStatus(), query.cloud_statuses);
    constexpr std::string_view placeholder = "/*__FILTERS__*/";
    const auto marker = statement.find(placeholder);
    if (marker == std::string::npos) throw std::logic_error("cover filter marker missing");
    statement.replace(marker, placeholder.size(), filter);
    return statement;
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::cover_detail
