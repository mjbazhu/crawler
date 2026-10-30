#include "crawler/infrastructure/music/persistence/mysql/MySqlProducedArtifactRegistry.h"

#include "MySqlResourceTransferJobsSupport.h"
#include "MySqlProducedArtifactRegistrySupport.h"

#include <algorithm>
#include <chrono>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {
namespace {
namespace jobs = transfer_jobs;
namespace model = application::model;
using Identities = std::vector<model::ResourceIdentity>;

// 生产器只能提供两种完整产物的摘要，不能夹带本地成功路径或其他资源身份。
void validate(const model::ResourceTransferJob &parent, const model::LyricArtifactBundle &bundle, std::uint64_t source_size)
{
    if (parent.resource.owner != model::ResourceOwner::Song || !parent.task_id || bundle.source_resource_id != parent.resource.resource_id || !jobs::hash(bundle.source_checksum) || !source_size || source_size > std::uint64_t(1024) * 1024 * 1024 || bundle.processor_version.empty() || bundle.processor_version.size() > 96 || bundle.schema_version < 1 || bundle.files.size() != 2) throw std::invalid_argument("invalid produced artifact scope");
    std::set<std::string> kinds;
    for (const auto &file : bundle.files) {
        if ((file.kind != "word_json" && file.kind != "enhanced_lrc") || !kinds.insert(file.kind).second || !file.local_path.empty() || !jobs::hash(file.checksum) || !file.size_bytes || file.size_bytes > std::uint64_t(4) * 1024 * 1024 * 1024) throw std::invalid_argument("invalid produced artifact evidence");
    }
}

// 同一来源/处理器的不同结果是冲突；升级身份不能抢走在途或未决任务。
void compareExisting(const boost::mysql::results &rows, const model::LyricArtifactBundle &bundle)
{
    for (const auto row : rows.rows()) {
        const auto kind = MySqlValueReader::stringAt(row, 1);
        const auto found = std::find_if(bundle.files.begin(), bundle.files.end(), [&](const auto &file) { return file.kind == kind; });
        if (found == bundle.files.end()) throw std::logic_error("unexpected artifact identity");
        const bool same = MySqlValueReader::stringAt(row, 2) == bundle.source_checksum && MySqlValueReader::stringAt(row, 3) == bundle.processor_version && MySqlValueReader::intAt(row, 4) == bundle.schema_version;
        const auto reference = MySqlValueReader::stringAt(row, 5);
        if (same && !reference.empty() && (reference != found->checksum || MySqlValueReader::uint64At(row, 6) != found->size_bytes)) throw std::logic_error("produced artifact content conflict");
        if (!same && (MySqlValueReader::intAt(row, 7) == 1 || MySqlValueReader::uint64At(row, 8) || MySqlValueReader::uint64At(row, 9))) throw std::logic_error("produced artifact identity is occupied");
    }
}

// 锁顺序为父操作、MRC 与产物资源、目标/任务；两端同时生成相同内容可幂等复用。
boost::asio::awaitable<Identities> persist(std::shared_ptr<MySqlConnectionPool> pool, model::ResourceTransferJob parent, model::LyricArtifactBundle bundle, std::uint64_t source_size, std::chrono::steady_clock::time_point deadline)
{
    validate(parent, bundle, source_size);
    auto work = [parent, bundle, source_size, deadline](boost::mysql::pooled_connection &connection) -> boost::asio::awaitable<Identities> {
        if (std::chrono::steady_clock::now() >= deadline) throw std::logic_error("producer deadline expired");
        (void)co_await jobs::operation(connection, parent.operation_id, true);
        (void)co_await jobs::snapshot(connection, parent.resource);
        (void)co_await jobs::query(connection, "song_resource_artifacts/producer_list", parent.resource.resource_id);
        if (!(co_await jobs::current(connection, parent))) throw std::logic_error("producer parent changed");
        const auto identities = co_await produced_artifacts::recordOnConnection(connection, parent, bundle, source_size);
        if (std::chrono::steady_clock::now() >= deadline) throw std::logic_error("producer deadline expired");
        co_return identities;
    };
    co_return co_await jobs::transaction<Identities>(std::move(pool), std::move(work));
}
} // namespace

