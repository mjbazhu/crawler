#pragma once

#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceTransferJobs.h"
#include "crawler/application/model/ResourceProducerTask.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlValueReader.h"
#include "crawler/infrastructure/music/persistence/sql/SqlFileLoader.h"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/use_future.hpp>
#include <boost/mysql/statement.hpp>

#include <array>
#include <chrono>
#include <exception>
#include <future>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <string_view>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::transfer_jobs {
namespace model = application::model;
namespace result = application::result;

// 提交确认丢失时保留未决语义，调用方必须按 operation/task 核对。
class CommitUnknown final : public std::runtime_error {
public:
    // 固定脱敏消息。
    CommitUnknown() : std::runtime_error("resource transfer commit response unknown")
    {}
};

// 未启用目标的新受理必须回滚，不能制造待处理父操作。
class DestinationDisabled final : public std::runtime_error {
public:
    // 目标未配置不能在新受理中隐式扩大或降级。
    DestinationDisabled() : std::runtime_error("resource destination disabled")
    {}
};

// API 前置检查后维护状态仍可能变化，登记事务必须再次失败关闭。
class WriteAdmissionClosed final : public std::runtime_error {
public:
    // 只向上层提供稳定脱敏原因。
    WriteAdmissionClosed() : std::runtime_error("resource write admission closed")
    {}
};

// 只表示事务已明确回滚的准入拒绝，不是未知提交。
class QueueCapacityExceeded final : public std::runtime_error {
public:
    // 固定诊断，无原始SQL或输入。
    QueueCapacityExceeded() : std::runtime_error("resource_queue_full")
    {}
};

// 区分有效查询中不存在的父操作。
class MissingOperation final : public std::runtime_error {
public:
    // 不携带业务或连接信息。
    MissingOperation() : std::runtime_error("resource transfer operation not found")
    {}
};

// 事务中冻结的原生资源状态；不包含临时 URL 或认证。
struct Snapshot {
    model::ResourceIdentity identity;   // 原生身份
    std::uint64_t generation = 0;       // 内容代次
    std::uint64_t source_revision = 0;  // 来源版本
    std::string source_identity;        // 来源身份摘要
    int status = 0;                     // 原生本地状态
    std::uint64_t claim_generation = 0; // 本地认领代次
    std::string running_batch_id;       // 本地占用
    std::string reference_sha256;       // 当前内容实测参考
    std::uint64_t reference_size = 0;   // 当前参考大小
    std::uint64_t local_generation = 0; // 本地已确认代次
    std::uint64_t local_revision = 0;   // 本地已确认目标
    model::ResourceTransferSkip skip;   // 本地历史策略
    std::string local_path;             // 上次本地完整对象
    std::string checksum;               // 上次本地原始摘要
    std::uint64_t size_bytes = 0;       // 上次本地大小
};

// 各端的持久闸门，不把配置可用与历史 skip 混同。
struct Target {
    model::ResourceDestination destination = model::ResourceDestination::Local; // 固定端
    std::uint64_t revision = 0;                                                 // 当前修订
    std::string fingerprint;                                                    // 当前目标摘要
    std::string state;                                                          // maintenance/active/paused
    bool baseline_ready = false;                                                // 历史已审阅
};

// 外置 SQL 路径只能由适配器固定表名与文件名构成。
std::string table(model::ResourceOwner owner);
// 只接受标准小写 SHA-256，防止把 size/MD5 当成强参考。
bool hash(std::string_view value);
// 校验数据库字段可容纳的受控结果码。
bool code(std::string_view value);
// 对象身份禁止 URL、控制字符和父目录跳转；根内约束由具体存储另行保证。
bool safePath(std::string_view path);
// 固定时钟用于任务预算和租约。
std::uint64_t now();
// 查询或写入一条外置 prepared SQL，参数按值归协程所有。
template <typename... Args>
boost::asio::awaitable<boost::mysql::results> query(boost::mysql::pooled_connection &connection, std::string path, Args... args)
{
    const auto text = sql::SqlFileLoader::loadOrThrow("sql/mysql/" + path + ".sql");
    auto statement = co_await connection->async_prepare_statement(text, boost::asio::use_awaitable);
    boost::mysql::results rows;
    co_await connection->async_execute(statement.bind(args...), rows, boost::asio::use_awaitable);
    co_return rows;
}

