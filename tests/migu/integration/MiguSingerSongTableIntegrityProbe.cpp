#include "migu/support/MySqlTestSupport.h"

#include "crawler/infrastructure/music/persistence/mysql/MySqlValueReader.h"
#include "crawler/infrastructure/platform/Environment.h"

#include <boost/asio/use_awaitable.hpp>
#include <boost/mysql/results.hpp>
#include <boost/mysql/statement.hpp>

#include <cstdint>
#include <cstddef>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace {

namespace mysql = orchestrator::modules::crawler::infrastructure::music::persistence::mysql;
namespace platform = orchestrator::modules::crawler::infrastructure::platform;
namespace support = orchestrator::modules::crawler::tests::migu::support;

// 本轮检查选中的歌手来源。
struct SingerTarget {
    std::uint64_t singer_source_ref_id = 0; // singer_source_refs.id
    std::string source_singer_id;           // Migu 歌手 ID
    std::string name;                       // 歌手名
    std::uint64_t song_count = 0;           // 当前关系表中关联歌曲数
};

// 单项完整性计数。
struct IntegrityMetric {
    std::string name;        // 指标名
    std::string sql;         // 只读检查 SQL
    std::uint64_t value = 0; // 指标值
};

// 专辑歌曲关系未关联专辑来源的样例。
struct MissingAlbumLinkSample {
    std::string source_album_id;   // 平台专辑 ID
    std::string source_album_name; // 歌曲来源表中的专辑名
    std::uint64_t song_count = 0;  // 受影响歌曲来源数
    std::uint64_t album_ref_count = 0; // album_source_refs 中可匹配记录数
};

// 读取无参数标量 SQL。
boost::asio::awaitable<std::uint64_t> scalarSql(
    const std::shared_ptr<mysql::MySqlConnectionPool> &pool,
    const std::string &sql
)
{
    boost::mysql::results result = co_await pool->coExecute(sql);
    if (result.rows().empty()) {
        co_return 0;
    }
    co_return mysql::MySqlValueReader::uint64At(result.rows().front(), 0);
}

// 优先选择指定歌手；指定歌手没有歌曲关系时，自动选择关系最多的歌手。
boost::asio::awaitable<SingerTarget> selectSinger(
    const std::shared_ptr<mysql::MySqlConnectionPool> &pool,
    std::string requested_source_singer_id
)
{
    auto connection = co_await pool->coAcquire();
    boost::mysql::results result;

    if (!requested_source_singer_id.empty()) {
        auto statement = co_await connection->async_prepare_statement(
            R"SQL(
SELECT r.id, r.source_singer_id, s.name, COUNT(DISTINCT ar.song_source_ref_id) AS song_count
FROM singer_source_refs r
JOIN singers s ON s.id = r.singer_db_id
JOIN song_artist_refs ar ON ar.singer_source_ref_id = r.id
WHERE r.source = 'migu'
  AND r.source_singer_id = ?
GROUP BY r.id, r.source_singer_id, s.name
HAVING song_count > 0
LIMIT 1
)SQL",
            boost::asio::use_awaitable
        );
        co_await connection->async_execute(statement.bind(std::move(requested_source_singer_id)), result, boost::asio::use_awaitable);
    }

    if (result.rows().empty()) {
        co_await connection->async_execute(
            R"SQL(
SELECT r.id, r.source_singer_id, s.name, COUNT(DISTINCT ar.song_source_ref_id) AS song_count
FROM singer_source_refs r
JOIN singers s ON s.id = r.singer_db_id
JOIN song_artist_refs ar ON ar.singer_source_ref_id = r.id
WHERE r.source = 'migu'
GROUP BY r.id, r.source_singer_id, s.name
ORDER BY song_count DESC, r.id ASC
LIMIT 1
)SQL",
            result,
            boost::asio::use_awaitable
        );
    }

    if (result.rows().empty()) {
        throw std::runtime_error("no singer with song relations found");
    }

    const auto row = result.rows().front();
    SingerTarget target;
    target.singer_source_ref_id = mysql::MySqlValueReader::uint64At(row, 0);
    target.source_singer_id = mysql::MySqlValueReader::stringAt(row, 1);
    target.name = mysql::MySqlValueReader::stringAt(row, 2);
    target.song_count = mysql::MySqlValueReader::uint64At(row, 3);
    co_return target;
}

