#include "crawler/infrastructure/music/persistence/mysql/MySqlLegacyResourceTransfer.h"
#include "MySqlLegacyResourceTransferSupport.h"

#include <algorithm>
#include <openssl/rand.h>
#include <tuple>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {
namespace jobs = transfer_jobs;
namespace legacy = legacy_transfer;
namespace model = application::model;
namespace result = application::result;
namespace {
// 未指定旧编号时生成批次ID；内部幂等键也独立使用此随机身份。
std::string batchId()
{
    std::array<unsigned char, 16> bytes {};
    if (RAND_bytes(bytes.data(), static_cast<int>(bytes.size())) != 1) throw std::runtime_error("legacy batch identity failed");
    constexpr char hex[] = "0123456789abcdef";
    std::string id = "legacy_rt_";
    for (auto b : bytes) {
        id += hex[b >> 4];
        id += hex[b & 15];
    }
    return id;
}
boost::asio::awaitable<model::LegacyResourceTransferBatch> startBatch(std::shared_ptr<MySqlConnectionPool> pool, model::LegacyResourceTransferRequest request)
{
    auto work = [request = std::move(request)](auto &connection) -> boost::asio::awaitable<model::LegacyResourceTransferBatch> {
        const auto provider = request.cloud_provider ? std::string(model::resourceDestinationName(*request.cloud_provider)) : "local";
        (void)co_await jobs::query(connection, "resource_legacy_profiles/ensure", provider);
        const auto fixed = co_await jobs::query(connection, "resource_legacy_profiles/lock");
        if (fixed.rows().size() != 1 || MySqlValueReader::stringAt(fixed.rows().front(), 0) != provider) throw std::logic_error("legacy provider is immutable");
        const auto dispatch_id = batchId();
        const auto id = request.owner == model::ResourceOwner::Song && !request.songs.batch_id.empty() ? request.songs.batch_id : dispatch_id;
        if (id.size() > 128 || std::any_of(id.begin(), id.end(), [](unsigned char ch) { return ch < 32 || ch == 127; })) throw std::invalid_argument("invalid legacy batch id");
        // 固定profile锁使兼容入口串行受理；严格插入另防其他批次writer竞争。
        if (!(co_await jobs::query(connection, "crawl_batches/legacy_identity", id)).rows().empty() || !(co_await jobs::query(connection, "resource_transfer_operations/find", id)).rows().empty()) throw std::logic_error("legacy batch id already exists");
        auto items = co_await legacy::select(connection, request);
        std::vector<model::ResourceIdentity> identities;
        for (const auto &item : items) identities.push_back(item.identity);
        std::sort(identities.begin(), identities.end(), [](const auto &a, const auto &b) { return std::tie(a.owner, a.resource_id) < std::tie(b.owner, b.resource_id); });
        for (const auto identity : identities) (void)co_await jobs::snapshot(connection, identity);
        const auto states = co_await jobs::targets(connection);
        if (std::any_of(states.begin(), states.end(), [](const auto &state) { return state.revision == 0 || state.state == "maintenance" || (state.state == "active" && (!state.baseline_ready || !jobs::hash(state.fingerprint))); })) throw jobs::WriteAdmissionClosed();
        // v2可能在初次选择与资源锁之间先受理；按当前锁读再去掉原范围已有占用。
        std::vector<model::ResourceIdentity> available;
        for (const auto identity : identities) {
            bool occupied = false;
            for (const auto destination : {model::ResourceDestination::Local, model::ResourceDestination::Baidu, model::ResourceDestination::Gdrive}) {
                if (destination != model::ResourceDestination::Local && (request.owner == model::ResourceOwner::Song || request.cloud_provider != destination)) continue;
                const auto slots = co_await jobs::query(connection, "resource_transfer_slots/lock", std::string(model::resourceOwnerName(identity.owner)), identity.resource_id, std::string(model::resourceDestinationName(destination)));
                occupied = occupied || !slots.rows().empty();
            }
            if (!occupied) available.push_back(identity);
        }
        identities = std::move(available);
        std::erase_if(items, [&](const auto &item) { return std::find(identities.begin(), identities.end(), item.identity) == identities.end(); });
        (void)co_await legacy::createLegacyRows(connection, request, id, items);
        for (std::size_t offset = 0; offset < identities.size(); offset += 200) {
            model::ResourceTransferRequest enqueue;
            enqueue.resources.assign(identities.begin() + offset, identities.begin() + (std::min)(offset + 200, identities.size()));
            enqueue.idempotency_key = dispatch_id + ":" + std::to_string(offset / 200);
            enqueue.require_write_admission = true;
            // 旧process是调用方明确再处理，延续原取消后可重新提交的合同；未决槽仍不可接管。
            enqueue.action = model::ResourceTransferAction::Retry;
            enqueue.process_related_resources = request.owner == model::ResourceOwner::Song && request.songs.options.process_related_resources;
            if (request.owner == model::ResourceOwner::Song) {
                const auto &options = request.songs.options;
                enqueue.source_options = model::ResourceTransferSourceOptions {options.pacmtoken_refresh_interval_seconds, options.auto_refresh_pacmtoken, options.refresh_pacmtoken_on_440013};
            }
            if (request.owner != model::ResourceOwner::Song && request.covers.refresh) enqueue.action = model::ResourceTransferAction::RefreshContent;
            const auto operation = co_await jobs::enqueueOnConnection(connection, std::move(enqueue), request.profiles);
            (void)co_await jobs::query(connection, "resource_legacy_operations/insert", id, operation.operation_id, static_cast<std::uint64_t>(offset / 200));
        }
        co_return *(co_await legacy::project(connection, id));
    };
    co_return co_await jobs::transaction<model::LegacyResourceTransferBatch>(pool, std::move(work), true);
}
} // namespace