boost::asio::awaitable<std::vector<application::model::ResourceIdentity>> produced_artifacts::recordOnConnection(boost::mysql::pooled_connection &connection, const application::model::ResourceTransferJob &parent, const application::model::LyricArtifactBundle &bundle, std::uint64_t source_size)
{
    validate(parent, bundle, source_size);
    const auto source = co_await jobs::snapshot(connection, parent.resource);
    const auto previous = co_await jobs::query(connection, "song_resource_artifacts/producer_list", parent.resource.resource_id);
    if (!source || source->generation != parent.version.content_generation || source->source_revision != parent.source_revision || source->source_identity != parent.source_identity) throw std::logic_error("producer parent changed");
    const auto kind = co_await jobs::query(connection, "song_resources/producer_kind", parent.resource.resource_id);
    if (kind.rows().size() != 1 || MySqlValueReader::stringAt(kind.rows().front(), 0) != "mrc") throw std::invalid_argument("producer parent is not MRC");
    if (!source->reference_sha256.empty() && (source->reference_sha256 != bundle.source_checksum || source->reference_size != source_size)) throw std::logic_error("producer source content conflict");
    compareExisting(previous, bundle);
    (void)co_await jobs::query(connection, "song_resources/transfer_reference", bundle.source_checksum, source_size, parent.resource.resource_id, parent.version.content_generation, parent.source_revision, bundle.source_checksum, source_size);
    for (const auto &file : bundle.files)
        (void)co_await jobs::query(connection, "song_resource_artifacts/producer_upsert", bundle.source_resource_id, file.kind, bundle.source_checksum, bundle.processor_version, bundle.schema_version);
    const auto registered = co_await jobs::query(connection, "song_resource_artifacts/producer_list", parent.resource.resource_id);
    if (registered.rows().size() != 2) throw std::logic_error("producer artifact set incomplete");
    Identities identities;
    for (const auto row : registered.rows()) {
        const auto id = MySqlValueReader::uint64At(row, 0);
        const auto kind_name = MySqlValueReader::stringAt(row, 1);
        const auto found = std::find_if(bundle.files.begin(), bundle.files.end(), [&](const auto &file) { return file.kind == kind_name; });
        if (found == bundle.files.end()) throw std::logic_error("producer artifact kind changed");
        const auto artifact = co_await jobs::snapshot(connection, {model::ResourceOwner::SongArtifact, id});
        if (!artifact || (!artifact->reference_sha256.empty() && (artifact->reference_sha256 != found->checksum || artifact->reference_size != found->size_bytes))) throw std::logic_error("producer artifact reference changed");
        (void)co_await jobs::query(connection, "song_resource_artifacts/transfer_reference", found->checksum, found->size_bytes, id, artifact->generation, artifact->source_revision, found->checksum, found->size_bytes);
        identities.push_back({model::ResourceOwner::SongArtifact, id});
    }
    co_return identities;
}

MySqlProducedArtifactRegistry::MySqlProducedArtifactRegistry(std::shared_ptr<MySqlConnectionPool> pool) : pool_(std::move(pool))
{
    if (!pool_) throw std::invalid_argument("produced artifact registry requires a pool");
}

application::result::ServiceResult<std::vector<application::model::ResourceIdentity>> MySqlProducedArtifactRegistry::record(const application::model::ResourceTransferJob &parent, const application::model::LyricArtifactBundle &bundle, std::uint64_t source_size, const application::model::ResourceTransferContext &context)
{
    if (context.stopped()) return application::result::ServiceResult<Identities>::failure(application::result::ServiceErrorType::Conflict, "producer_stopped");
    return jobs::run(pool_, persist(pool_, parent, bundle, source_size, context.deadline), true, std::chrono::milliseconds(std::max(1, context.remaining())));
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
