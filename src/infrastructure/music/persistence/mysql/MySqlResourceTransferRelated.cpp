#include "MySqlResourceTransferRelated.h"

#include <array>
#include <stdexcept>
#include <string>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::transfer_jobs {
namespace {
// 关联行为只属于旧合同中明确启用的音频；MRC自身的producer单独管理。
boost::asio::awaitable<std::uint64_t> source(boost::mysql::pooled_connection &connection, const model::ResourceTransferJob &job)
{
    if (!job.process_related_resources || job.resource.owner != model::ResourceOwner::Song) co_return 0;
    const auto rows = co_await query(connection, "song_resources/related_parent", job.resource.resource_id);
    if (rows.rows().size() != 1 || MySqlValueReader::stringAt(rows.rows().front(), 1) != "audio") co_return 0;
    co_return MySqlValueReader::uint64At(rows.rows().front(), 0);
}
} // namespace

boost::asio::awaitable<void> lockRelatedScope(boost::mysql::pooled_connection &connection, const model::ResourceTransferJob &job, bool force_song_scope)
{
    if ((!job.process_related_resources && !force_song_scope) || job.resource.owner != model::ResourceOwner::Song) co_return;
    co_await lockOperationTree(connection, job.operation_id);
    (void)co_await query(connection, "song_resources/related_lock_scope", job.resource.resource_id);
}

boost::asio::awaitable<void> recordRelated(boost::mysql::pooled_connection &connection, const model::ResourceTransferJob &job, std::uint8_t mask)
{
    if (mask > 15) throw std::invalid_argument("invalid related discovery");
    if (!(co_await source(connection, job))) co_return;
    (void)co_await query(connection, "resource_transfer_related/record", job.task_id, static_cast<unsigned int>(mask), job.claim_generation);
}

boost::asio::awaitable<void> enqueueRelated(boost::mysql::pooled_connection &connection, const model::ResourceTransferJob &job)
{
    const auto source_id = co_await source(connection, job);
    if (!source_id) co_return;
    const auto discovered = co_await query(connection, "resource_transfer_related/find", job.task_id);
    if (discovered.rows().size() != 1) throw std::logic_error("related discovery missing");
    const auto row = discovered.rows().front();
    if (!row[1].is_null()) co_return;
    const auto mask = MySqlValueReader::uint64At(row, 0);
    if (mask > 15) throw std::logic_error("invalid persisted related discovery");
    model::ResourceTransferRequest request;
    const std::array<std::string, 4> kinds {"cover", "lyric", "mrc", "trc"};
    for (std::size_t i = 0; i < kinds.size(); ++i) {
        if (!(mask & (std::uint64_t(1) << i))) continue;
        (void)co_await query(connection, "song_resources/related_register", source_id, kinds[i]);
        const auto found = co_await query(connection, "song_resources/related_identity", source_id, kinds[i]);
        if (found.rows().size() != 1) throw std::logic_error("related resource missing");
        request.resources.push_back({model::ResourceOwner::Song, MySqlValueReader::uint64At(found.rows().front(), 0)});
    }
    if (request.resources.empty()) co_return;
    request.destinations = {job.destination};
    request.parent_operation_id = job.operation_id;
    request.parent_task_id = job.task_id;
    request.parent_claim_generation = job.claim_generation;
    request.child_kind = "related";
    request.source_options = job.source_options;
    request.idempotency_key = "related:" + job.operation_id + ':' + std::to_string(job.resource.resource_id) + ':' + std::string(model::resourceDestinationName(job.destination));
    const auto profile = co_await frozenProfile(connection, job);
    const auto child = co_await enqueueOnConnection(connection, std::move(request), {profile});
    (void)co_await query(connection, "resource_transfer_related/child", child.operation_id, job.task_id);
}

boost::asio::awaitable<void> inheritRelated(boost::mysql::pooled_connection &connection, std::uint64_t old_task_id, std::uint64_t new_task_id)
{
    (void)co_await query(connection, "resource_transfer_related/inherit", new_task_id, old_task_id);
}
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::transfer_jobs