MySqlLegacyResourceTransfer::MySqlLegacyResourceTransfer(std::shared_ptr<MySqlConnectionPool> pool) : pool_(std::move(pool))
{
    if (!pool_) throw std::invalid_argument("legacy resource pool required");
}
result::ServiceResult<model::LegacyResourceTransferBatch> MySqlLegacyResourceTransfer::start(model::LegacyResourceTransferRequest request)
{
    return jobs::run(pool_, startBatch(pool_, std::move(request)), true);
}
result::ServiceResult<std::optional<model::LegacyResourceTransferBatch>> MySqlLegacyResourceTransfer::find(std::string id)
{
    auto work = [id = std::move(id)](auto &connection) -> boost::asio::awaitable<std::optional<model::LegacyResourceTransferBatch>> { co_return co_await legacy::project(connection, id); };
    return jobs::run(pool_, jobs::transaction<std::optional<model::LegacyResourceTransferBatch>>(pool_, std::move(work), true), true);
}
result::ServiceResult<bool> MySqlLegacyResourceTransfer::cancel(std::string id)
{
    auto work = [id = std::move(id)](auto &connection) -> boost::asio::awaitable<bool> {
        const auto batch = co_await legacy::project(connection, id);
        if (!batch) throw jobs::MissingOperation();
        if (batch->terminal) throw std::logic_error("legacy batch completed");
        for (const auto &operation : batch->operation_ids) (void)co_await jobs::cancelOnConnection(connection, operation, {});
        (void)co_await legacy::project(connection, id, true);
        co_return true;
    };
    return jobs::run(pool_, jobs::transaction<bool>(pool_, std::move(work), true), true);
}
result::ServiceResult<bool> MySqlLegacyResourceTransfer::pollNext()
{
    auto work = [this](auto &connection) -> boost::asio::awaitable<bool> {
        auto rows = co_await jobs::query(connection, "resource_legacy_batches/next", cursor_.load());
        if (rows.rows().empty()) {
            cursor_ = 0;
            co_return false;
        }
        const auto row = rows.rows().front();
        cursor_ = MySqlValueReader::uint64At(row, 0);
        (void)co_await legacy::project(connection, MySqlValueReader::stringAt(row, 1));
        co_return true;
    };
    return jobs::run(pool_, jobs::transaction<bool>(pool_, std::move(work), true), true);
}
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
