#include "MySqlResourceProducerTasksSupport.h"

#include <algorithm>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::producer_jobs {
namespace jobs = transfer_jobs;

boost::asio::awaitable<std::optional<model::ResourceProducerTask>> read(boost::mysql::pooled_connection &connection, std::uint64_t id, bool lock)
{
    auto statement = co_await connection->async_prepare_statement(sql::SqlFileLoader::loadOrThrow("sql/mysql/resource_producer_tasks/read.sql") + (lock ? " FOR UPDATE" : ""), boost::asio::use_awaitable);
    boost::mysql::results rows;
    co_await connection->async_execute(statement.bind(id), rows, boost::asio::use_awaitable);
    if (rows.rows().empty()) co_return std::nullopt;
    const auto row = rows.rows().front();
    const auto parent_id = MySqlValueReader::uint64At(row, 2);
    const auto parents = co_await jobs::query(connection, lock ? "resource_transfer_tasks/lock" : "resource_transfer_tasks/read", parent_id);
    if (parents.rows().empty()) throw std::logic_error("producer parent task missing");
    model::ResourceProducerTask value;
    value.producer_id = id;
    value.source_job = jobs::mapTask(parents.rows().front());
    if (value.source_job.operation_id != MySqlValueReader::stringAt(row, 1) || model::resourceDestinationName(value.source_job.destination) != MySqlValueReader::stringAt(row, 3)) throw std::logic_error("producer parent mismatch");
    value.processor_version = MySqlValueReader::stringAt(row, 4);
    value.schema_version = MySqlValueReader::intAt(row, 5);
    value.state = MySqlValueReader::stringAt(row, 6);
    value.claim_generation = MySqlValueReader::uint64At(row, 7);
    value.claimed_at = MySqlValueReader::uint64At(row, 8);
    value.attempt_count = MySqlValueReader::intAt(row, 9);
    value.cancel_requested = MySqlValueReader::uint64At(row, 10) != 0;
    value.policy = {MySqlValueReader::intAt(row, 11), MySqlValueReader::intAt(row, 12), MySqlValueReader::uint64At(row, 13), MySqlValueReader::uint64At(row, 14)};
    value.result_code = MySqlValueReader::stringAt(row, 15);
    value.child_operation_id = MySqlValueReader::stringAt(row, 16);
    co_return value;
}

boost::asio::awaitable<bool> current(boost::mysql::pooled_connection &connection, const model::ResourceProducerTask &value, bool running)
{
    const auto &parent = value.source_job;
    co_await jobs::lockOperationTree(connection, parent.operation_id);
    const auto operation = co_await jobs::query(connection, "resource_transfer_operations/lock", parent.operation_id);
    if (operation.rows().empty()) co_return false;
    const auto resource = co_await jobs::snapshot(connection, parent.resource);
    const auto targets = co_await jobs::targets(connection);
    const auto actual = co_await read(connection, value.producer_id);
    if (!actual || (running && (actual->state != "running" || actual->claim_generation != value.claim_generation)) || actual->cancel_requested || actual->source_job.cancel_requested || actual->source_job.state == "cancelled") co_return false;
    const auto &stored = actual->source_job;
    if (stored.task_id != parent.task_id || stored.operation_id != parent.operation_id || stored.resource != parent.resource || stored.destination != parent.destination || stored.version != parent.version || stored.source_revision != parent.source_revision || stored.source_identity != parent.source_identity || stored.target_fingerprint != parent.target_fingerprint || stored.adapter != parent.adapter || actual->processor_version != value.processor_version || actual->schema_version != value.schema_version) co_return false;
    if (!resource || resource->status == 4 || resource->generation != parent.version.content_generation || resource->source_revision != parent.source_revision || resource->source_identity != parent.source_identity) co_return false;
    if (!(co_await jobs::query(connection, "resource_baseline_blocks/find", std::string(model::resourceOwnerName(parent.resource.owner)), parent.resource.resource_id, parent.version.content_generation)).rows().empty()) co_return false;
    for (const auto &target : targets)
        if (!target.revision || target.state == "maintenance" || (target.state == "active" && (!target.baseline_ready || !jobs::hash(target.fingerprint)))) co_return false;
    const auto &target = targets.at(static_cast<std::size_t>(parent.destination));
    if (target.state != "active" || !target.baseline_ready || target.revision != parent.version.target_revision || target.fingerprint != parent.target_fingerprint) co_return false;
    const bool producer_only = stored.action == model::ResourceTransferAction::ProduceArtifacts && stored.state == "skipped" && stored.result_code == "producer_only";
    const auto admission = co_await jobs::query(connection, jobs::table(parent.resource.owner) + "/transfer_admission", std::string(model::resourceDestinationName(parent.destination)), parent.resource.resource_id);
    if (!producer_only && !admission.rows().empty()) {
        const auto row = admission.rows().front();
        if (MySqlValueReader::uint64At(row, 6) && MySqlValueReader::uint64At(row, 0) == MySqlValueReader::uint64At(row, 2) && MySqlValueReader::uint64At(row, 1) == MySqlValueReader::uint64At(row, 3) && !MySqlValueReader::stringAt(row, 4).empty() && !MySqlValueReader::stringAt(row, 5).empty()) co_return false;
    }
    co_return true;
}

