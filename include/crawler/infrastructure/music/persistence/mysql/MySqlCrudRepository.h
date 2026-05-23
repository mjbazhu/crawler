#pragma once

#include "crawler/infrastructure/music/persistence/mysql/MySqlConnectionPool.h"

#include <boost/asio/awaitable.hpp>
#include <boost/mysql/results.hpp>
#include <boost/mysql/row_view.hpp>

#include <cstdint>
#include <cstddef>
#include <memory>
#include <optional>
#include <stdexcept>
#include <utility>
#include <vector>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {

// MySQL CRUD 仓储基类，提供并发协程写入所需的通用执行和查询辅助能力。
// Entity 是领域实体类型，Id 是实体主键类型；具体 SQL 和行映射由派生类实现。
template <typename Entity, typename Id>
class MySqlCrudRepository {
public:
    // 保存共享连接池，派生类通过该连接池执行数据库操作。
    explicit MySqlCrudRepository(std::shared_ptr<MySqlConnectionPool> pool) : pool_(std::move(pool))
    {
        if (!pool_) {
            throw std::invalid_argument("[MySqlCrudRepository] pool must not be null");
        }
    }

    // 基类析构函数，保证通过基类指针释放派生仓储安全。
    virtual ~MySqlCrudRepository() = default;

    // 新增实体，返回是否实际写入或影响数据。
    virtual boost::asio::awaitable<bool> coCreate(const Entity &entity) = 0;

    // 更新实体，返回是否实际影响数据。
    virtual boost::asio::awaitable<bool> coUpdate(const Entity &entity) = 0;

    // 按主键删除实体，返回是否实际影响数据。
    virtual boost::asio::awaitable<bool> coDeleteById(const Id &id) = 0;

    // 按主键查询单个实体，不存在时返回 std::nullopt。
    virtual boost::asio::awaitable<std::optional<Entity>> coFindById(const Id &id) = 0;

    // 分页查询实体列表，由派生类决定排序字段和过滤条件。
    virtual boost::asio::awaitable<std::vector<Entity>> coFindAll(std::size_t limit, std::size_t offset) = 0;

protected:
    // 执行 INSERT/UPDATE/DELETE 或 SELECT 请求并返回 Boost.MySQL 原始结果。
    template <typename ExecutionRequest>
    boost::asio::awaitable<boost::mysql::results> coExecute(ExecutionRequest request)
    {
        co_return co_await pool_->coExecute(std::move(request));
    }

    // 执行查询并通过 mapper 把每一行转换为实体。
    template <typename ExecutionRequest, typename Mapper>
    boost::asio::awaitable<std::vector<Entity>> coQuery(ExecutionRequest request, Mapper mapper)
    {
        boost::mysql::results result = co_await coExecute(std::move(request));
        std::vector<Entity> entities;
        entities.reserve(result.rows().size());
        for (boost::mysql::row_view row : result.rows()) {
            entities.push_back(mapper(row));
        }
        co_return entities;
    }

    // 执行查询并返回第一行实体，适合按唯一键查询。
    template <typename ExecutionRequest, typename Mapper>
    boost::asio::awaitable<std::optional<Entity>> coQueryOne(ExecutionRequest request, Mapper mapper)
    {
        boost::mysql::results result = co_await coExecute(std::move(request));
        if (result.rows().empty()) {
            co_return std::nullopt;
        }
        co_return mapper(result.rows().front());
    }

    // 判断写操作是否影响了至少一行。
    static bool hasAffectedRows(const boost::mysql::results &result)
    {
        return result.affected_rows() > 0;
    }

    // 获取自增主键，供新增后回填实体或构造后续写入使用。
    static std::uint64_t lastInsertId(const boost::mysql::results &result)
    {
        return result.last_insert_id();
    }

    std::shared_ptr<MySqlConnectionPool> pool_; // 线程安全连接池，供派生类复用
};

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
