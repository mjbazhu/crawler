#include "MySqlMetadataWriteSession.h"

#include <set>
#include <stdexcept>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::detail {

boost::asio::awaitable<void> MetadataWriteSession::mergeAlbumAliases()
{
    for (const auto &[material, canonical] : album_aliases_) {
        auto lock = co_await statement("album_source_refs/lock_identity_pair");
        boost::mysql::results rows;
        co_await connection_->async_execute(lock.bind(material, canonical), rows, boost::asio::use_awaitable);
        std::uint64_t alias_id = 0, canonical_id = 0, alias_album = 0, canonical_album = 0;
        for (const auto row : rows.rows()) {
            if (MySqlValueReader::stringAt(row, 2) == material) {
                alias_id = MySqlValueReader::uint64At(row, 0);
                alias_album = MySqlValueReader::uint64At(row, 1);
                if (MySqlValueReader::intAt(row, 3) != 0) throw std::invalid_argument("album material alias owns another digital mapping");
            } else {
                canonical_id = MySqlValueReader::uint64At(row, 0);
                canonical_album = MySqlValueReader::uint64At(row, 1);
            }
        }
        if (alias_id == 0) continue;
        if (canonical_id == 0) throw std::invalid_argument("canonical album source is missing");

        co_await write("albums/merge_alias_metadata", alias_album, canonical_album);
        co_await write("album_source_refs/merge_alias_metadata", alias_id, canonical_id);
        co_await write("album_artist_refs/merge_album_alias", canonical_id, alias_id);
        co_await write("album_song_refs/merge_album_alias", canonical_id, alias_id);

        // 删除别名前记录两端文件，只在提交后回收没有继续被规范来源引用的文件。
        std::set<std::string> previous_paths;
        for (const auto owner : {alias_id, canonical_id}) {
            auto current = co_await statement("album_resources/find_current");
            boost::mysql::results result;
            co_await connection_->async_execute(current.bind(owner), result, boost::asio::use_awaitable);
            if (!result.rows().empty()) {
                const auto path = MySqlValueReader::stringAt(result.rows().front(), 0);
                if (!path.empty()) previous_paths.insert(path);
            }
        }
        co_await write("album_resources/merge_album_alias", canonical_id, alias_id);
        auto current = co_await statement("album_resources/find_current");
        boost::mysql::results result;
        co_await connection_->async_execute(current.bind(canonical_id), result, boost::asio::use_awaitable);
        if (!result.rows().empty()) previous_paths.erase(MySqlValueReader::stringAt(result.rows().front(), 0));
        replaced_cover_paths_.insert(replaced_cover_paths_.end(), previous_paths.begin(), previous_paths.end());

        co_await write("album_source_refs/delete_alias", alias_id);
        co_await write("albums/delete_unreferenced", alias_album);
    }
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::detail
