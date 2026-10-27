#include "MySqlMetadataWriteSession.h"

#include <nlohmann/json.hpp>

#include <set>
#include <stdexcept>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::detail {

boost::asio::awaitable<void> MetadataWriteSession::normalizeAlbumIdentities(application::model::MetadataWriteBatch &batch)
{
    std::set<std::string> identities;
    for (const auto &album : batch.albums) {
        if (album.source != "migu") continue;
        if (!album.source_album_id.empty()) identities.insert(album.source_album_id);
        if (!album.source_material_id.empty()) identities.insert(album.source_material_id);
    }
    for (const auto &song : batch.songs) {
        if (song.source != "migu") continue;
        if (!song.source_album_id.empty()) identities.insert(song.source_album_id);
        if (!song.source_album_material_id.empty()) identities.insert(song.source_album_material_id);
    }
    if (identities.empty()) co_return;

    std::map<std::string, std::string> materials;
    const auto add = [&](const std::string &material, const std::string &canonical) {
        if (material.empty() || canonical.empty() || material == canonical) return;
        const auto [alias, inserted] = album_aliases_.emplace(material, canonical);
        const auto [owner, unique] = materials.emplace(canonical, material);
        if ((!inserted && alias->second != canonical) || (!unique && owner->second != material)) {
            throw std::invalid_argument("album material identity mapping is ambiguous");
        }
    };
    // 单次有界查询按物料索引加锁；不存在的映射也受事务间隙锁保护。
    auto lookup = co_await statement("migu_dalbum_details/select_identity_mappings");
    const std::string input = nlohmann::json(identities).dump();
    boost::mysql::results mappings;
    co_await connection_->async_execute(lookup.bind(input), mappings, boost::asio::use_awaitable);
    for (const auto row : mappings.rows()) add(MySqlValueReader::stringAt(row, 0), MySqlValueReader::stringAt(row, 1));
    for (const auto &album : batch.albums) {
        if (album.source == "migu") add(album.source_material_id, album.source_album_id);
    }
    for (const auto &[material, canonical] : album_aliases_) {
        if (album_aliases_.contains(canonical)) throw std::invalid_argument("album material identity contains a cycle or chain");
    }

    for (auto &album : batch.albums) {
        if (album.source != "migu") continue;
        const auto alias = album_aliases_.find(album.source_album_id);
        if (alias != album_aliases_.end()) {
            album.source_material_id = alias->first;
            album.source_album_id = alias->second;
            // 物料入口的普通专辑响应不能冒充数字专辑的详情采集结果。
            album.detail_sync_status = 0;
            album.songs_sync.reset();
        }
        if (const auto material = materials.find(album.source_album_id); material != materials.end()) {
            album.source_material_id = material->second;
            album.source_resource_type = "5";
            album.source_res_type = "1";
            album.source_album_kind = 2;
        }
    }
    for (auto &song : batch.songs) {
        if (song.source != "migu") continue;
        const auto alias = album_aliases_.find(song.source_album_id);
        if (alias == album_aliases_.end()) continue;
        song.source_album_material_id = alias->first;
        song.source_album_id = alias->second;
    }
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::detail
