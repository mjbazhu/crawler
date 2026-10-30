#include "MySqlResourceTransferJobsSupport.h"
#include "MySqlResourceProducerTasksSupport.h"

#include <algorithm>
#include <nlohmann/json.hpp>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::transfer_jobs {

boost::asio::awaitable<model::ResourceTransferTargetProfile> frozenProfile(boost::mysql::pooled_connection &connection, const model::ResourceTransferJob &parent)
{
    model::ResourceTransferTargetProfile profile;
    profile.destination = parent.destination;
    profile.fingerprint = parent.target_fingerprint;
    profile.adapter = parent.adapter;
    profile.policy = parent.policy;
    const auto rows = co_await query(connection, "resource_transfer_operations/frozen_profile", parent.operation_id);
    if (rows.rows().empty()) throw std::logic_error("frozen profile missing");
    const auto frozen = nlohmann::json::parse(MySqlValueReader::stringAt(rows.rows().front(), 0));
    const auto name = std::string(model::resourceDestinationName(parent.destination));
    if (frozen.contains("producers") && frozen["producers"].contains(name)) {
        const auto &value = frozen["producers"][name];
        profile.produces_artifacts = value.value("enabled", false);
        profile.producer_version = value.value("version", std::string {});
        profile.producer_schema_version = value.value("schema", 1);
        profile.producer_key_available = value.value("key_available", false);
        profile.producer_policy = {value.value("timeout_ms", 120000), value.value("retries", 1), value.value("max_bytes", std::uint64_t(1024 * 1024)), value.value("bytes_per_second", std::uint64_t(0))};
    }
    co_return profile;
}

boost::asio::awaitable<std::optional<model::ResourceTransferJob>> childOrigin(boost::mysql::pooled_connection &connection, const model::ResourceTransferRequest &request, const std::optional<model::ResourceProducerTask> &authorization)
{
    if (request.parent_operation_id.empty()) {
        if (authorization || !request.child_kind.empty()) throw std::invalid_argument("unexpected child authorization");
        co_return std::nullopt;
    }
    const bool related = request.child_kind == "related";
    if (request.process_related_resources || (related && authorization)) throw std::invalid_argument("nested discovery is not allowed");
    const auto parent = co_await operation(connection, request.parent_operation_id, true);
    if (!parent) throw std::logic_error("child parent missing");
    if (!parent->parent_operation_id.empty()) {
        const auto ancestor = co_await operation(connection, parent->parent_operation_id, true);
        const auto raw = co_await query(connection, "resource_transfer_operations/frozen_profile", parent->operation_id);
        if (related || !ancestor || !ancestor->parent_operation_id.empty() || raw.rows().empty() || nlohmann::json::parse(MySqlValueReader::stringAt(raw.rows().front(), 0)).value("child_kind", std::string {}) != "related") throw std::logic_error("child depth or origin invalid");
    }
    const auto found = std::find_if(parent->tasks.begin(), parent->tasks.end(), [&](const auto &item) { return item.task_id == request.parent_task_id; });
    if (found == parent->tasks.end() || found->resource.owner != model::ResourceOwner::Song || found->destination != request.destinations.front()) throw std::logic_error("child parent identity changed");
    if (!authorization && found->claim_generation != request.parent_claim_generation) throw std::logic_error("child parent claim changed");
    if (authorization && (authorization->source_job.task_id != found->task_id || authorization->source_job.operation_id != found->operation_id)) throw std::logic_error("producer parent identity changed");
    const auto expected = std::string(related ? "related:" : "producer:") + found->operation_id + ':' + std::to_string(found->resource.resource_id) + ':' + std::string(model::resourceDestinationName(found->destination));
    if (request.idempotency_key != expected) throw std::invalid_argument("invalid child idempotency key");
    (void)co_await snapshot(connection, found->resource);
    co_return *found;
}

boost::asio::awaitable<void> validateChildren(boost::mysql::pooled_connection &connection, const model::ResourceTransferRequest &request, const model::ResourceTransferJob &parent, const std::optional<model::ResourceProducerTask> &authorization)
{
    if (authorization) {
        if (!(co_await producer_jobs::current(connection, *authorization))) throw std::logic_error("producer no longer current");
    } else if (!(co_await current(connection, parent)))
        throw std::logic_error("child parent no longer current");
    const bool related = request.child_kind == "related";
    const auto kind = co_await query(connection, "song_resources/producer_kind", parent.resource.resource_id);
    if (kind.rows().empty() || MySqlValueReader::stringAt(kind.rows().front(), 0) != (related ? "audio" : "mrc")) throw std::logic_error("invalid child source kind");
    for (const auto identity : request.resources) {
        if (identity.owner != (related ? model::ResourceOwner::Song : model::ResourceOwner::SongArtifact)) throw std::logic_error("invalid child owner");
        const auto child = related ? co_await query(connection, "song_resources/related_child_authorization", parent.task_id, parent.resource.resource_id, identity.resource_id) : co_await query(connection, "song_resource_artifacts/producer_parent", identity.resource_id, parent.resource.resource_id);
        if (child.rows().empty()) throw std::logic_error("child resource outside confirmed scope");
    }
}
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::transfer_jobs
