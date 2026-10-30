#include "MySqlLegacyResourceTransferSupport.h"

#include <algorithm>
#include <map>
#include <set>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::legacy_transfer {
namespace jobs = transfer_jobs;
namespace {
struct Task {
    std::string owner, destination, state, code, category, path, checksum;
    std::uint64_t id = 0, size = 0;
    int depth = 0;
};
bool active(const Task &t)
{
    return t.state == "pending" || t.state == "running" || t.state == "retry";
}
bool skipped(const Task &t)
{
    return t.state == "skipped" || t.state == "busy" || t.code == "already_current";
}
std::vector<Task> readTasks(const boost::mysql::results &rows)
{
    std::vector<Task> result;
    for (auto row : rows.rows()) {
        Task t;
        t.owner = MySqlValueReader::stringAt(row, 0);
        t.id = MySqlValueReader::uint64At(row, 1);
        t.destination = MySqlValueReader::stringAt(row, 2);
        t.state = MySqlValueReader::stringAt(row, 3);
        t.code = MySqlValueReader::stringAt(row, 4);
        t.category = MySqlValueReader::stringAt(row, 5);
        t.path = MySqlValueReader::stringAt(row, 6);
        t.checksum = MySqlValueReader::stringAt(row, 7);
        t.size = MySqlValueReader::uint64At(row, 8);
        t.depth = MySqlValueReader::intAt(row, 9);
        result.push_back(std::move(t));
    }
    return result;
}
// 必需范围内同一逻辑文件只计一次；其它平台不会延长旧完成时间。
void applySong(Item &item, const Task &t)
{
    auto &v = item.song;
    v.ok = t.state == "succeeded" && t.code != "already_current";
    v.skipped = skipped(t);
    v.result_code = t.code;
    v.error_category = t.category;
    v.message = t.code;
    if (t.state == "succeeded" && !t.path.empty()) {
        v.local_path = t.path;
        v.checksum = t.checksum;
        v.size_bytes = t.size;
    }
}
void countSong(model::SongResourceProcessResult &r, const model::SongResourceProcessItem &v)
{
    if (v.related) {
        if (v.ok)
            ++r.related_succeeded_count;
        else if (v.skipped)
            ++r.related_skipped_count;
        else
            ++r.related_failed_count;
    } else {
        if (v.ok)
            ++r.succeeded_count;
        else if (v.skipped)
            ++r.skipped_count;
        else
            ++r.failed_count;
    }
}
} // namespace