boost::asio::awaitable<void> registerPlans(boost::mysql::pooled_connection &connection, const std::string &operation_id, const std::vector<model::ResourceTransferTargetProfile> &profiles)
{
    const auto tasks = co_await jobs::query(connection, "resource_transfer_tasks/list", operation_id);
    for (const auto row : tasks.rows()) {
        const auto parent = jobs::mapTask(row);
        const bool producer_only = parent.action == model::ResourceTransferAction::ProduceArtifacts && parent.state == "skipped" && parent.result_code == "producer_only";
        if (parent.resource.owner != model::ResourceOwner::Song || (parent.state != "pending" && parent.state != "succeeded" && !producer_only)) continue;
        const auto profile = std::find_if(profiles.begin(), profiles.end(), [&](const auto &p) { return p.destination == parent.destination; });
        if (profile == profiles.end() || !profile->produces_artifacts) continue;
        const auto kind = co_await jobs::query(connection, "song_resources/producer_kind", parent.resource.resource_id);
        if (kind.rows().empty() || MySqlValueReader::stringAt(kind.rows().front(), 0) != "mrc") continue;
        const auto &budget = profile->producer_policy;
        if (profile->producer_version.empty() || profile->producer_version.size() > 96 || !std::all_of(profile->producer_version.begin(), profile->producer_version.end(), [](unsigned char c) { return c >= 33 && c <= 126; }) || profile->producer_schema_version < 1 || budget.timeout_ms < 100 || budget.timeout_ms > 600000 || budget.retries < 0 || budget.retries > 5 || !budget.max_bytes || budget.max_bytes > std::uint64_t(1024) * 1024) throw std::invalid_argument("invalid producer configuration");
        (void)co_await jobs::query(connection, "resource_producer_tasks/insert", operation_id, parent.task_id, std::string(model::resourceDestinationName(parent.destination)), profile->producer_version, profile->producer_schema_version, profile->producer_key_available ? std::string("pending") : std::string("waiting_input"), budget.timeout_ms, budget.retries, budget.max_bytes, budget.bytes_per_second, profile->producer_key_available ? std::string {} : std::string("producer_key_required"));
    }
}

boost::asio::awaitable<bool> finish(boost::mysql::pooled_connection &connection, const model::ResourceProducerTask &value, std::string state, std::string code, std::string category, std::string child)
{
    const auto changed = co_await jobs::query(connection, "resource_producer_tasks/finish", state, code, category, child, state == "retry" || state == "waiting_input" ? jobs::now() + 2 : std::uint64_t(0), value.producer_id, value.claim_generation);
    if (!changed.affected_rows()) co_return false;
    (void)co_await jobs::aggregate(connection, value.source_job.operation_id);
    co_return true;
}

boost::asio::awaitable<void> cancel(boost::mysql::pooled_connection &connection, const std::string &operation_id, const std::vector<model::ResourceDestination> &destinations)
{
    for (const auto destination : {model::ResourceDestination::Local, model::ResourceDestination::Baidu, model::ResourceDestination::Gdrive})
        if (destinations.empty() || std::find(destinations.begin(), destinations.end(), destination) != destinations.end()) (void)co_await jobs::query(connection, "resource_producer_tasks/cancel", operation_id, std::string(model::resourceDestinationName(destination)));
}
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::producer_jobs
