#include "MySqlLegacyResourceTransferSupport.h"

#include <algorithm>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::legacy_transfer {
namespace jobs = transfer_jobs;
boost::asio::awaitable<bool> createLegacyRows(boost::mysql::pooled_connection &connection, const model::LegacyResourceTransferRequest &request, const std::string &id, const std::vector<Item> &items)
{
    const bool song = request.owner == model::ResourceOwner::Song;
    const auto provider = request.cloud_provider ? std::string(model::resourceDestinationName(*request.cloud_provider)) : "local";
    const int concurrency = song ? request.songs.options.resource_download_concurrency : request.covers.concurrency;
    const int timeout = song ? request.songs.options.resource_download_timeout_ms : request.covers.timeout_ms;
    const int retries = song ? request.songs.options.resource_download_retries : request.covers.retries;
    if (concurrency < 1 || concurrency > 32 || timeout < 100 || timeout > 600000 || retries < 0 || retries > 5) throw std::invalid_argument("invalid legacy options");
    const auto local = std::find_if(request.profiles.begin(), request.profiles.end(), [](const auto &p) { return p.destination == model::ResourceDestination::Local; });
    const auto cloud = std::find_if(request.profiles.begin(), request.profiles.end(), [&](const auto &p) { return request.cloud_provider == p.destination; });
    const bool wait_artifacts = local != request.profiles.end() && local->produces_artifacts && local->producer_key_available;
    const auto created = jobs::now();
    if (request.batch_timeout_ms < 1000 || request.batch_timeout_ms > 86400000) throw std::invalid_argument("invalid legacy batch timeout");
    Json snapshot = {{"items", itemsJson(items)}, {"initial_count", items.size()}, {"wait_for_artifacts", wait_artifacts}, {"strict_cloud", cloud != request.profiles.end() && cloud->policy.strict_sha256}, {"deadline", created + static_cast<std::uint64_t>(request.batch_timeout_ms + 999) / 1000}};
    const auto task_type = song ? "resourceProcess" : "metadataCovers";
    const auto count = static_cast<std::uint64_t>(items.size());
    (void)co_await jobs::query(connection, "crawl_batches/insert_legacy_batch", id, std::string("migu"), std::string(task_type), std::string("Running"), std::string("{}"), std::string("{}"), count, count, 0, 0, 0, 0, std::string {}, created, created, std::uint64_t {0}, created, std::uint64_t {0}, std::string {}, std::string {});
    (void)co_await jobs::query(connection, "resource_legacy_batches/insert", id, std::string(model::resourceOwnerName(request.owner)), provider, concurrency, snapshot.dump());
    (void)co_await jobs::query(connection, "crawl_batch_events/insert_event", id, 0, std::string("batchStarted"), std::string("legacy resource operation accepted"), Json {{"legacy_projection", true}}.dump(), created);
    co_return true;
}

boost::asio::awaitable<void> storeItem(boost::mysql::pooled_connection &connection, const std::string &id, std::size_t index, const Item &item, const std::string &state, bool written)
{
    const bool song = item.identity.owner == model::ResourceOwner::Song;
    auto payload = song ? songJson(item.song) : coverJson(item.cover);
    payload["batch_id"] = id;
    payload["item_index"] = index;
    payload["item_key"] = (song ? "resource:" : std::string(model::resourceOwnerName(item.identity.owner)) + ":") + std::to_string(item.identity.resource_id);
    payload["trace"] = "batch:" + id + "/item:" + payload["item_key"].get<std::string>() + "/index:" + std::to_string(index);
    if (song) payload["content_id"] = item.song.source_content_id;
    const auto label = song ? item.song.title : item.cover.record.source_id;
    const auto code = song ? item.song.result_code : item.cover.result_code;
    const auto category = song ? item.song.error_category : item.cover.error_category;
    const auto now = jobs::now();
    (void)co_await jobs::query(connection, "crawl_batch_items/insert_item", id, static_cast<std::uint64_t>(index), payload["item_key"].get<std::string>(), song ? std::string("songResource") : item.identity.owner == model::ResourceOwner::Singer ? std::string("singerCover") :
                                                                                                                                                                                                                                                      std::string("albumCover"),
                               label,
                               std::string("migu"),
                               song ? std::string {} : item.identity.owner == model::ResourceOwner::Singer ? item.cover.record.source_id :
                                                                                                             std::string {},
                               song ? std::string {} : item.identity.owner == model::ResourceOwner::Album ? item.cover.record.source_id :
                                                                                                            std::string {},
                               song ? item.song.source_song_id : std::string {},
                               state,
                               0,
                               written ? 1 : 0,
                               category,
                               code,
                               now,
                               state == "Running" ? std::uint64_t {0} : now,
                               std::string {},
                               payload.dump(),
                               now,
                               now);
}
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::legacy_transfer
