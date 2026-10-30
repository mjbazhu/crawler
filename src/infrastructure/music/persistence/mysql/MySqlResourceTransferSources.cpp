#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceTransferSources.h"
#include "MySqlResourceTransferJobsSupport.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlSongResourceQueue.h"
#include <stdexcept>
#include <algorithm>
#include <chrono>
#include <utility>
namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {
MySqlResourceTransferSources::MySqlResourceTransferSources(std::shared_ptr<MySqlConnectionPool> pool) : pool_(std::move(pool))
{
    if (!pool_) throw std::invalid_argument("resource source pool required");
}
application::result::ServiceResult<application::model::ResourceTransferSourceInput> MySqlResourceTransferSources::describe(const application::model::ResourceTransferJob &job)
{
    auto work = [job](boost::mysql::pooled_connection &connection) -> boost::asio::awaitable<application::model::ResourceTransferSourceInput> {
        const auto current = co_await transfer_jobs::snapshot(connection, job.resource);
        if (!current || current->generation != job.version.content_generation || current->source_revision != job.source_revision || current->source_identity != job.source_identity) throw std::logic_error("source version changed");
        const auto rows = co_await transfer_jobs::query(connection, transfer_jobs::table(job.resource.owner) + "/transfer_source", job.resource.resource_id);
        if (rows.rows().size() != 1) throw std::logic_error("source description missing");
        const auto row = rows.rows().front();
        if (job.resource.owner == application::model::ResourceOwner::Song) co_return MySqlSongResourceQueue::mapTarget(row);
        if (job.resource.owner == application::model::ResourceOwner::SongArtifact) {
            application::model::ResourceArtifactInput input;
            input.local_path = MySqlValueReader::stringAt(row, 0);
            input.checksum = MySqlValueReader::stringAt(row, 1);
            input.size_bytes = MySqlValueReader::uint64At(row, 2);
            input.artifact_type = MySqlValueReader::stringAt(row, 3);
            input.source_checksum = MySqlValueReader::stringAt(row, 5);
            input.processor_version = MySqlValueReader::stringAt(row, 6);
            input.schema_version = static_cast<int>(MySqlValueReader::uint64At(row, 7));
            const auto original = co_await transfer_jobs::query(connection, "song_resources/transfer_source", MySqlValueReader::uint64At(row, 4));
            if (original.rows().size() == 1) input.source = MySqlSongResourceQueue::mapTarget(original.rows().front());
            co_return input;
        }
        application::model::MetadataCoverRecord input;
        input.owner = job.resource.owner == application::model::ResourceOwner::Singer ? application::model::CoverOwner::Singer : application::model::CoverOwner::Album;
        input.resource_id = job.resource.resource_id;
        input.source_ref_id = MySqlValueReader::uint64At(row, 0);
        input.source = MySqlValueReader::stringAt(row, 1);
        input.source_id = MySqlValueReader::stringAt(row, 2);
        input.remote_url = MySqlValueReader::stringAt(row, 3);
        input.claim_generation = job.native_claim_generation;
        co_return input;
    };
    return transfer_jobs::run(pool_, transfer_jobs::transaction<application::model::ResourceTransferSourceInput>(pool_, std::move(work)), false, std::chrono::milliseconds((std::clamp)(job.policy.timeout_ms, 1, 1000)));
}
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