// 构建只读完整性指标，全部限定在一个歌手来源关系上。
std::vector<IntegrityMetric> buildMetrics(const SingerTarget &target)
{
    const std::string id = std::to_string(target.singer_source_ref_id);
    std::vector<IntegrityMetric> metrics;

    metrics = {
        {"song_artist_relation_rows", "SELECT COUNT(*) FROM song_artist_refs ar WHERE ar.singer_source_ref_id = " + id},
        {"distinct_song_source_refs", "SELECT COUNT(DISTINCT ar.song_source_ref_id) FROM song_artist_refs ar WHERE ar.singer_source_ref_id = " + id},
        {"distinct_song_rows", "SELECT COUNT(DISTINCT r.song_db_id) FROM song_artist_refs ar JOIN song_source_refs r ON r.id = ar.song_source_ref_id WHERE ar.singer_source_ref_id = " + id},
        {"orphan_song_source_refs", "SELECT COUNT(*) FROM song_artist_refs ar LEFT JOIN song_source_refs r ON r.id = ar.song_source_ref_id WHERE ar.singer_source_ref_id = " + id + " AND r.id IS NULL"},
        {"orphan_song_rows", "SELECT COUNT(*) FROM song_artist_refs ar JOIN song_source_refs r ON r.id = ar.song_source_ref_id LEFT JOIN songs s ON s.id = r.song_db_id WHERE ar.singer_source_ref_id = " + id + " AND s.id IS NULL"},
        {"duplicate_artist_relations", "SELECT COUNT(*) FROM (SELECT ar.song_source_ref_id, ar.singer_source_ref_id, ar.artist_role, COUNT(*) AS c FROM song_artist_refs ar WHERE ar.singer_source_ref_id = " + id + " GROUP BY ar.song_source_ref_id, ar.singer_source_ref_id, ar.artist_role HAVING c > 1) d"},
        {"missing_song_identity", "SELECT COUNT(DISTINCT r.id) FROM song_artist_refs ar JOIN song_source_refs r ON r.id = ar.song_source_ref_id WHERE ar.singer_source_ref_id = " + id + " AND COALESCE(r.source_song_id, '') = '' AND COALESCE(r.source_content_id, '') = ''"},
        {"missing_audio_format_rows", "SELECT COUNT(*) FROM (SELECT DISTINCT ar.song_source_ref_id FROM song_artist_refs ar WHERE ar.singer_source_ref_id = " + id + ") x LEFT JOIN song_source_audio_formats f ON f.song_source_ref_id = x.song_source_ref_id WHERE f.id IS NULL"},
        {"audio_format_without_resource", "SELECT COUNT(*) FROM song_artist_refs ar JOIN song_source_audio_formats f ON f.song_source_ref_id = ar.song_source_ref_id LEFT JOIN song_resources sr ON sr.song_source_ref_id = f.song_source_ref_id AND sr.resource_kind = 'audio' AND sr.format_type = f.format_type WHERE ar.singer_source_ref_id = " + id + " AND sr.id IS NULL"},
        {"audio_resource_without_format", "SELECT COUNT(*) FROM song_artist_refs ar JOIN song_resources sr ON sr.song_source_ref_id = ar.song_source_ref_id AND sr.resource_kind = 'audio' LEFT JOIN song_source_audio_formats f ON f.song_source_ref_id = sr.song_source_ref_id AND f.format_type = sr.format_type WHERE ar.singer_source_ref_id = " + id + " AND f.id IS NULL"},
        {"cover_url_without_resource", "SELECT COUNT(DISTINCT r.id) FROM song_artist_refs ar JOIN song_source_refs r ON r.id = ar.song_source_ref_id LEFT JOIN song_resources sr ON sr.song_source_ref_id = r.id AND sr.resource_kind = 'cover' AND sr.format_type = '' WHERE ar.singer_source_ref_id = " + id + " AND COALESCE(r.cover_url, '') <> '' AND sr.id IS NULL"},
        {"lyric_url_without_resource", "SELECT COUNT(DISTINCT r.id) FROM song_artist_refs ar JOIN song_source_refs r ON r.id = ar.song_source_ref_id LEFT JOIN song_resources sr ON sr.song_source_ref_id = r.id AND sr.resource_kind = 'lyric' AND sr.format_type = '' WHERE ar.singer_source_ref_id = " + id + " AND COALESCE(r.lyric_url, '') <> '' AND sr.id IS NULL"},
        {"mrc_url_without_resource", "SELECT COUNT(DISTINCT r.id) FROM song_artist_refs ar JOIN song_source_refs r ON r.id = ar.song_source_ref_id LEFT JOIN song_resources sr ON sr.song_source_ref_id = r.id AND sr.resource_kind = 'mrc' AND sr.format_type = '' WHERE ar.singer_source_ref_id = " + id + " AND COALESCE(r.mrc_url, '') <> '' AND sr.id IS NULL"},
        {"trc_url_without_resource", "SELECT COUNT(DISTINCT r.id) FROM song_artist_refs ar JOIN song_source_refs r ON r.id = ar.song_source_ref_id LEFT JOIN song_resources sr ON sr.song_source_ref_id = r.id AND sr.resource_kind = 'trc' AND sr.format_type = '' WHERE ar.singer_source_ref_id = " + id + " AND COALESCE(r.trc_url, '') <> '' AND sr.id IS NULL"},
        {"album_id_without_album_song_ref", "SELECT COUNT(DISTINCT r.id) FROM song_artist_refs ar JOIN song_source_refs r ON r.id = ar.song_source_ref_id LEFT JOIN album_song_refs abr ON abr.song_source_ref_id = r.id AND abr.source = r.source AND abr.source_album_id = r.source_album_id WHERE ar.singer_source_ref_id = " + id + " AND COALESCE(r.source_album_id, '') <> '' AND abr.id IS NULL"},
        {"album_song_ref_without_album_source_ref", "SELECT COUNT(*) FROM song_artist_refs ar JOIN album_song_refs abr ON abr.song_source_ref_id = ar.song_source_ref_id WHERE ar.singer_source_ref_id = " + id + " AND abr.source_album_id <> '' AND abr.album_source_ref_id IS NULL"}
    };
    return metrics;
}

