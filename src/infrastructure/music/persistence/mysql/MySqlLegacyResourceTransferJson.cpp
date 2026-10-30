#include "MySqlLegacyResourceTransferSupport.h"

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::legacy_transfer {
Json songJson(const model::SongResourceProcessItem &v)
{
    return {{"resource_id", v.resource_id}, {"song_source_ref_id", v.song_source_ref_id}, {"source", v.source}, {"source_song_id", v.source_song_id}, {"source_content_id", v.source_content_id}, {"title", v.title}, {"resource_kind", v.resource_kind}, {"format_type", v.format_type}, {"ok", v.ok}, {"skipped", v.skipped}, {"related", v.related}, {"synthetic", v.synthetic}, {"local_path", v.local_path}, {"cloud_backend", v.cloud_backend}, {"cloud_path", v.cloud_path}, {"size_bytes", v.size_bytes}, {"checksum", v.checksum}, {"error_category", v.error_category}, {"result_code", v.result_code}, {"message", v.message}};
}
model::SongResourceProcessItem songFromJson(const Json &j)
{
    model::SongResourceProcessItem v;
    j.at("resource_id").get_to(v.resource_id);
    j.at("song_source_ref_id").get_to(v.song_source_ref_id);
    j.at("source").get_to(v.source);
    j.at("source_song_id").get_to(v.source_song_id);
    j.at("source_content_id").get_to(v.source_content_id);
    j.at("title").get_to(v.title);
    j.at("resource_kind").get_to(v.resource_kind);
    j.at("format_type").get_to(v.format_type);
    j.at("ok").get_to(v.ok);
    j.at("skipped").get_to(v.skipped);
    j.at("related").get_to(v.related);
    j.at("synthetic").get_to(v.synthetic);
    j.at("local_path").get_to(v.local_path);
    j.at("cloud_backend").get_to(v.cloud_backend);
    j.at("cloud_path").get_to(v.cloud_path);
    j.at("size_bytes").get_to(v.size_bytes);
    j.at("checksum").get_to(v.checksum);
    j.at("error_category").get_to(v.error_category);
    j.at("result_code").get_to(v.result_code);
    j.at("message").get_to(v.message);
    return v;
}
Json coverJson(const model::MetadataCoverItem &v)
{
    const auto &r = v.record;
    const auto &f = r.file;
    const auto &c = r.cloud;
    return {{"owner_type", model::coverOwnerName(r.owner)}, {"resource_id", r.resource_id}, {"source_ref_id", r.source_ref_id}, {"source", r.source}, {"source_id", r.source_id}, {"file", {{"local_path", f.local_path}, {"size_bytes", f.size_bytes}, {"checksum", f.checksum}, {"status", f.status}, {"error_category", f.error_category}, {"result_code", f.result_code}, {"last_error", f.last_error}}}, {"cloud", {{"enabled", c.enabled}, {"status", c.status}, {"current", c.current}, {"backend", c.receipt.backend}, {"path", c.receipt.path}, {"size_bytes", c.receipt.size_bytes}, {"checksum", c.receipt.checksum}, {"uploaded_at", c.uploaded_at}, {"error_category", c.error_category}, {"result_code", c.result_code}, {"last_error", c.last_error}}}, {"success", v.success}, {"skipped", v.skipped}, {"result_code", v.result_code}, {"error_category", v.error_category}, {"last_error", v.last_error}};
}
model::MetadataCoverItem coverFromJson(const Json &j)
{
    model::MetadataCoverItem v;
    auto &r = v.record;
    auto &f = r.file;
    auto &c = r.cloud;
    r.owner = j.at("owner_type") == "singer" ? model::CoverOwner::Singer : model::CoverOwner::Album;
    j.at("resource_id").get_to(r.resource_id);
    j.at("source_ref_id").get_to(r.source_ref_id);
    j.at("source").get_to(r.source);
    j.at("source_id").get_to(r.source_id);
    const auto &a = j.at("file");
    a.at("local_path").get_to(f.local_path);
    a.at("size_bytes").get_to(f.size_bytes);
    a.at("checksum").get_to(f.checksum);
    a.at("status").get_to(f.status);
    a.at("error_category").get_to(f.error_category);
    a.at("result_code").get_to(f.result_code);
    a.at("last_error").get_to(f.last_error);
    const auto &b = j.at("cloud");
    b.at("enabled").get_to(c.enabled);
    b.at("status").get_to(c.status);
    b.at("current").get_to(c.current);
    b.at("backend").get_to(c.receipt.backend);
    b.at("path").get_to(c.receipt.path);
    b.at("size_bytes").get_to(c.receipt.size_bytes);
    b.at("checksum").get_to(c.receipt.checksum);
    b.at("uploaded_at").get_to(c.uploaded_at);
    b.at("error_category").get_to(c.error_category);
    b.at("result_code").get_to(c.result_code);
    b.at("last_error").get_to(c.last_error);
    j.at("success").get_to(v.success);
    j.at("skipped").get_to(v.skipped);
    j.at("result_code").get_to(v.result_code);
    j.at("error_category").get_to(v.error_category);
    j.at("last_error").get_to(v.last_error);
    return v;
}
Json itemsJson(const std::vector<Item> &items)
{
    auto result = Json::array();
    for (const auto &item : items) result.push_back(item.identity.owner == model::ResourceOwner::Song ? songJson(item.song) : coverJson(item.cover));
    return result;
}
std::vector<Item> itemsFromJson(const Json &value, model::ResourceOwner owner)
{
    std::vector<Item> result;
    for (const auto &j : value) {
        Item item;
        item.identity = {owner, j.at("resource_id").get<std::uint64_t>()};
        if (owner == model::ResourceOwner::Song)
            item.song = songFromJson(j);
        else
            item.cover = coverFromJson(j);
        result.push_back(std::move(item));
    }
    return result;
}
Json batchJson(const model::LegacyResourceTransferBatch &v)
{
    auto songs = Json::array(), covers = Json::array();
    for (const auto &i : v.songs.items) songs.push_back(songJson(i));
    for (const auto &i : v.covers) covers.push_back(coverJson(i));
    const auto &p = v.progress;
    const auto &r = v.songs;
    return {{"batch_id", v.batch_id}, {"owner_type", model::resourceOwnerName(v.owner)}, {"operations", v.operation_ids}, {"terminal", v.terminal}, {"timed_out", v.timed_out}, {"state", p.state_str}, {"total", p.total}, {"processed", p.processed}, {"success", p.success}, {"failed", p.failed}, {"duration", p.duration_seconds}, {"songs", songs}, {"covers", covers}, {"counts", {r.claimed_count, r.succeeded_count, r.failed_count, r.skipped_count, r.related_succeeded_count, r.related_failed_count, r.related_skipped_count, r.synthetic_related_skipped_count}}};
}
model::LegacyResourceTransferBatch batchFromJson(const Json &j)
{
    model::LegacyResourceTransferBatch v;
    v.batch_id = j.at("batch_id");
    v.owner = *model::parseResourceOwner(j.at("owner_type").get<std::string>());
    j.at("operations").get_to(v.operation_ids);
    j.at("terminal").get_to(v.terminal);
    v.timed_out = j.value("timed_out", false);
    auto &p = v.progress;
    p.batch_id = v.batch_id;
    j.at("state").get_to(p.state_str);
    j.at("total").get_to(p.total);
    j.at("processed").get_to(p.processed);
    j.at("success").get_to(p.success);
    j.at("failed").get_to(p.failed);
    j.at("duration").get_to(p.duration_seconds);
    auto &r = v.songs;
    r.batch_id = v.batch_id;
    const auto &c = j.at("counts");
    r.claimed_count = c[0];
    r.succeeded_count = c[1];
    r.failed_count = c[2];
    r.skipped_count = c[3];
    r.related_succeeded_count = c[4];
    r.related_failed_count = c[5];
    r.related_skipped_count = c[6];
    r.synthetic_related_skipped_count = c[7];
    for (const auto &item : j.at("songs")) r.items.push_back(songFromJson(item));
    for (const auto &item : j.at("covers")) v.covers.push_back(coverFromJson(item));
    return v;
}
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::legacy_transfer