// 短事务统一提交/回滚；连接由 pooled_connection RAII 托管并在归还时重置。
template <typename T, typename Work>
boost::asio::awaitable<T> transaction(std::shared_ptr<MySqlConnectionPool> pool, Work work, bool read_committed = false)
{
    auto connection = co_await pool->coAcquire();
    boost::mysql::results ignored;
    // 认领门禁等待批次锁后需要最新计数；只改变下一事务，不泄漏连接默认配置。
    if (read_committed) co_await connection->async_execute("SET TRANSACTION ISOLATION LEVEL READ COMMITTED", ignored, boost::asio::use_awaitable);
    co_await connection->async_execute("START TRANSACTION", ignored, boost::asio::use_awaitable);
    std::exception_ptr failure;
    bool committing = false;
    try {
        auto value = co_await work(connection);
        committing = true;
        co_await connection->async_execute("COMMIT", ignored, boost::asio::use_awaitable);
        co_return value;
    } catch (...) {
        failure = std::current_exception();
    }
    if (committing) throw CommitUnknown();
    try {
        co_await connection->async_execute("ROLLBACK", ignored, boost::asio::use_awaitable);
    } catch (...) {}
    std::rethrow_exception(failure);
}

// 同步端口只做有界等待；超时写入需核对，不声称已经取消后台 SQL。
template <typename T>
result::ServiceResult<T> run(std::shared_ptr<MySqlConnectionPool> pool, boost::asio::awaitable<T> operation, bool write = false, std::chrono::milliseconds timeout = std::chrono::seconds(60))
{
    try {
        auto pending = boost::asio::co_spawn(pool->executor(), std::move(operation), boost::asio::use_future);
        if (pending.wait_for(timeout) != std::future_status::ready)
            return result::ServiceResult<T>::failure(write ? result::ServiceErrorType::CommitUncertain : result::ServiceErrorType::InternalError, "resource transfer database timeout");
        return result::ServiceResult<T>::success(pending.get());
    } catch (const std::invalid_argument &) {
        return result::ServiceResult<T>::failure(result::ServiceErrorType::InvalidParam, "invalid resource transfer request");
    } catch (const DestinationDisabled &) {
        return result::ServiceResult<T>::failure(result::ServiceErrorType::ExternalApiError, "resource_destination_disabled");
    } catch (const WriteAdmissionClosed &) {
        return result::ServiceResult<T>::failure(result::ServiceErrorType::ExternalApiError, "resource_maintenance");
    } catch (const QueueCapacityExceeded &) {
        return result::ServiceResult<T>::failure(result::ServiceErrorType::ExternalApiError, "resource_queue_full");
    } catch (const MissingOperation &) {
        return result::ServiceResult<T>::failure(result::ServiceErrorType::NotFound, "resource transfer operation not found");
    } catch (const std::logic_error &) {
        return result::ServiceResult<T>::failure(result::ServiceErrorType::Conflict, "resource transfer request or version conflict");
    } catch (const CommitUnknown &) {
        return result::ServiceResult<T>::failure(result::ServiceErrorType::CommitUncertain, "resource transfer commit needs reconciliation");
    } catch (const std::exception &) {
        return result::ServiceResult<T>::failure(result::ServiceErrorType::InternalError, "resource transfer database failure");
    }
}