// 输出并判断指标；硬断链为失败，资源或专辑补充缺口只报告。
bool reportMetrics(const SingerTarget &target, const std::vector<IntegrityMetric> &metrics)
{
    std::cout << "[Singer] source_singer_id=" << target.source_singer_id << ", name=" << target.name << ", relation_song_count=" << target.song_count << std::endl;

    bool hard_ok = true;
    bool optional_ok = true;
    for (const auto &metric : metrics) {
        const auto delimiter = metric.name.find('\n');
        const std::string name = metric.name.substr(0, delimiter);
        std::cout << "[Metric] " << name << "=" << metric.value << std::endl;
        if ((name == "orphan_song_source_refs" || name == "orphan_song_rows" || name == "duplicate_artist_relations" || name == "missing_song_identity") && metric.value != 0) {
            hard_ok = false;
        }
        if ((name == "missing_audio_format_rows" || name == "audio_format_without_resource" || name == "audio_resource_without_format" || name == "cover_url_without_resource" || name == "lyric_url_without_resource" || name == "mrc_url_without_resource" || name == "trc_url_without_resource" || name == "album_id_without_album_song_ref" || name == "album_song_ref_without_album_source_ref") && metric.value != 0) {
            optional_ok = false;
        }
    }

    std::cout << "[Result] hard_integrity=" << (hard_ok ? "ok" : "failed")
              << ", enrichment_integrity=" << (optional_ok ? "ok" : "has_gaps") << std::endl;
    return hard_ok;
}

