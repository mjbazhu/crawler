#include "MySqlMetadataWriteSession.h"
#include "MySqlResourceCloudCleanupCapture.h"
#include "MySqlResourceTransferJobsSupport.h"

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

        auto guarded = co_await statement("album_resources/alias_transfer_guard");
        boost::mysql::results guard_rows;
        co_await connection_->async_execute(guarded.bind(alias_id, canonical_id), guard_rows, boost::asio::use_awaitable);
        for (const auto row : guard_rows.rows())
            if (MySqlValueReader::intAt(row, 1)) throw std::logic_error("album alias transfer requires reconciliation");

        // 旧来源随后级联删除；统一回执及旧字段证据先随本事务保存。
        const auto retiring = co_await transfer_jobs::query(connection_, "album_resources/delete_scope", alias_id);
        std::vector<application::model::ResourceIdentity> retiring_resources;
        for (const auto row : retiring.rows()) retiring_resources.push_back({application::model::ResourceOwner::Album, MySqlValueReader::uint64At(row, 0)});
        if (!retiring_resources.empty()) (void)co_await cleanup::captureOnConnection(connection_, std::move(retiring_resources), cleanup::CaptureReason::AlbumAlias);

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
        // 只有同一完整内容参考才迁移逐端事实；其他事实归档并阻止隐式重下。
        co_await write("album_resources/alias_transfer_reference", alias_id, canonical_id);
        const auto archived = co_await write("album_resources/alias_transfer_archive", canonical_id, alias_id);
        co_await write("album_resources/alias_transfer_receipts", canonical_id, alias_id);
        co_await write("album_resources/alias_transfer_skip", alias_id, canonical_id);
        co_await write("album_resources/alias_transfer_block", canonical_id, alias_id);
        if (archived) {
            auto retained = co_await statement("album_resources/find_current");
            boost::mysql::results retained_rows;
            co_await connection_->async_execute(retained.bind(alias_id), retained_rows, boost::asio::use_awaitable);
            if (!retained_rows.rows().empty()) previous_paths.erase(MySqlValueReader::stringAt(retained_rows.rows().front(), 0));
        }
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