// 下列 helper 的锁顺序统一为父操作、排序资源、三个目标、原生回执及任务。
boost::asio::awaitable<std::optional<Snapshot>> snapshot(boost::mysql::pooled_connection &connection, model::ResourceIdentity identity);
boost::asio::awaitable<std::array<Target, 3>> targets(boost::mysql::pooled_connection &connection);
boost::asio::awaitable<std::optional<model::ResourceTransferOperation>> operation(boost::mysql::pooled_connection &connection, std::string id, bool lock);
// 父链最多两层，所有修改按 root→child 取锁，避免取消与子项完成反向等待。
boost::asio::awaitable<void> lockOperationTree(boost::mysql::pooled_connection &connection, const std::string &id);
boost::asio::awaitable<std::optional<model::ResourceTransferJob>> task(boost::mysql::pooled_connection &connection, std::uint64_t id);
model::ResourceTransferJob mapTask(boost::mysql::row_view row);
// 条件匹配本次认领及独占槽；可选检查内容、目标和维护闸门。
boost::asio::awaitable<bool> current(boost::mysql::pooled_connection &connection, const model::ResourceTransferJob &job, bool versions = true);
// 刷新父操作聚合，仅持有短事务锁，无网络或文件操作。
boost::asio::awaitable<bool> aggregate(boost::mysql::pooled_connection &connection, const std::string &id);
// 终结任务与本端认领；未决时保留独占槽，普通请求无法越过核对。
boost::asio::awaitable<bool> finish(boost::mysql::pooled_connection &connection, const model::ResourceTransferJob &job, std::string state, std::string result_code, std::string category, bool keep_slot = false);
// 登记、认领和单项写回的独立实现入口。
boost::asio::awaitable<model::ResourceTransferOperation> enqueue(std::shared_ptr<MySqlConnectionPool> pool, model::ResourceTransferRequest request, std::vector<model::ResourceTransferTargetProfile> profiles);
// 复用调用方事务，供旧兼容批次、已核实关联发现和 producer 原子完成共用。
boost::asio::awaitable<model::ResourceTransferOperation> enqueueOnConnection(boost::mysql::pooled_connection &connection, model::ResourceTransferRequest request, std::vector<model::ResourceTransferTargetProfile> profiles, std::optional<model::ResourceProducerTask> producer_authorization = {});
// 同事务取消受控两层子操作及 producer，不重新开启事务。
boost::asio::awaitable<bool> cancelOnConnection(boost::mysql::pooled_connection &connection, std::string operation_id, std::vector<model::ResourceDestination> destinations);
// 关联资源继承首次受理的 producer 配置，不按当前配置重选。
boost::asio::awaitable<model::ResourceTransferTargetProfile> frozenProfile(boost::mysql::pooled_connection &connection, const model::ResourceTransferJob &parent);
// 只允许受控的 root audio→related MRC→artifact 两层来源关系。
boost::asio::awaitable<std::optional<model::ResourceTransferJob>> childOrigin(boost::mysql::pooled_connection &connection, const model::ResourceTransferRequest &request, const std::optional<model::ResourceProducerTask> &authorization);
boost::asio::awaitable<void> validateChildren(boost::mysql::pooled_connection &connection, const model::ResourceTransferRequest &request, const model::ResourceTransferJob &parent, const std::optional<model::ResourceProducerTask> &authorization);
boost::asio::awaitable<std::optional<model::ResourceTransferJob>> claim(std::shared_ptr<MySqlConnectionPool> pool, model::ResourceDestination destination, std::string fingerprint, std::string adapter);
boost::asio::awaitable<bool> complete(std::shared_ptr<MySqlConnectionPool> pool, model::ResourceTransferJob job, model::ResourceTransferReceipt receipt);
// 受理私有查询会话，完整定义不扩散到其他持久化入口。
class PreparedQueries;
// 为单个资源目标判定策略并登记任务，调用方已锁定全部资源与目标。
boost::asio::awaitable<bool> registerItem(PreparedQueries &statements, model::ResourceTransferJob job, const std::optional<Snapshot> &resource, const std::array<Target, 3> &states, const model::ResourceTransferTargetProfile *profile);

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::transfer_jobs
