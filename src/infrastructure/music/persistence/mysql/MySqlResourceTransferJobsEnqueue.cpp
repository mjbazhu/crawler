#include "MySqlResourceTransferJobsSupport.h"
#include "MySqlResourceTransferPreparedQueries.h"
#include "MySqlResourceTransferBatch.h"
#include "MySqlResourceQueueCapacitySupport.h"
#include "MySqlResourceProducerTasksSupport.h"

#include <nlohmann/json.hpp>
#include <openssl/evp.h>
#include <openssl/rand.h>

#include <algorithm>
#include <map>
#include <tuple>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::transfer_jobs {
namespace {
// 只对受控请求计算摘要，不记录认证、路径或来源正文。
std::string digest(std::string_view text)
{
    std::array<unsigned char, 32> bytes {};
    unsigned int size = 0;
    if (EVP_Digest(text.data(), text.size(), bytes.data(), &size, EVP_sha256(), nullptr) != 1 || size != bytes.size()) throw std::runtime_error("request digest failed");
    static constexpr char hex[] = "0123456789abcdef";
    std::string value;
    for (const auto byte : bytes) {
        value.push_back(hex[byte >> 4]);
        value.push_back(hex[byte & 15]);
    }
    return value;
}

// 随机操作 ID 不使用用户输入，避免跨操作文件身份碰撞。
std::string operationId()
{
    std::array<unsigned char, 16> bytes {};
    if (RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) != 1) throw std::runtime_error("operation identity failed");
    static constexpr char hex[] = "0123456789abcdef";
    std::string value = "rt_";
    for (const auto byte : bytes) {
        value.push_back(hex[byte >> 4]);
        value.push_back(hex[byte & 15]);
    }
    return value;
}

// 请求按语义排序；缺省目标仍保持缺省，不随热配置重新展开。
nlohmann::json canonical(model::ResourceTransferRequest &request, bool durable_producer)
{
    if (request.resources.empty() || request.resources.size() > 200 || request.destinations.size() > 3 || request.idempotency_key.size() > 128 || model::resourceTransferActionName(request.action).empty()) throw std::invalid_argument("unbounded request");
    if (!std::all_of(request.idempotency_key.begin(), request.idempotency_key.end(), [](unsigned char ch) { return ch >= 33 && ch <= 126; })) throw std::invalid_argument("invalid idempotency key");
    std::sort(request.resources.begin(), request.resources.end(), [](const auto &a, const auto &b) { return std::tie(a.owner, a.resource_id) < std::tie(b.owner, b.resource_id); });
    if (std::adjacent_find(request.resources.begin(), request.resources.end()) != request.resources.end()) throw std::invalid_argument("duplicate resource");
    std::sort(request.destinations.begin(), request.destinations.end());
    if (std::adjacent_find(request.destinations.begin(), request.destinations.end()) != request.destinations.end()) throw std::invalid_argument("duplicate destination");
    auto resources = nlohmann::json::array(), destinations = nlohmann::json::array();
    for (const auto &resource : request.resources) {
        if (resource.resource_id == 0 || model::resourceOwnerName(resource.owner).empty()) throw std::invalid_argument("invalid identity");
        resources.push_back({{"owner_type", model::resourceOwnerName(resource.owner)}, {"resource_id", resource.resource_id}});
    }
    for (const auto destination : request.destinations) {
        if (model::resourceDestinationName(destination).empty()) throw std::invalid_argument("invalid destination");
        destinations.push_back(model::resourceDestinationName(destination));
    }
    nlohmann::json value = {{"resources", resources}, {"destinations", destinations}, {"action", model::resourceTransferActionName(request.action)}};
    const bool child = !request.parent_operation_id.empty();
    if (child != (request.parent_task_id != 0) || (!durable_producer && child != (request.parent_claim_generation != 0))) throw std::invalid_argument("incomplete producer identity");
    if (child) {
        if (request.parent_operation_id.size() > 64 || !code(request.parent_operation_id) || request.destinations.size() != 1 || request.action != model::ResourceTransferAction::Process || (!request.child_kind.empty() && request.child_kind != "artifact" && request.child_kind != "related")) throw std::invalid_argument("invalid producer request");
        value["parent_operation_id"] = request.parent_operation_id;
        value["parent_task_id"] = request.parent_task_id;
        if (!request.child_kind.empty()) value["child_kind"] = request.child_kind;
        // 认领代次仅作本次登记栅栏，同一真实父任务重试仍重放已有产物操作。
    }
    if (request.process_related_resources) value["process_related_resources"] = true;
    if (request.source_options) {
        const auto &source = *request.source_options;
        if (source.pacmtoken_refresh_interval_seconds < 0 || source.pacmtoken_refresh_interval_seconds > 3600) throw std::invalid_argument("invalid source refresh interval");
        value["source_options"] = {{"pacmtoken_refresh_interval_seconds", source.pacmtoken_refresh_interval_seconds}, {"auto_refresh_pacmtoken", source.auto_refresh_pacmtoken}, {"refresh_pacmtoken_on_440013", source.refresh_pacmtoken_on_440013}};
    }
    return value;
}

