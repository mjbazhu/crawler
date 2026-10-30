#include "MySqlLegacyResourceImportSupport.h"

#include <boost/asio/use_awaitable.hpp>
#include <nlohmann/json.hpp>

#include <array>
#include <exception>
#include <map>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::legacy_import {
namespace {
// 校验清单的固定范围，不允许把多个任意资源页拼成跳跃游标。
void validate(const model::LegacyResourceImportPlan &plan, const model::LegacyResourceImportPage &page)
{
    if (plan.operator_attested_both == plan.operator_excluded_all || plan.run_id.empty() || plan.run_id.size() > 96 || !hash(plan.manifest_hash) || !hash(page.database_identity) || page.rows.size() > 5000 || page.after_id > page.next_id || page.next_id > page.high_water_id)
        throw std::invalid_argument("invalid historical import plan");
    if (plan.legacy_rclone_provider == model::ResourceDestination::Local) throw std::invalid_argument("local cannot identify rclone");
    if (model::resourceOwnerName(page.owner).empty()) throw std::invalid_argument("invalid import owner");
    for (std::size_t i = 0; i < page.targets.size(); ++i)
        if (page.targets[i].destination != static_cast<model::ResourceDestination>(i) || !page.targets[i].revision || !hash(page.targets[i].fingerprint))
            throw std::invalid_argument("target identity has not been reviewed");
    auto previous = page.after_id;
    for (const auto &row : page.rows) {
        if (row.identity.owner != page.owner || row.identity.resource_id <= previous || row.identity.resource_id > page.next_id || !row.generation || row.legacy_values.size() > 32768)
            throw std::invalid_argument("invalid import resource order");
        const auto evidence = nlohmann::json::parse(row.legacy_values, nullptr, false);
        if (!evidence.is_object()) throw std::invalid_argument("invalid import evidence");
        previous = row.identity.resource_id;
    }
}

// 工具证据和旧路径只归属于明确的平台；不能把 baidupcs 当成另一端的实际 adapter。
std::string historicalAdapter(const model::LegacyResourceImportPlan &plan, const model::LegacyResourceSnapshot &row, model::ResourceDestination provider)
{
    const bool baidu = row.cloud_backend == "baidupcs" && provider == model::ResourceDestination::Baidu;
    const bool mapped = row.cloud_backend == "rclone" && plan.legacy_rclone_provider == provider;
    return baidu || mapped ? row.cloud_backend : std::string {};
}

// 一次写入某平台策略；已确认路径/摘要/核验等级由 upsert 保留。
boost::asio::awaitable<void> importReceipt(boost::mysql::pooled_connection &connection, const boost::mysql::statement &statement, const model::LegacyResourceImportPlan &plan, const model::LegacyResourceSnapshot &row, const model::LegacyResourceTarget &target)
{
    const std::string owner(model::resourceOwnerName(row.identity.owner)), provider(model::resourceDestinationName(target.destination));
    const auto id = row.identity.resource_id, generation = row.generation;
    const auto adapter = historicalAdapter(plan, row, target.destination);
    const auto path = adapter.empty() ? std::string {} : row.cloud_path;
    auto facts = nlohmann::json {{"run_id", plan.run_id}, {"manifest_sha256", plan.manifest_hash}, {"source_identity", row.source_identity}, {"basis", "operator_confirmed_both_clouds_present"}};
    // 只有已明确映射的平台才能投影旧云事实；它不是本次上传/核验结果。
    if (!adapter.empty()) {
        const auto legacy = nlohmann::json::parse(row.legacy_values);
        auto retired = nlohmann::json::object();
        for (const auto &[key, value] : legacy.items())
            if (key.starts_with("cloud_")) retired[key] = value;
        facts["retired_legacy"] = std::move(retired);
    }
    const auto evidence = facts.dump();
    boost::mysql::results result;
    co_await connection->async_execute(statement.bind(owner, id, provider, row.identity.owner == model::ResourceOwner::Song ? id : 0, row.identity.owner == model::ResourceOwner::Singer ? id : 0, row.identity.owner == model::ResourceOwner::Album ? id : 0, row.identity.owner == model::ResourceOwner::SongArtifact ? id : 0, generation, target.revision, generation, target.revision, plan.run_id, path, path.empty() ? 1 : 0, adapter, evidence), result, boost::asio::use_awaitable);
}

// 对照固定清单绑定两个 provider 的游标，不隐式改变已有清单。
boost::asio::awaitable<std::pair<std::uint64_t, bool>> lockRuns(boost::mysql::pooled_connection &connection, const model::LegacyResourceImportPlan &plan, const model::LegacyResourceImportPage &page)
{
    auto start = co_await connection->async_prepare_statement(load("resource_cloud_backfills/start_import"), boost::asio::use_awaitable);
    auto lock = co_await connection->async_prepare_statement(load("resource_cloud_backfills/lock"), boost::asio::use_awaitable);
    std::array<std::uint64_t, 2> cursors {};
    bool completed = true;
    for (std::size_t i = 0; i < 2; ++i) {
        const auto &target = page.targets[i + 1];
        const std::string provider(model::resourceDestinationName(target.destination)), owner(model::resourceOwnerName(page.owner));
        const auto run_id = plan.run_id + ':' + provider;
        boost::mysql::results result;
        co_await connection->async_execute(start.bind(run_id, plan.manifest_hash, page.database_identity, owner, provider, target.revision, page.high_water_id), result, boost::asio::use_awaitable);
        co_await connection->async_execute(lock.bind(run_id), result, boost::asio::use_awaitable);
        if (result.rows().size() != 1) throw std::logic_error("import run is missing");
        const auto row = result.rows().front();
        if (MySqlValueReader::stringAt(row, 8) != "active" || MySqlValueReader::uint64At(row, 9) != plan.control_generation || MySqlValueReader::stringAt(row, 10) != "import_attested_skip") throw std::logic_error("import control changed");
        if (MySqlValueReader::stringAt(row, 0) != plan.manifest_hash || MySqlValueReader::stringAt(row, 1) != page.database_identity || MySqlValueReader::stringAt(row, 2) != owner || MySqlValueReader::stringAt(row, 3) != provider || MySqlValueReader::uint64At(row, 4) != target.revision || MySqlValueReader::uint64At(row, 5) != page.high_water_id || MySqlValueReader::stringAt(row, 7) == "cancelled")
            throw std::logic_error("import run changed");
        cursors[i] = MySqlValueReader::uint64At(row, 6);
        completed = completed && MySqlValueReader::stringAt(row, 7) == "completed";
    }
    if (cursors[0] != cursors[1]) throw std::logic_error("provider cursors diverged");
    if (cursors[0] >= page.next_id && (page.next_id > page.after_id || completed)) co_return std::make_pair(cursors[0], true);
    if (cursors[0] != page.after_id) throw std::logic_error("import cursor changed");
    co_return std::make_pair(cursors[0], false);
}
} // namespace

