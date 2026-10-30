#include "MySqlLegacyResourceTransferSupport.h"

#include <algorithm>
#include <set>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::legacy_transfer {
namespace jobs = transfer_jobs;
namespace {
// 查询输出与cloud_status筛选复用同一事实表达式，人工声明只属于待核对。
std::string cloudCurrent(bool strict)
{
    return "COALESCE(c.confirmed_content_generation=r.content_generation AND c.confirmed_target_revision=d.target_revision AND r.reference_sha256 REGEXP '^[0-9a-f]{64}$' AND BINARY c.checksum=BINARY r.reference_sha256 AND c.size_bytes=r.reference_size_bytes AND c.cloud_path<>'' AND c.path_unresolved=0 AND c.verification IN(" + std::string(strict ? "'sha256_verified'" : "'hash_matched','sha256_verified'") + "),0)";
}
std::string cloudStatus(bool strict)
{
    return "CASE WHEN c.task_state='running' THEN 1 WHEN c.task_state='uncertain' THEN 4 WHEN " + cloudCurrent(strict) + " THEN 2 WHEN c.verification IN('operator_attested','object_observed') OR COALESCE(c.cloud_path,'')<>'' OR (c.transfer_policy='skip' AND c.policy_content_generation=r.content_generation AND c.policy_target_revision=d.target_revision AND c.baseline_id<>'' AND c.skip_reason<>'') THEN 4 WHEN c.task_state IN('failed','blocked','unavailable') THEN 3 ELSE 0 END";
}
// SQL列表只来自经过边界校验的整数，字符串仍绑定参数。
template <class T>
std::string list(const std::vector<T> &values)
{
    std::string out;
    for (const auto v : values) {
        if (!out.empty()) out += ',';
        out += std::to_string(v);
    }
    return out;
}
void replace(std::string &text, std::string_view key, const std::string &value)
{
    const auto at = text.find(key);
    if (at == std::string::npos) throw std::logic_error("legacy selector template missing");
    text.replace(at, key.size(), value);
}
Item song(boost::mysql::row_view row)
{
    Item item;
    auto &v = item.song;
    v.resource_id = MySqlValueReader::uint64At(row, 0);
    item.identity = {model::ResourceOwner::Song, v.resource_id};
    v.song_source_ref_id = MySqlValueReader::uint64At(row, 1);
    v.source = MySqlValueReader::stringAt(row, 2);
    v.source_song_id = MySqlValueReader::stringAt(row, 3);
    v.source_content_id = MySqlValueReader::stringAt(row, 4);
    v.title = MySqlValueReader::stringAt(row, 5);
    v.resource_kind = MySqlValueReader::stringAt(row, 6);
    v.format_type = MySqlValueReader::stringAt(row, 7);
    v.local_path = MySqlValueReader::stringAt(row, 8);
    v.size_bytes = MySqlValueReader::uint64At(row, 9);
    v.checksum = MySqlValueReader::stringAt(row, 10);
    return item;
}
Item cover(boost::mysql::row_view row, model::ResourceOwner owner, bool enabled)
{
    Item item;
    item.identity = {owner, MySqlValueReader::uint64At(row, 0)};
    auto &r = item.cover.record;
    r.owner = owner == model::ResourceOwner::Singer ? model::CoverOwner::Singer : model::CoverOwner::Album;
    r.resource_id = item.identity.resource_id;
    r.source_ref_id = MySqlValueReader::uint64At(row, 1);
    r.source = MySqlValueReader::stringAt(row, 2);
    r.source_id = MySqlValueReader::stringAt(row, 3);
    r.file.local_path = MySqlValueReader::stringAt(row, 4);
    r.file.size_bytes = MySqlValueReader::uint64At(row, 5);
    r.file.checksum = MySqlValueReader::stringAt(row, 6);
    r.file.status = MySqlValueReader::intAt(row, 7);
    auto &c = r.cloud;
    c.enabled = enabled;
    c.receipt = {MySqlValueReader::stringAt(row, 8), MySqlValueReader::stringAt(row, 9), MySqlValueReader::uint64At(row, 11), MySqlValueReader::stringAt(row, 10)};
    c.uploaded_at = MySqlValueReader::uint64At(row, 12);
    c.current = MySqlValueReader::uint64At(row, 24) != 0;
    c.status = MySqlValueReader::intAt(row, 25);
    c.result_code = MySqlValueReader::stringAt(row, 21);
    c.error_category = MySqlValueReader::stringAt(row, 22);
    return item;
}
} // namespace