// 查询少量专辑来源未关联样例，帮助判断是未抓专辑还是关联条件问题。
boost::asio::awaitable<std::vector<MissingAlbumLinkSample>> missingAlbumLinkSamples(
    const std::shared_ptr<mysql::MySqlConnectionPool> &pool,
    const SingerTarget &target
)
{
    const std::string id = std::to_string(target.singer_source_ref_id);
    auto connection = co_await pool->coAcquire();
    boost::mysql::results result;
    const std::string sql = std::string {R"SQL(
SELECT
    abr.source_album_id,
    COALESCE(MAX(r.source_album_name), '') AS source_album_name,
    COUNT(DISTINCT abr.song_source_ref_id) AS song_count,
    COUNT(DISTINCT al.id) AS album_ref_count
FROM song_artist_refs ar
JOIN album_song_refs abr ON abr.song_source_ref_id = ar.song_source_ref_id
JOIN song_source_refs r ON r.id = abr.song_source_ref_id
LEFT JOIN album_source_refs al ON al.source = abr.source AND al.source_album_id = abr.source_album_id
WHERE ar.singer_source_ref_id = )SQL" + id + R"SQL(
  AND abr.source_album_id <> ''
  AND abr.album_source_ref_id IS NULL
GROUP BY abr.source_album_id
ORDER BY song_count DESC, abr.source_album_id ASC
LIMIT 5
)SQL"};
    co_await connection->async_execute(
        sql,
        result,
        boost::asio::use_awaitable
    );

    std::vector<MissingAlbumLinkSample> samples;
    samples.reserve(result.rows().size());
    for (const auto row : result.rows()) {
        MissingAlbumLinkSample sample;
        sample.source_album_id = mysql::MySqlValueReader::stringAt(row, 0);
        sample.source_album_name = mysql::MySqlValueReader::stringAt(row, 1);
        sample.song_count = mysql::MySqlValueReader::uint64At(row, 2);
        sample.album_ref_count = mysql::MySqlValueReader::uint64At(row, 3);
        samples.push_back(std::move(sample));
    }
    co_return samples;
}

// 执行完整性检查主协程。
boost::asio::awaitable<bool> runProbe(const std::shared_ptr<mysql::MySqlConnectionPool> &pool)
{
    platform::Environment::loadDotEnvFile(".env.test");
    platform::Environment::loadDotEnvFile(".env");
    const std::string requested = platform::Environment::getStringOr("CRAWLER_TEST_MIGU_SINGER_SOURCE_ID", "112");
    const SingerTarget target = co_await selectSinger(pool, requested);
    std::vector<IntegrityMetric> metrics = buildMetrics(target);
    for (auto &metric : metrics) {
        metric.value = co_await scalarSql(pool, metric.sql);
    }
    const bool ok = reportMetrics(target, metrics);
    const auto samples = co_await missingAlbumLinkSamples(pool, target);
    for (const auto &sample : samples) {
        std::cout << "[MissingAlbumLink] source_album_id=" << sample.source_album_id
                  << ", source_album_name=" << sample.source_album_name
                  << ", song_count=" << sample.song_count
                  << ", matching_album_source_refs=" << sample.album_ref_count << std::endl;
    }
    co_return ok;
}

} // namespace

int main()
{
    try {
        auto pool = support::makeInitializedPool();
        const bool ok = support::runOnExecutor(pool->executor(), runProbe(pool), "Migu singer song table integrity probe");
        if (!ok) {
            std::cerr << "MiguSingerSongTableIntegrityProbe failed: hard integrity gaps found" << std::endl;
            return 1;
        }
        std::cout << "MiguSingerSongTableIntegrityProbe passed" << std::endl;
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "MiguSingerSongTableIntegrityProbe failed: " << e.what() << std::endl;
        return 1;
    }
}
