#include "MySqlResourceTransferJobsSupport.h"

#include <algorithm>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::transfer_jobs {

std::string table(model::ResourceOwner owner)
{
    switch (owner) {
    case model::ResourceOwner::Song: return "song_resources";
    case model::ResourceOwner::Singer: return "singer_resources";
    case model::ResourceOwner::Album: return "album_resources";
    case model::ResourceOwner::SongArtifact: return "song_resource_artifacts";
    }
    throw std::invalid_argument("invalid resource owner");
}

bool hash(std::string_view value)
{
    return value.size() == 64 && std::all_of(value.begin(), value.end(), [](char ch) { return (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f'); });
}

bool code(std::string_view value)
{
    return value.size() <= 64 && std::all_of(value.begin(), value.end(), [](char ch) { return (ch >= 'a' && ch <= 'z') || (ch >= '0' && ch <= '9') || ch == '_'; });
}

std::uint64_t now()
{
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}

boost::asio::awaitable<std::optional<Snapshot>> snapshot(boost::mysql::pooled_connection &connection, model::ResourceIdentity identity)
{
    const auto rows = co_await query(connection, table(identity.owner) + "/transfer_snapshot", identity.resource_id);
    if (rows.rows().empty()) co_return std::nullopt;
    const auto row = rows.rows().front();
    Snapshot value;
    value.identity = identity;
    value.generation = MySqlValueReader::uint64At(row, 1);
    value.source_revision = MySqlValueReader::uint64At(row, 2);
    value.source_identity = MySqlValueReader::stringAt(row, 3);
    value.status = MySqlValueReader::intAt(row, 4);
    value.claim_generation = MySqlValueReader::uint64At(row, 5);
    value.running_batch_id = MySqlValueReader::stringAt(row, 6);
    value.reference_sha256 = MySqlValueReader::stringAt(row, 7);
    value.reference_size = MySqlValueReader::uint64At(row, 8);
    value.local_generation = MySqlValueReader::uint64At(row, 9);
    value.local_revision = MySqlValueReader::uint64At(row, 10);
    value.skip.version = {MySqlValueReader::uint64At(row, 11), MySqlValueReader::uint64At(row, 12)};
    value.skip.baseline_id = MySqlValueReader::stringAt(row, 13);
    value.skip.reason = MySqlValueReader::stringAt(row, 14);
    value.skip.enabled = value.skip.version.content_generation > 0;
    value.local_path = MySqlValueReader::stringAt(row, 15);
    value.checksum = MySqlValueReader::stringAt(row, 16);
    value.size_bytes = MySqlValueReader::uint64At(row, 17);
    co_return value;
}

boost::asio::awaitable<std::array<Target, 3>> targets(boost::mysql::pooled_connection &connection)
{
    const auto rows = co_await query(connection, "resource_destination_states/transfer_lock");
    std::array<Target, 3> values {};
    for (const auto row : rows.rows()) {
        const auto destination = model::parseResourceDestination(MySqlValueReader::stringAt(row, 0));
        if (!destination) throw std::logic_error("unknown destination");
        auto &value = values.at(static_cast<std::size_t>(*destination));
        value.destination = *destination;
        value.revision = MySqlValueReader::uint64At(row, 1);
        value.fingerprint = MySqlValueReader::stringAt(row, 2);
        value.state = MySqlValueReader::stringAt(row, 3);
        value.baseline_ready = MySqlValueReader::uint64At(row, 4) != 0;
    }
    co_return values;
}

model::ResourceTransferJob mapTask(boost::mysql::row_view row)
{
    model::ResourceTransferJob value;
    const auto owner = model::parseResourceOwner(MySqlValueReader::stringAt(row, 2));
    const auto destination = model::parseResourceDestination(MySqlValueReader::stringAt(row, 4));
    const auto action = model::parseResourceTransferAction(MySqlValueReader::stringAt(row, 25));
    if (!owner || !destination || !action) throw std::logic_error("invalid persisted task");
    value.task_id = MySqlValueReader::uint64At(row, 0);
    value.operation_id = MySqlValueReader::stringAt(row, 1);
    value.resource = {*owner, MySqlValueReader::uint64At(row, 3)};
    value.destination = *destination;
    value.version = {MySqlValueReader::uint64At(row, 5), MySqlValueReader::uint64At(row, 6)};
    value.source_revision = MySqlValueReader::uint64At(row, 7);
    value.source_identity = MySqlValueReader::stringAt(row, 8);
    value.target_fingerprint = MySqlValueReader::stringAt(row, 9);
    value.claim_generation = MySqlValueReader::uint64At(row, 10);
    value.native_claim_generation = MySqlValueReader::uint64At(row, 11);
    value.claimed_at = MySqlValueReader::uint64At(row, 12);
    value.attempt_count = MySqlValueReader::intAt(row, 13);
    value.policy = {MySqlValueReader::intAt(row, 14), MySqlValueReader::intAt(row, 15), MySqlValueReader::uint64At(row, 16), MySqlValueReader::uint64At(row, 17)};
    value.state = MySqlValueReader::stringAt(row, 18);
    value.result_code = MySqlValueReader::stringAt(row, 19);
    value.cancel_requested = MySqlValueReader::uint64At(row, 20) != 0;
    value.busy_task_id = MySqlValueReader::uint64At(row, 21);
    value.attempt_path = MySqlValueReader::stringAt(row, 22);
    value.attempt_checksum = MySqlValueReader::stringAt(row, 23);
    value.attempt_size_bytes = MySqlValueReader::uint64At(row, 24);
    value.action = *action;
    value.adapter = MySqlValueReader::stringAt(row, 26);
    value.policy.strict_sha256 = MySqlValueReader::uint64At(row, 27) != 0;
    value.process_related_resources = MySqlValueReader::uint64At(row, 28) != 0;
    value.source_options = {MySqlValueReader::intAt(row, 29), MySqlValueReader::uint64At(row, 30) != 0, MySqlValueReader::uint64At(row, 31) != 0};
    return value;
}

boost::asio::awaitable<std::optional<model::ResourceTransferOperation>> operation(boost::mysql::pooled_connection &connection, std::string id, bool lock)
{
    if (lock) co_await lockOperationTree(connection, id);
    const auto header = co_await query(connection, lock ? "resource_transfer_operations/lock" : "resource_transfer_operations/find", id);
    if (header.rows().empty()) co_return std::nullopt;
    model::ResourceTransferOperation value;
    value.operation_id = MySqlValueReader::stringAt(header.rows().front(), 0);
    value.state = MySqlValueReader::stringAt(header.rows().front(), 1);
    value.logical_resource_count = MySqlValueReader::uint64At(header.rows().front(), 2);
    value.parent_operation_id = MySqlValueReader::stringAt(header.rows().front(), 5);
    value.parent_task_id = MySqlValueReader::uint64At(header.rows().front(), 6);
    const auto items = co_await query(connection, "resource_transfer_tasks/list", id);
    for (const auto row : items.rows()) value.tasks.push_back(mapTask(row));
    if (!lock) {
        const auto children = co_await query(connection, "resource_transfer_operations/children", id);
        if (children.rows().size() > 600) throw std::logic_error("unbounded producer children");
        for (const auto row : children.rows()) value.child_operation_ids.push_back(MySqlValueReader::stringAt(row, 0));
    }
    co_return value;
}

boost::asio::awaitable<std::optional<model::ResourceTransferJob>> task(boost::mysql::pooled_connection &connection, std::uint64_t id)
{
    const auto rows = co_await query(connection, "resource_transfer_tasks/lock", id);
    if (rows.rows().empty()) co_return std::nullopt;
    co_return mapTask(rows.rows().front());
}

boost::asio::awaitable<bool> current(boost::mysql::pooled_connection &connection, const model::ResourceTransferJob &job, bool versions)
{
    co_await lockOperationTree(connection, job.operation_id);
    // 高频版本/取消检查只锁父行，不反复加载同操作最多 600 条明细。
    const auto parent = co_await query(connection, "resource_transfer_operations/lock", job.operation_id);
    if (parent.rows().empty()) co_return false;
    const auto resource = co_await snapshot(connection, job.resource);
    const auto states = co_await targets(connection);
    const auto actual = co_await task(connection, job.task_id);
    if (!actual || actual->operation_id != job.operation_id || actual->resource != job.resource || actual->destination != job.destination || actual->state != "running" || actual->claim_generation != job.claim_generation || actual->native_claim_generation != job.native_claim_generation || actual->version != job.version || actual->source_revision != job.source_revision || actual->source_identity != job.source_identity || actual->target_fingerprint != job.target_fingerprint) co_return false;
    if (actual->adapter != job.adapter || !model::supportsResourceTransferAdapter(job.destination, job.adapter)) co_return false;
    const auto slot = co_await query(connection, "resource_transfer_slots/lock", std::string(model::resourceOwnerName(job.resource.owner)), job.resource.resource_id, std::string(model::resourceDestinationName(job.destination)));
    if (slot.rows().empty() || MySqlValueReader::uint64At(slot.rows().front(), 0) != job.task_id) co_return false;
    if (!versions) co_return true;
    if (!(co_await query(connection, "resource_baseline_blocks/find", std::string(model::resourceOwnerName(job.resource.owner)), job.resource.resource_id, job.version.content_generation)).rows().empty()) co_return false;
    const auto &target = states.at(static_cast<std::size_t>(job.destination));
    if (!resource || actual->cancel_requested || resource->status == 4 || resource->generation != job.version.content_generation || resource->source_revision != job.source_revision || resource->source_identity != job.source_identity || target.revision != job.version.target_revision || target.fingerprint != job.target_fingerprint || target.state != "active" || !target.baseline_ready || !hash(target.fingerprint)) co_return false;
    for (const auto &state : states)
        if (state.revision == 0 || state.state == "maintenance" || (state.state == "active" && (!state.baseline_ready || !hash(state.fingerprint)))) co_return false;
    const auto batch = "rt:" + std::to_string(job.task_id);
    if (job.destination == model::ResourceDestination::Local)
        co_return resource->claim_generation == job.native_claim_generation && resource->running_batch_id == batch;
    const auto receipt = co_await query(connection, "resource_cloud_receipts/transfer_lock", std::string(model::resourceOwnerName(job.resource.owner)), job.resource.resource_id, std::string(model::resourceDestinationName(job.destination)));
    co_return !receipt.rows().empty() && MySqlValueReader::uint64At(receipt.rows().front(), 1) == job.native_claim_generation && MySqlValueReader::stringAt(receipt.rows().front(), 2) == batch;
}

boost::asio::awaitable<bool> aggregate(boost::mysql::pooled_connection &connection, const std::string &id)
{
    co_await lockOperationTree(connection, id);
    auto current_id = id;
    for (int depth = 0; depth <= 2 && !current_id.empty(); ++depth) {
        (void)co_await query(connection, "resource_transfer_operations/aggregate", current_id);
        const auto parent = co_await query(connection, "resource_transfer_operations/parent", current_id);
        current_id = parent.rows().empty() ? std::string {} : MySqlValueReader::stringAt(parent.rows().front(), 0);
    }
    co_return true;
}

boost::asio::awaitable<void> lockOperationTree(boost::mysql::pooled_connection &connection, const std::string &id)
{
    std::vector<std::string> chain {id};
    for (int depth = 0; depth <= 2; ++depth) {
        const auto rows = co_await query(connection, "resource_transfer_operations/parent", chain.back());
        if (rows.rows().empty()) break;
        const auto parent = MySqlValueReader::stringAt(rows.rows().front(), 0);
        if (parent.empty()) break;
        if (depth == 2 || std::find(chain.begin(), chain.end(), parent) != chain.end()) throw std::logic_error("invalid operation ancestry");
        chain.push_back(parent);
    }
    for (auto it = chain.rbegin(); it != chain.rend(); ++it) (void)co_await query(connection, "resource_transfer_operations/lock", *it);
}

boost::asio::awaitable<bool> finish(boost::mysql::pooled_connection &connection, const model::ResourceTransferJob &job, std::string state, std::string result_code, std::string category, bool keep_slot)
{
    const auto changed = co_await query(connection, "resource_transfer_tasks/finish", state, result_code, category, state == "retry" ? now() + 2 : std::uint64_t(0), job.task_id, job.claim_generation);
    if (!changed.affected_rows()) co_return false;
    const auto batch = "rt:" + std::to_string(job.task_id);
    if (job.destination == model::ResourceDestination::Local) {
        if (job.resource.owner == model::ResourceOwner::SongArtifact)
            (void)co_await query(connection, table(job.resource.owner) + "/transfer_finish", result_code, job.resource.resource_id, job.native_claim_generation, batch);
        else
            (void)co_await query(connection, table(job.resource.owner) + "/transfer_finish", result_code, category, job.resource.resource_id, job.native_claim_generation, batch);
    } else {
        (void)co_await query(connection, "resource_cloud_receipts/transfer_finish", state, result_code, category, std::string(model::resourceOwnerName(job.resource.owner)), job.resource.resource_id, std::string(model::resourceDestinationName(job.destination)), job.native_claim_generation, batch);
    }
    if (!keep_slot && state != "retry") (void)co_await query(connection, "resource_transfer_slots/release", job.task_id);
    (void)co_await aggregate(connection, job.operation_id);
    co_return true;
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::transfer_jobs