boost::asio::awaitable<std::vector<Item>> select(boost::mysql::pooled_connection &connection, const model::LegacyResourceTransferRequest &request, bool facts_only, bool page_query, std::uint64_t *total)
{
    const bool songs = request.owner == model::ResourceOwner::Song;
    const auto &ids = songs ? request.songs.resource_ids : request.covers.query.resource_ids;
    const auto limit = songs ? request.songs.limit : static_cast<std::size_t>(request.covers.limit);
    if (limit < 1 || limit > (songs ? 1000U : 200U) || ids.size() > (songs ? 1000U : 200U)) throw std::invalid_argument("invalid legacy resource range");
    for (const auto id : ids)
        if (!id) throw std::invalid_argument("invalid legacy resource identity");
    std::string filters = ids.empty() ? "" : " AND r.id IN (" + list(ids) + ")";
    if (!songs && !request.covers.query.source_ref_ids.empty()) {
        if (request.covers.query.source_ref_ids.size() > 200) throw std::invalid_argument("invalid legacy cover scope");
        for (auto id : request.covers.query.source_ref_ids)
            if (!id) throw std::invalid_argument("invalid legacy cover source");
        filters += " AND s.id IN (" + list(request.covers.query.source_ref_ids) + ")";
    }
    if (page_query) {
        if (songs || request.covers.query.page < 1 || request.covers.query.source != "migu") throw std::invalid_argument("invalid legacy cover page");
        for (int status : request.covers.query.statuses)
            if (status < 0 || status > 4) throw std::invalid_argument("invalid legacy cover status");
        for (int status : request.covers.query.cloud_statuses)
            if (status < 0 || status > 4) throw std::invalid_argument("invalid legacy cloud status");
        if (!request.covers.query.statuses.empty()) filters += " AND r.status IN (" + list(request.covers.query.statuses) + ")";
        if (!request.covers.query.cloud_statuses.empty()) filters += " AND (" + cloudStatus(request.strict_cloud) + ") IN (" + list(request.covers.query.cloud_statuses) + ")";
    }
    if (!facts_only) {
        filters += " AND NOT EXISTS (SELECT 1 FROM resource_transfer_slots occupied WHERE occupied.owner_type='" + std::string(model::resourceOwnerName(request.owner)) + "' AND occupied.resource_id=r.id AND (occupied.destination='local'" + (!songs && request.cloud_provider ? " OR occupied.destination='" + std::string(model::resourceDestinationName(*request.cloud_provider)) + "'" : "") + "))";
        std::vector<int> statuses = songs ? request.songs.statuses : request.covers.query.statuses;
        if (statuses.empty()) statuses = songs ? std::vector<int> {0, 3} : (request.covers.refresh || request.cloud_provider ? std::vector<int> {0, 2, 3} : std::vector<int> {0, 3});
        if (songs) {
            std::erase_if(statuses, [](int state) { return state != 0 && state != 3; });
            if (!ids.empty()) statuses = {0, 3};
        } else
            for (const auto state : statuses)
                if (state != 0 && state != 3 && !(state == 2 && (request.covers.refresh || request.cloud_provider))) throw std::invalid_argument("invalid legacy cover status");
        const auto status_filter = statuses.empty() ? "FALSE" : "r.status IN (" + list(statuses) + ")";
        const auto skip = "r.local_skip_generation=r.content_generation AND r.local_skip_target_revision=(SELECT target_revision FROM resource_destination_states WHERE destination='local') AND r.local_skip_baseline<>'' AND r.local_skip_reason<>''";
        filters += " AND (" + status_filter + (ids.empty() ? "" : " OR (" + std::string(skip) + ")") + ")";
        if (!songs && request.cloud_provider && !request.covers.refresh) {
            filters += " AND (r.status IN(0,3) OR (r.status=2 AND (c.id IS NULL OR c.task_state<>'running') AND NOT (" + cloudCurrent(request.strict_cloud) + ")))";
        }
        if (!songs && !request.covers.query.cloud_statuses.empty()) filters += " AND (" + cloudStatus(request.strict_cloud) + ") IN(" + list(request.covers.query.cloud_statuses) + ")";
    }
    auto text = sql::SqlFileLoader::loadOrThrow("sql/mysql/resource_legacy_batches/" + std::string(songs ? "select_songs" : request.owner == model::ResourceOwner::Singer ? "select_singers" :
                                                                                                                                                                            "select_albums")
                                                + ".sql");
    replace(text, "/*__FILTERS__*/", filters);
    if (!songs) {
        replace(text, "/*__CLOUD_CURRENT__*/", cloudCurrent(request.strict_cloud));
        replace(text, "/*__CLOUD_STATUS__*/", cloudStatus(request.strict_cloud));
    }
    if (songs) replace(text, "/*__ORDER__*/", !ids.empty() ? "FIELD(r.id," + list(ids) + ")," : "r.updated_at,");
    if (page_query && total) {
        const auto begin = text.find("FROM "), end = text.find(" ORDER BY ");
        if (begin == std::string::npos || end == std::string::npos) throw std::logic_error("legacy page template missing");
        const auto count_text = "SELECT COUNT(*) " + text.substr(begin, end - begin);
        auto count_statement = co_await connection->async_prepare_statement(count_text, boost::asio::use_awaitable);
        boost::mysql::results counts;
        const auto provider = request.cloud_provider ? std::string(model::resourceDestinationName(*request.cloud_provider)) : "local";
        co_await connection->async_execute(count_statement.bind(provider, request.covers.query.source), counts, boost::asio::use_awaitable);
        *total = MySqlValueReader::uint64At(counts.rows().front(), 0);
    }
    auto statement = co_await connection->async_prepare_statement(text, boost::asio::use_awaitable);
    boost::mysql::results rows;
    if (songs) {
        const auto &q = request.songs;
        const auto kind = ids.empty() ? q.resource_kind : "", content = ids.empty() ? q.source_content_id : "", format = ids.empty() ? q.format_type : "";
        co_await connection->async_execute(statement.bind(q.source, kind, kind, content, content, format, format, static_cast<std::uint64_t>(limit), static_cast<std::uint64_t>(ids.empty() ? q.offset : 0)), rows, boost::asio::use_awaitable);
    } else {
        const auto provider = request.cloud_provider ? std::string(model::resourceDestinationName(*request.cloud_provider)) : "local";
        const auto offset = page_query ? static_cast<std::uint64_t>(request.covers.query.page - 1) * limit : 0;
        co_await connection->async_execute(statement.bind(provider, request.covers.query.source, static_cast<std::uint64_t>(limit), offset), rows, boost::asio::use_awaitable);
    }
    std::vector<Item> result;
    for (const auto row : rows.rows()) result.push_back(songs ? song(row) : cover(row, request.owner, request.cloud_provider.has_value()));
    co_return result;
}
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::legacy_transfer