boost::asio::awaitable<std::optional<model::LegacyResourceTransferBatch>> project(boost::mysql::pooled_connection &connection, const std::string &id, bool cancel)
{
    const auto locked = co_await jobs::query(connection, "resource_legacy_batches/lock", id);
    if (locked.rows().empty()) co_return std::nullopt;
    const auto row = locked.rows().front();
    const auto terminal_json = MySqlValueReader::stringAt(row, 4);
    if (!terminal_json.empty()) co_return batchFromJson(Json::parse(terminal_json));
    model::LegacyResourceTransferBatch batch;
    batch.batch_id = id;
    batch.owner = *model::parseResourceOwner(MySqlValueReader::stringAt(row, 1));
    const auto provider = MySqlValueReader::stringAt(row, 2);
    auto snapshot = Json::parse(MySqlValueReader::stringAt(row, 3));
    auto items = itemsFromJson(snapshot.at("items"), batch.owner);
    const auto initial_count = snapshot.at("initial_count").get<std::size_t>();
    const auto mappings = co_await jobs::query(connection, "resource_legacy_operations/list", id);
    for (auto mapping : mappings.rows()) batch.operation_ids.push_back(MySqlValueReader::stringAt(mapping, 0));
    const auto tasks = readTasks(co_await jobs::query(connection, "resource_legacy_batches/tasks", id));
    const bool expired = jobs::now() >= snapshot.at("deadline").get<std::uint64_t>();
    batch.timed_out = !cancel && expired && std::any_of(tasks.begin(), tasks.end(), [&](const auto &t) { return t.owner == model::resourceOwnerName(batch.owner) && active(t) && (t.destination == "local" || (batch.owner != model::ResourceOwner::Song && t.destination == provider)); });
    bool post_expired = false;
    if (expired && !cancel && snapshot.value("wait_for_artifacts", false)) {
        const auto plans = co_await jobs::query(connection, "resource_legacy_batches/producers", id);
        post_expired = std::any_of(plans.rows().begin(), plans.rows().end(), [](const auto &producer) {
            const auto state = MySqlValueReader::stringAt(producer, 0);
            return state == "pending" || state == "running" || state == "retry" || state == "waiting_input";
        });
        post_expired = post_expired || std::any_of(tasks.begin(), tasks.end(), [](const auto &t) { return t.owner == "song_artifact" && t.destination == "local" && active(t); });
    }
    // 单独后处理超时也停止原必要范围，但不将已经确认的MRC改报下载失败。
    if (batch.timed_out || post_expired) {
        std::vector<model::ResourceDestination> scope {model::ResourceDestination::Local};
        if (batch.owner != model::ResourceOwner::Song && provider != "local") scope.push_back(*model::parseResourceDestination(provider));
        for (const auto &operation : batch.operation_ids) (void)co_await jobs::cancelOnConnection(connection, operation, scope);
    }
    std::set<std::uint64_t> known;
    for (const auto &item : items) known.insert(item.identity.resource_id);
    model::LegacyResourceTransferRequest facts;
    facts.owner = batch.owner;
    facts.strict_cloud = snapshot.value("strict_cloud", false);
    if (provider != "local") facts.cloud_provider = *model::parseResourceDestination(provider);
    // 后代仅由受控listen/producer登记；歌曲展示关联文件，产物不增加旧歌曲计数。
    std::vector<std::uint64_t> ids;
    for (const auto &t : tasks)
        if (t.owner == model::resourceOwnerName(batch.owner) && t.destination == "local" && known.insert(t.id).second) ids.push_back(t.id);
    for (const auto &item : items) ids.push_back(item.identity.resource_id);
    std::map<std::uint64_t, Item> current;
    for (std::size_t offset = 0; offset < ids.size(); offset += 200) {
        std::vector<std::uint64_t> page(ids.begin() + offset, ids.begin() + (std::min)(ids.size(), offset + 200));
        if (batch.owner == model::ResourceOwner::Song) {
            facts.songs.resource_ids = page;
            facts.songs.limit = 200;
        } else {
            facts.covers.query.resource_ids = page;
            facts.covers.limit = 200;
        }
        for (auto &item : co_await select(connection, facts, true)) current.emplace(item.identity.resource_id, std::move(item));
    }
    for (auto &item : items)
        if (auto found = current.find(item.identity.resource_id); found != current.end()) {
            const bool related = item.song.related;
            item = found->second;
            item.song.related = related;
        }
    for (auto &[key, item] : current)
        if (std::none_of(items.begin(), items.end(), [&](const auto &i) { return i.identity.resource_id == key; })) {
            item.song.related = true;
            items.push_back(std::move(item));
        }
    auto &p = batch.progress;
    p.batch_id = id;
    p.state_str = "Running";
    p.total = static_cast<int>(items.size());
    p.duration_seconds = static_cast<long long>(jobs::now() - MySqlValueReader::uint64At(row, 6));
    batch.songs.batch_id = id;
    batch.songs.claimed_count = static_cast<int>(initial_count);
    bool done = true;
    for (std::size_t index = 0; index < items.size(); ++index) {
        auto &item = items[index];
        std::vector<const Task *> required;
        for (const auto &t : tasks)
            if (t.owner == model::resourceOwnerName(batch.owner) && t.id == item.identity.resource_id && (t.destination == "local" || (batch.owner != model::ResourceOwner::Song && t.destination == provider))) required.push_back(&t);
        const bool waiting = std::any_of(required.begin(), required.end(), [](const auto *t) { return active(*t); });
        const bool complete = cancel || batch.timed_out || !waiting;
        done = done && complete;
        bool success = false, is_skipped = false;
        if (batch.owner == model::ResourceOwner::Song) {
            const auto local = std::find_if(required.begin(), required.end(), [](const auto *t) { return t->destination == "local"; });
            if (local != required.end())
                applySong(item, **local);
            else
                item.song.result_code = "resource_not_found";
            if ((cancel || batch.timed_out) && waiting) {
                item.song.ok = false;
                item.song.skipped = false;
                item.song.result_code = batch.timed_out ? "timeout" : "cancelled";
            }
            success = item.song.ok;
            is_skipped = item.song.skipped;
            if (complete) {
                countSong(batch.songs, item.song);
                batch.songs.items.push_back(item.song);
            }
        } else {
            auto &v = item.cover;
            v.success = !required.empty();
            v.skipped = !required.empty();
            for (const auto *t : required) {
                v.success = v.success && (t->state == "succeeded");
                v.skipped = v.skipped && skipped(*t);
                if (t->state != "succeeded") {
                    v.result_code = t->code;
                    v.error_category = t->category;
                }
            }
            if (snapshot.value("strict_cloud", false) && v.record.cloud.current) {
                for (const auto *t : required)
                    if (t->destination == provider && t->code == "verification_required") v.record.cloud.current = false;
            }
            if ((cancel || batch.timed_out) && waiting) {
                v.success = false;
                v.skipped = false;
                v.result_code = batch.timed_out ? "timeout" : "cancelled";
            }
            success = v.success;
            is_skipped = v.skipped;
            if (complete) batch.covers.push_back(v);
        }
        if (complete) {
            ++p.processed;
            if (success || (batch.owner != model::ResourceOwner::Song && is_skipped))
                ++p.success;
            else if (!is_skipped)
                ++p.failed;
        }
        const std::string state = complete ? (cancel && waiting ? "Cancelled" : success || is_skipped ? "Completed" :
                                                                                                        "Failed") :
                                             "Running";
        const auto key = std::to_string(index);
        auto &previous = snapshot["item_states"];
        const auto old = previous.is_object() ? previous.value(key, std::string {}) : std::string {};
        if (old != state) {
            (void)co_await storeItem(connection, id, index, item, state, complete && success && !is_skipped);
            (void)co_await jobs::query(connection, "crawl_batch_events/insert_event", id, static_cast<std::uint64_t>(index), !complete ? std::string("itemStarted") : success || is_skipped ? std::string("itemCompleted") :
                                                                                                                                                                                              std::string("itemFailed"),
                                       std::string("legacy resource item state changed"),
                                       batch.owner == model::ResourceOwner::Song ? songJson(item.song).dump() : coverJson(item.cover).dump(),
                                       jobs::now());
            previous[key] = state;
        }
    }
    bool post_failure = false;
    if (snapshot.value("wait_for_artifacts", false) && !cancel) {
        const auto producers = co_await jobs::query(connection, "resource_legacy_batches/producers", id);
        for (auto producer : producers.rows()) {
            const auto state = MySqlValueReader::stringAt(producer, 0);
            const bool pending = state == "pending" || state == "running" || state == "retry" || state == "waiting_input";
            if (pending && !expired)
                done = false;
            else if (state != "succeeded")
                post_failure = true;
        }
        for (const auto &t : tasks)
            if (t.owner == "song_artifact" && t.destination == "local") {
                if (active(t) && !expired)
                    done = false;
                else if (t.state != "succeeded" && t.state != "skipped")
                    post_failure = true;
            }
    }
    batch.terminal = done || cancel;
    if (batch.terminal) p.state_str = cancel ? "Cancelled" : batch.timed_out || (batch.owner != model::ResourceOwner::Song && p.failed > 0) ? "Failed" :
                                                                                                                                              "Completed";
    snapshot["items"] = itemsJson(items);
    (void)co_await jobs::query(connection, "resource_legacy_batches/update_snapshot", snapshot.dump(), id);
    (void)co_await jobs::query(connection, "resource_legacy_batches/update_projection", p.state_str, p.total, p.processed, p.success, p.failed, p.state_str, batch.terminal ? 1 : 0, id);
    if (batch.terminal) {
        if (batch.timed_out) (void)co_await jobs::query(connection, "crawl_batch_events/insert_event", id, 0, std::string("timeout"), std::string("legacy batch deadline exceeded"), std::string("{}"), jobs::now());
        if (post_failure) (void)co_await jobs::query(connection, "crawl_batch_events/insert_event", id, 0, std::string("resourcePostProcessingFailed"), std::string("post processing failed or exceeded legacy wait budget"), std::string("{}"), jobs::now());
        (void)co_await jobs::query(connection, "resource_legacy_batches/finish", batchJson(batch).dump(), id);
        (void)co_await jobs::query(connection, "crawl_batch_events/insert_event", id, 0, cancel ? std::string("batchCancelled") : p.state_str == "Failed" ? std::string("batchFailed") :
                                                                                                                                                            std::string("batchCompleted"),
                                   std::string("legacy resource view finished"),
                                   Json {{"operation_ids", batch.operation_ids}}.dump(),
                                   jobs::now());
    }
    co_return batch;
}
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::legacy_transfer