boost::asio::awaitable<model::LegacyResourceImportResult> apply(std::shared_ptr<MySqlConnectionPool> pool, model::LegacyResourceImportPlan plan, model::LegacyResourceImportPage page)
{
    validate(plan, page);
    auto connection = co_await pool->coAcquire();
    boost::mysql::results result;
    co_await connection->async_execute(load("transactions/transaction_begin"), result, boost::asio::use_awaitable);
    std::exception_ptr failure;
    bool committing = false;
    model::LegacyResourceImportResult summary;
    summary.next_id = page.next_id;
    try {
        if (co_await databaseIdentity(connection) != page.database_identity) throw std::logic_error("wrong import database");
        // 只锁当前资源与来源；不可变档案的 JSON_TABLE 投影不能参与隐式锁定。
        auto read = co_await connection->async_prepare_statement(load(table(page.owner) + "/cloud_import_snapshot") + " FOR UPDATE OF r,s", boost::asio::use_awaitable);
        co_await connection->async_execute(read.bind(page.after_id, page.next_id, std::uint64_t(5001)), result, boost::asio::use_awaitable);
        std::map<std::uint64_t, model::LegacyResourceSnapshot> current;
        for (const auto row : result.rows()) {
            auto value = snapshot(page.owner, row);
            current.emplace(value.identity.resource_id, std::move(value));
        }
        if (current.size() > 5000) throw std::invalid_argument("import page is not bounded");
        // 两侧均按主键排序，线性检查漏项，避免每页产生平方级比较。
        std::size_t expected_index = 0;
        for (const auto &[id, ignored] : current) {
            while (expected_index < page.rows.size() && page.rows[expected_index].identity.resource_id < id) ++expected_index;
            if (expected_index == page.rows.size() || page.rows[expected_index].identity.resource_id != id)
                throw std::logic_error("manifest omitted a current resource");
        }
        const auto actual_targets = co_await targets(connection, true);
        for (std::size_t i = 0; i < actual_targets.size(); ++i)
            if (actual_targets[i].revision != page.targets[i].revision || actual_targets[i].fingerprint != page.targets[i].fingerprint || actual_targets[i].dispatch_state != "maintenance")
                throw std::logic_error("target changed or dispatch is active");
        const auto [cursor, already_applied] = co_await lockRuns(connection, plan, page);
        summary.already_applied = already_applied;
        if (already_applied) summary.next_id = cursor;
        if (!summary.already_applied) {
            auto audit = co_await connection->async_prepare_statement(load("resource_cloud_migration_audit/import_batch"), boost::asio::use_awaitable);
            auto lock = co_await connection->async_prepare_statement(load("resource_cloud_receipts/lock_import"), boost::asio::use_awaitable);
            auto active = co_await connection->async_prepare_statement(load("resource_cloud_receipts/import_active_transfer"), boost::asio::use_awaitable);
            auto receipt = co_await connection->async_prepare_statement(load("resource_cloud_receipts/import_attested"), boost::asio::use_awaitable);
            auto local = co_await connection->async_prepare_statement(load(table(page.owner) + "/cloud_import_local_skip"), boost::asio::use_awaitable);
            const std::string owner(model::resourceOwnerName(page.owner));
            std::string audit_rows = "["; // 页内审计分块，4MiB上限避免最大清单页产生超大参数。
            constexpr std::size_t audit_batch_bytes = 4 * 1024 * 1024;
            for (const auto &expected : page.rows) {
                const auto found = current.find(expected.identity.resource_id);
                std::string disposition = "imported";
                std::size_t existing = 0;
                if (found == current.end() || found->second.generation != expected.generation || found->second.source_identity != expected.source_identity || found->second.values_digest != expected.values_digest || !hash(expected.source_identity))
                    disposition = "conflict";
                else if (plan.operator_excluded_all || found->second.status != 2)
                    disposition = "excluded";
                else {
                    boost::mysql::results active_tasks;
                    co_await connection->async_execute(active.bind(owner, expected.identity.resource_id), active_tasks, boost::asio::use_awaitable);
                    if (!active_tasks.rows().empty()) disposition = "conflict";
                    co_await connection->async_execute(lock.bind(owner, expected.identity.resource_id), result, boost::asio::use_awaitable);
                    existing = result.rows().size();
                    for (auto row : result.rows()) {
                        const auto provider = model::parseCloudProvider(MySqlValueReader::stringAt(row, 0));
                        if (!provider || MySqlValueReader::stringAt(row, 1) == "running" || MySqlValueReader::stringAt(row, 1) == "uncertain" || MySqlValueReader::uint64At(row, 2) > expected.generation || MySqlValueReader::uint64At(row, 3) > page.targets[static_cast<std::size_t>(*provider)].revision)
                            disposition = "conflict";
                    }
                    if (disposition == "imported") {
                        const auto &value = found->second;
                        co_await connection->async_execute(local.bind(value.generation, page.targets[0].revision, plan.run_id, value.identity.resource_id, value.generation), result, boost::asio::use_awaitable);
                        // 同值重跑的 UPDATE 允许零变更，资源行仍在本事务锁内。
                        co_await importReceipt(connection, receipt, plan, value, page.targets[1]);
                        co_await importReceipt(connection, receipt, plan, value, page.targets[2]);
                        summary.new_receipts += 2 - existing;
                        ++summary.imported_resources;
                    }
                }
                if (disposition == "conflict") ++summary.conflicts;
                if (disposition == "excluded") ++summary.excluded_resources;
                const auto audit_row = nlohmann::json {{"resource_id", expected.identity.resource_id}, {"source_identity", expected.source_identity}, {"content_generation", expected.generation}, {"old_values", expected.legacy_values}, {"disposition", disposition}}.dump();
                if (audit_row.size() + 2 > audit_batch_bytes) throw std::invalid_argument("import audit row exceeds bound");
                if (audit_rows.size() + audit_row.size() + 2 > audit_batch_bytes) {
                    audit_rows += ']';
                    co_await connection->async_execute(audit.bind(plan.run_id, owner, audit_rows), result, boost::asio::use_awaitable);
                    audit_rows = "[";
                }
                if (audit_rows.size() > 1) audit_rows += ',';
                audit_rows += audit_row;
            }
            if (audit_rows.size() > 1) {
                audit_rows += ']';
                co_await connection->async_execute(audit.bind(plan.run_id, owner, audit_rows), result, boost::asio::use_awaitable);
            }
            auto finish = co_await connection->async_prepare_statement(load("resource_cloud_backfills/finish_page"), boost::asio::use_awaitable);
            for (const auto provider : {model::ResourceDestination::Baidu, model::ResourceDestination::Gdrive}) {
                const auto run_id = plan.run_id + ':' + std::string(model::resourceDestinationName(provider));
                const std::string state = page.next_id == page.high_water_id ? "completed" : "paused";
                co_await connection->async_execute(finish.bind(page.next_id, static_cast<std::uint64_t>(page.rows.size()), static_cast<std::uint64_t>(summary.imported_resources), static_cast<std::uint64_t>(summary.conflicts), state, run_id, plan.manifest_hash, page.after_id), result, boost::asio::use_awaitable);
                if (result.affected_rows() != 1) throw std::logic_error("import page cursor changed");
            }
        }
        committing = true;
        co_await connection->async_execute(load("transactions/transaction_commit"), result, boost::asio::use_awaitable);
        co_return summary;
    } catch (...) {
        failure = std::current_exception();
    }
    if (committing) throw CommitUnknown();
    try {
        co_await connection->async_execute(load("transactions/transaction_rollback"), result, boost::asio::use_awaitable);
    } catch (...) {
        // 未发出 COMMIT 的连接归还时重置会话，不将普通语句错误写成确定成功。
    }
    std::rethrow_exception(failure);
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::legacy_import