// 刷新只在无旧副作用占用时推进，不能绕过维护或先抹掉 skip 再报错。
boost::asio::awaitable<bool> refresh(boost::mysql::pooled_connection &connection, const std::optional<Snapshot> &resource)
{
    if (!resource) co_return false;
    if (resource->status == 1) throw std::logic_error("refresh requires idle resource");
    const auto owner = std::string(model::resourceOwnerName(resource->identity.owner));
    for (const auto destination : {model::ResourceDestination::Local, model::ResourceDestination::Baidu, model::ResourceDestination::Gdrive}) {
        const auto name = std::string(model::resourceDestinationName(destination));
        const auto slot = co_await query(connection, "resource_transfer_slots/lock", owner, resource->identity.resource_id, name);
        if (!slot.rows().empty()) throw std::logic_error("refresh requires idle targets");
        if (destination != model::ResourceDestination::Local) {
            const auto receipt = co_await query(connection, "resource_cloud_receipts/transfer_lock", owner, resource->identity.resource_id, name);
            if (!receipt.rows().empty() && MySqlValueReader::stringAt(receipt.rows().front(), 0) == "running") throw std::logic_error("refresh requires idle native claims");
        }
    }
    (void)co_await query(connection, table(resource->identity.owner) + "/transfer_refresh", resource->identity.resource_id);
    co_return true;
}
} // namespace

boost::asio::awaitable<model::ResourceTransferOperation> enqueueOnConnection(boost::mysql::pooled_connection &connection, model::ResourceTransferRequest request, std::vector<model::ResourceTransferTargetProfile> profiles, std::optional<model::ResourceProducerTask> producer_authorization)
{
    const auto raw = canonical(request, producer_authorization.has_value());
    const auto request_hash = digest(raw.dump()), proposed_id = operationId();
    // 唯一键竞争由 INSERT 序列化；不先锁不存在的键而产生间隙锁升级死锁。
    (void)co_await query(connection, "resource_transfer_operations/insert", proposed_id, request.idempotency_key, request_hash, raw.dump(), std::string(model::resourceTransferActionName(request.action)), static_cast<std::uint64_t>(request.resources.size()));
    if (!request.idempotency_key.empty()) {
        const auto existing = co_await query(connection, "resource_transfer_operations/idempotency", request.idempotency_key);
        if (existing.rows().size() != 1 || MySqlValueReader::stringAt(existing.rows().front(), 1) != request_hash) throw std::logic_error("idempotency mismatch");
        const auto existing_id = MySqlValueReader::stringAt(existing.rows().front(), 0);
        if (existing_id != proposed_id) {
            auto replay = co_await operation(connection, existing_id, false);
            if (!replay) throw std::logic_error("missing idempotent operation");
            replay->replayed = true;
            co_return *replay;
        }
    }
    std::map<model::ResourceDestination, model::ResourceTransferTargetProfile> selected;
    for (const auto &profile : profiles) {
        if (model::resourceDestinationName(profile.destination).empty() || !selected.emplace(profile.destination, profile).second) throw std::invalid_argument("ambiguous profiles");
    }
    if (request.destinations.empty())
        for (const auto &[destination, ignored] : selected) request.destinations.push_back(destination);
    if (request.destinations.empty()) throw DestinationDisabled();
    for (const auto destination : request.destinations)
        if (!selected.contains(destination)) throw DestinationDisabled();
    const auto producer = co_await childOrigin(connection, request, producer_authorization);
    std::vector<std::optional<Snapshot>> resources;
    for (const auto identity : request.resources) resources.push_back(co_await snapshot(connection, identity));
    PreparedQueries statements(connection);
    co_await statements.prepareTransferPlan(request.action == model::ResourceTransferAction::Process);
    const auto states = co_await targets(connection);
    if (request.require_write_admission) {
        bool active = false;
        for (const auto &state : states) {
            if (!state.revision || state.state == "maintenance" || (state.state == "active" && (!state.baseline_ready || !hash(state.fingerprint)))) throw WriteAdmissionClosed();
            active = active || state.state == "active";
        }
        if (!active) throw WriteAdmissionClosed();
    }
    if (producer) co_await validateChildren(connection, request, *producer, producer_authorization);
    if (request.action == model::ResourceTransferAction::RefreshContent) {
        bool active = false;
        for (const auto &state : states)
            if (state.revision == 0 || state.state == "maintenance" || (state.state == "active" && (!state.baseline_ready || !hash(state.fingerprint)))) throw std::logic_error("refresh dispatch blocked");
        for (const auto destination : request.destinations) {
            const auto &state = states.at(static_cast<std::size_t>(destination));
            const auto configured = selected.find(destination);
            active = active || (configured != selected.end() && configured->second.available && state.state == "active" && state.fingerprint == configured->second.fingerprint);
        }
        if (!active) throw std::logic_error("refresh has no active target");
        for (auto &resource : resources)
            if (co_await refresh(connection, resource)) resource = co_await snapshot(connection, resource->identity);
    }
    auto frozen = raw;
    if (producer) frozen["parent_claim_generation"] = request.parent_claim_generation;
    frozen["resolved_targets"] = nlohmann::json::array();
    frozen["adapters"] = nlohmann::json::object();
    frozen["strict_sha256"] = nlohmann::json::object();
    frozen["producers"] = nlohmann::json::object();
    for (const auto destination : request.destinations) {
        const auto &state = states.at(static_cast<std::size_t>(destination));
        frozen["resolved_targets"].push_back({{"destination", model::resourceDestinationName(destination)}, {"target_revision", state.revision}, {"target_fingerprint", state.fingerprint}});
        const auto profile = selected.find(destination);
        frozen["adapters"][std::string(model::resourceDestinationName(destination))] = profile == selected.end() ? "" : profile->second.adapter;
        frozen["strict_sha256"][std::string(model::resourceDestinationName(destination))] = profile != selected.end() && profile->second.policy.strict_sha256;
        if (profile != selected.end()) {
            const auto &p = profile->second;
            frozen["producers"][std::string(model::resourceDestinationName(destination))] = {{"enabled", p.produces_artifacts}, {"version", p.producer_version}, {"schema", p.producer_schema_version}, {"key_available", p.producer_key_available}, {"timeout_ms", p.producer_policy.timeout_ms}, {"retries", p.producer_policy.retries}, {"max_bytes", p.producer_policy.max_bytes}, {"bytes_per_second", p.producer_policy.bytes_per_second}};
        }
    }
    std::vector<PlannedItem> planned;
    for (std::size_t i = 0; i < request.resources.size(); ++i) {
        for (const auto destination : request.destinations) {
            model::ResourceTransferJob job;
            job.operation_id = proposed_id;
            job.resource = request.resources[i];
            job.destination = destination;
            job.action = request.action;
            job.process_related_resources = request.process_related_resources;
            job.source_options = request.source_options.value_or(model::ResourceTransferSourceOptions {});
            const auto profile = selected.find(destination);
            if (request.action == model::ResourceTransferAction::Process)
                planned.push_back(co_await planItem(statements, std::move(job), resources[i], states, profile == selected.end() ? nullptr : &profile->second));
            else
                (void)co_await registerItem(statements, std::move(job), resources[i], states, profile == selected.end() ? nullptr : &profile->second);
        }
    }
    if (request.action == model::ResourceTransferAction::Process) co_await persistProcess(statements, std::move(planned));
    (void)co_await query(connection, "resource_transfer_operations/freeze", frozen.dump(), static_cast<std::uint64_t>(request.resources.size() * request.destinations.size()), proposed_id);
    co_await producer_jobs::registerPlans(connection, proposed_id, profiles);
    co_await enforceOperationCapacity(connection, proposed_id);
    (void)co_await aggregate(connection, proposed_id);
    auto accepted = co_await operation(connection, proposed_id, false);
    if (!accepted) throw std::logic_error("accepted operation disappeared");
    co_return *accepted;
}

boost::asio::awaitable<model::ResourceTransferOperation> enqueue(std::shared_ptr<MySqlConnectionPool> pool, model::ResourceTransferRequest request, std::vector<model::ResourceTransferTargetProfile> profiles)
{
    auto work = [request = std::move(request), profiles = std::move(profiles)](boost::mysql::pooled_connection &connection) mutable -> boost::asio::awaitable<model::ResourceTransferOperation> {
        co_return co_await enqueueOnConnection(connection, std::move(request), std::move(profiles));
    };
    co_return co_await transaction<model::ResourceTransferOperation>(std::move(pool), std::move(work));
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::transfer_jobs
