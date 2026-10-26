#include "crawler/application/use_cases/MetadataBatchBuilder.h"
#include "crawler/application/use_cases/MetadataPersistenceService.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMetadataBatchWriter.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlSongResourceQueue.h"
#include "crawler/infrastructure/storage/filesystem/MetadataCoverStore.h"
#include "migu/support/MySqlTestSupport.h"
#include "mocks/MockHttpClient.h"

#include <filesystem>
#include <iostream>
#include <stdexcept>
#include <string>

namespace crawler = orchestrator::modules::crawler;
namespace model = crawler::application::model;
namespace cases = crawler::application::use_cases;
namespace mysql = crawler::infrastructure::music::persistence::mysql;
namespace storage = crawler::infrastructure::storage::filesystem;
namespace support = crawler::tests::migu::support;
namespace sql = crawler::infrastructure::music::persistence::sql;
namespace http = crawler::infrastructure::http::client;

// 测试失败必须返回非零退出码，不依赖可在 Release 中禁用的 assert。
void require(bool condition, const std::string &message)
{
    if (!condition) throw std::runtime_error(message);
}

// 只在显式指定的独立测试库中执行 fixture SQL。
boost::mysql::results execute(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, std::string text)
{
    return support::runOnExecutor(pool->executor(), pool->coExecute(std::move(text)), "ownership SQL");
}

// 读取单个计数，测试数据不包含业务库真实记录。
std::uint64_t scalar(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, const std::string &text)
{
    const auto result = execute(pool, text);
    require(!result.rows().empty(), "scalar query returned no row");
    return mysql::MySqlValueReader::uint64At(result.rows().front(), 0);
}

// 最新迁移重复执行时不得改变身份、关系或同步结果。
boost::asio::awaitable<bool> repeatMigration(const std::shared_ptr<mysql::MySqlConnectionPool> &pool)
{
    auto connection = co_await pool->coAcquire();
    boost::mysql::results result;
    for (const auto &statement : sql::SqlFileLoader::splitStatements(sql::SqlFileLoader::loadOrThrow("sql/mysql/migrations/V2026102601__source_identity_and_album_count_semantics.sql"))) {
        co_await connection->async_execute(statement, result, boost::asio::use_awaitable);
    }
    co_return true;
}

// 先以历史 schema 构造关系和资源 fixture，再让正式 migration 升级。
std::shared_ptr<mysql::MySqlConnectionPool> migrateFixture(mysql::MySqlConfig config)
{
    const auto historical = std::filesystem::temp_directory_path() / (config.database + "_migrations");
    std::filesystem::create_directories(historical);
    for (const auto &entry : std::filesystem::directory_iterator("sql/mysql/migrations")) {
        if (entry.path().filename().string() < "V2026102501") {
            std::filesystem::copy_file(entry.path(), historical / entry.path().filename(), std::filesystem::copy_options::overwrite_existing);
        }
    }
    auto old_config = config;
    old_config.migrations_path = historical.string();
    auto previous = std::make_shared<mysql::MySqlConnectionPool>(old_config);
    require(support::runOnExecutor(previous->executor(), previous->coInitializeSchema(), "historical schema"), "historical migrations failed");
    execute(previous, "INSERT INTO singers(name,normalized_name) VALUES('旧歌手','ownership-legacy-singer')");
    execute(previous, "INSERT INTO singer_source_refs(singer_db_id,source,source_item_key,source_singer_id) SELECT id,'migu','singer:ownership-old','ownership-old' FROM singers WHERE normalized_name='ownership-legacy-singer'");
    execute(previous, "INSERT INTO albums(title,normalized_title,identity_hash) VALUES('旧专辑','ownership-legacy-album',SHA2('old-name-identity',256))");
    execute(previous, "INSERT INTO album_source_refs(album_db_id,source,source_item_key,source_album_id,source_artist_id,total_count) SELECT id,'migu','album:ownership-old','ownership-old','ownership-old','1' FROM albums WHERE normalized_title='ownership-legacy-album'");
    execute(previous, "INSERT INTO album_source_refs(album_db_id,source,source_item_key,source_album_id) SELECT id,'migu','album:ownership-alias','ownership-alias' FROM albums WHERE normalized_title='ownership-legacy-album'");
    execute(previous, "INSERT INTO songs(title,normalized_title) VALUES('旧歌曲','ownership-legacy-song')");
    execute(previous, "INSERT INTO song_source_refs(song_db_id,source,source_item_key,source_content_id,source_album_id,source_album_name,source_album_pinyin) SELECT id,'migu','content:ownership-old','ownership-old','ownership-missing','待建专辑','daijianzhuanji' FROM songs WHERE normalized_title='ownership-legacy-song'");
    execute(previous, "INSERT INTO album_song_refs(song_source_ref_id,source,source_album_id) SELECT id,'migu','ownership-extra' FROM song_source_refs WHERE source_content_id='ownership-old'");
    execute(previous, "INSERT INTO song_resources(song_source_ref_id,resource_kind,format_type,status,local_path,cloud_backend,cloud_path,remote_url) SELECT id,'audio','SQ',2,'migu/songs/old.flac','test','old.flac','https://example.test/expired' FROM song_source_refs WHERE source_content_id='ownership-old'");
    previous->cancel();
    previous.reset();
    auto pool = std::make_shared<mysql::MySqlConnectionPool>(config);
    require(support::runOnExecutor(pool->executor(), pool->coInitializeSchema(), "ownership migration"), "ownership migration failed");
    require(scalar(pool, "SELECT COUNT(*) FROM album_source_refs WHERE source_album_id='ownership-missing' AND source_title_pinyin='daijianzhuanji'") == 1, "migration lost album pinyin");
    require(scalar(pool, "SELECT COUNT(*) FROM album_song_refs r JOIN album_source_refs a ON a.id=r.album_source_ref_id WHERE a.source_album_id='ownership-extra'") == 1, "migration lost relation-only album");
    require(scalar(pool, "SELECT COUNT(*) FROM song_resources WHERE status=2 AND local_path='migu/songs/old.flac' AND cloud_path='old.flac'") == 1, "migration changed completed resources");
    require(scalar(pool, "SELECT COUNT(*) FROM album_artist_refs") == 1, "migration lost album artist");
    require(scalar(pool, "SELECT COUNT(DISTINCT album_db_id) FROM album_source_refs WHERE source_album_id IN('ownership-old','ownership-alias')") == 2, "old name deduplication still shares identities across different source IDs");
    // MySQL DDL 不能整体回滚，重复执行验证中断后可以重跑。
    require(support::runOnExecutor(pool->executor(), repeatMigration(pool), "repeat ownership migration"), "migration repeat failed");
    require(scalar(pool, "SELECT COUNT(*) FROM information_schema.columns WHERE table_schema=DATABASE() AND ((table_name IN('song_source_refs','singer_source_refs','album_source_refs') AND column_name='source_item_key') OR (table_name='album_source_refs' AND column_name='source_title'))") == 0, "derived source fields remain stored");
    require(scalar(pool, "SELECT COUNT(*) FROM information_schema.table_constraints WHERE constraint_schema=DATABASE() AND table_name='song_source_refs' AND constraint_name='chk_song_source_identity' AND constraint_type='CHECK'") == 1, "song identity check is missing");
    require(scalar(pool, "SELECT COUNT(*) FROM information_schema.statistics WHERE table_schema=DATABASE() AND table_name='song_source_refs' AND index_name IN('uk_source_song','uk_source_content') AND non_unique=0 AND seq_in_index=2") == 2, "song or content identity uniqueness was lost");
    require(scalar(pool, "SELECT COUNT(*) FROM information_schema.statistics WHERE table_schema=DATABASE() AND ((table_name='song_source_refs' AND index_name='uk_source_item') OR (table_name='singer_source_refs' AND index_name='uk_singer_source_item') OR (table_name='album_source_refs' AND index_name='uk_album_source_item'))") == 0, "old business-key indexes must not survive as source-only unique indexes");
    require(scalar(pool, "SELECT COUNT(*) FROM information_schema.columns WHERE table_schema=DATABASE() AND table_name='albums' AND column_name='identity_hash' AND column_comment LIKE '%source_album_id%' AND column_comment NOT LIKE '%primary_artist_name%'") == 1, "album identity comment is stale");
    return pool;
}

// 所有运行期模板在新 schema 下 prepare，定位未随结构更新的查询列。
boost::asio::awaitable<int> prepareRuntimeSql(const std::shared_ptr<mysql::MySqlConnectionPool> &pool)
{
    auto connection = co_await pool->coAcquire();
    int count = 0;
    std::string failures;
    for (const auto &entry : std::filesystem::recursive_directory_iterator("sql/mysql")) {
        const auto path = entry.path();
        if (path.extension() != ".sql" || path.parent_path().filename() == "migrations" || path.filename() == "create.sql" || path.filename() == "catalog_view.sql" || path.parent_path().filename() == "transactions") continue;
        try {
            for (const auto &statement : sql::SqlFileLoader::splitStatements(sql::SqlFileLoader::loadOrThrow(path.string()))) {
                auto prepared = co_await connection->async_prepare_statement(statement, boost::asio::use_awaitable);
                co_await connection->async_close_statement(prepared, boost::asio::use_awaitable);
                ++count;
            }
        } catch (const std::exception &error) {
            failures += path.string() + ": " + error.what() + "\n";
        }
    }
    if (!failures.empty()) throw std::runtime_error(failures);
    co_return count;
}

int main(int argc, char *argv[])
{
    try {
        auto config = support::makeMySqlConfigFromEnv();
        require(config.database.starts_with("crawler_ownership_test_"), "set CRAWLER_MYSQL_DATABASE to a dedicated crawler_ownership_test_* database");
        if (argc == 2 && std::string(argv[1]) == "--check-sql-only") {
            auto pool = support::makeInitializedPool();
            const auto prepared = support::runOnExecutor(pool->executor(), prepareRuntimeSql(pool), "prepare runtime SQL");
            pool->cancel();
            std::cout << "PASS: prepared " << prepared << " runtime SQL templates\n";
            return 0;
        }
        auto pool = migrateFixture(config);
        auto writer = std::make_shared<mysql::MySqlMetadataBatchWriter>(pool);
        auto client = std::make_shared<http::MockHttpClient>();
        http::HttpResponse image;
        image.status_code = 200;
        image.content_type = "image/png";
        const std::string bytes = "\x89PNG\r\n\x1a\nfixture-cover";
        image.body.assign(bytes.begin(), bytes.end());
        client->mock_responses.emplace("https://example.test/cover.png", image);
        const auto directory = std::filesystem::temp_directory_path() / config.database;
        auto files = std::make_shared<storage::FileStorage>(directory.string());
        auto covers = std::make_shared<storage::MetadataCoverStore>(client, files, std::vector<std::string> {});
        cases::MetadataPersistenceService service(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, {writer, covers, {}});
        model::SongSourceMetadata song;
        song.source = "migu";
        song.source_song_id = "ownership-song";
        song.source_content_id = "ownership-content";
        song.source_resource_type = "2";
        song.title = "完整首次写入";
        song.source_album_id = "ownership-album";
        song.source_album_name = "首次专辑";
        song.source_album_pinyin = "shoucizhuanji";
        song.cover_url = "https://example.test/cover.png";
        song.artist_refs = {{"ownership-singer", "首次歌手", "shoucigeshou", "artist", 1, true, true}};
        song.audio_formats = {{"SQ", "E", "011003", "011002", 40561011, 39768162}};
        require(service.saveSongSources({song}, 1), "first song ingestion failed");
        require(scalar(pool, "SELECT COUNT(*) FROM album_song_refs r JOIN album_source_refs a ON a.id=r.album_source_ref_id JOIN song_source_refs s ON s.id=r.song_source_ref_id WHERE a.source_album_id='ownership-album' AND s.source_content_id='ownership-content'") == 1, "first write lacks album relationship");
        require(scalar(pool, "SELECT COUNT(*) FROM song_artist_refs r JOIN singer_source_refs s ON s.id=r.singer_source_ref_id WHERE s.source_singer_id='ownership-singer' AND s.source_name_pinyin='shoucigeshou' AND r.is_seed=1") == 1, "first write lacks singer or pinyin");
        auto resource = execute(pool, "SELECT local_path,size_bytes,checksum FROM album_resources WHERE status=2");
        require(resource.rows().size() == 1, "album cover was not saved");
        const auto local_path = mysql::MySqlValueReader::stringAt(resource.rows().front(), 0);
        require(files->exists(local_path), "completed cover file is missing");
        require(mysql::MySqlValueReader::uint64At(resource.rows().front(), 1) == bytes.size(), "cover size differs from complete file");
        require(!mysql::MySqlValueReader::stringAt(resource.rows().front(), 2).empty(), "cover checksum is empty");
        mysql::MySqlSongResourceQueue queue(pool);
        model::SongResourceClaimQuery claim;
        claim.source_content_id = "ownership-content";
        claim.resource_kind = "audio";
        claim.format_type = "SQ";
        claim.limit = 1;
        auto pending = queue.claimPending(claim);
        require(pending.size() == 1 && pending[0].remote_url.empty(), "queue depended on a persisted remote URL");
        model::SongResourceFailureUpdate failed;
        failed.resource_id = pending[0].resource_id;
        failed.error_category = "network";
        failed.result_code = "http_status_404";
        failed.message = "fixture failure";
        require(queue.markFailed(failed), "URL-free failure update failed");
        model::SongResourceRelatedClaimRequest related;
        related.song_source_ref_id = pending[0].song_source_ref_id;
        related.resource_kind = "cover";
        related.remote_url = "https://example.test/fresh-cover";
        auto related_target = queue.claimRelated(related);
        require(related_target && related_target->remote_url == related.remote_url, "related claim lost the current call URL");
        model::SongResourceSuccessUpdate succeeded;
        succeeded.resource_id = related_target->resource_id;
        succeeded.local_path = "migu/songs/ownership-content/cover.png";
        succeeded.size_bytes = bytes.size();
        require(files->save(succeeded.local_path, image.body), "song cover fixture could not be saved");
        succeeded.checksum = mysql::MySqlValueReader::stringAt(resource.rows().front(), 2);
        require(queue.markSucceeded(succeeded), "URL-free success update failed");
        require(service.saveSongSources({song}, 1), "idempotent ingestion failed");
        require(!files->exists(local_path), "successful recollection left the replaced album cover");
        require(scalar(pool, "SELECT COUNT(*) FROM song_source_refs WHERE source_content_id='ownership-content'") == 1, "song ingestion duplicated source");
        require(scalar(pool, "SELECT COUNT(*) FROM album_source_refs WHERE source_album_id='ownership-album'") == 1, "song ingestion duplicated album");
        model::AlbumSourceMetadata album;
        album.source = "migu";
        album.source_album_id = "ownership-album";
        album.source_material_id = "ownership-material";
        album.title = "名称修正";
        album.artist_refs = song.artist_refs;
        album.detail_sync_status = 2;
        require(service.saveAlbumDetails({album}, {song}), "album detail ingestion failed");
        require(scalar(pool, "SELECT COUNT(*) FROM albums WHERE identity_hash=SHA2('migu|ownership-album',256)") == 1, "detail altered stable identity");
        require(scalar(pool, "SELECT COUNT(*) FROM album_artist_refs r JOIN album_source_refs a ON a.id=r.album_source_ref_id WHERE a.source_album_id='ownership-album'") == 1, "album artists missing");
        model::SingerSourceMetadata singer;
        singer.source = "migu";
        singer.source_singer_id = "ownership-singer";
        singer.name = "首次歌手";
        singer.avatar_url = "https://example.test/cover.png";
        singer.source_index_tab = "a";
        auto singer2 = singer;
        singer2.source_index_tab = "b";
        require(service.saveSingerSources({singer, singer2}, 2), "singer ingestion failed");
        require(scalar(pool, "SELECT COUNT(*) FROM singer_resources WHERE status=2") == 1, "singer cover missing");
        require(scalar(pool, "SELECT COUNT(*) FROM singer_index_memberships") == 2, "multiple index memberships were lost");
        auto broken = song;
        broken.source_content_id = "ownership-rollback";
        broken.title = "应回滚";
        broken.artist_refs = {{"ownership-unknown", "", "", "artist", 1, true, false}};
        require(!service.saveSongSources({broken}, 1), "missing endpoint did not fail");
        require(scalar(pool, "SELECT COUNT(*) FROM song_source_refs WHERE source_content_id='ownership-rollback'") == 0, "failed batch partially committed");
        // 删除派生业务键后，单 ID 写入及后续补齐仍复用同一来源记录。
        model::SongSourceMetadata single_id;
        single_id.source = "migu";
        single_id.source_song_id = "ownership-song-only";
        single_id.title = "单身份歌曲";
        require(service.saveSongSources({single_id}, 1), "song-only identity ingestion failed");
        single_id.source_content_id = "ownership-now-content";
        require(service.saveSongSources({single_id}, 1), "identity completion ingestion failed");
        require(scalar(pool, "SELECT COUNT(*) FROM song_catalog_sources WHERE source_song_id='ownership-song-only' AND source_item_key='content:ownership-now-content'") == 1, "identity completion duplicated a source or left a stale derived key");
        single_id.source_song_id.clear();
        single_id.source_content_id = "ownership-content-only";
        require(service.saveSongSources({single_id}, 1), "content-only identity ingestion failed");
        single_id.source_content_id.clear();
        require(!service.saveSongSources({single_id}, 1), "missing song identity was accepted by ingestion");
        for (const auto *identity_values : {"NULL,NULL", "'',''", "' ',NULL"}) {
            bool rejected = false;
            try {
                execute(pool, "INSERT INTO song_source_refs(song_db_id,source,source_song_id,source_content_id) SELECT id,'migu'," + std::string(identity_values) + " FROM songs ORDER BY id LIMIT 1");
            } catch (const std::exception &) {
                rejected = true;
            }
            require(rejected, "database accepted an empty song identity");
        }
        require(scalar(pool, "SELECT COUNT(*) FROM information_schema.columns WHERE table_schema=DATABASE() AND ((table_name IN('song_source_refs','song_resources','album_resources','singer_resources') AND column_name IN('remote_url','cover_url','lyric_url','mrc_url','trc_url')) OR (table_name='song_source_refs' AND column_name LIKE 'source_album%') OR (table_name='album_song_refs' AND column_name IN('source','source_song_id','source_album_id','source_content_id')))") == 0, "removed columns remain");
        const auto prepared = support::runOnExecutor(pool->executor(), prepareRuntimeSql(pool), "prepare runtime SQL");
        pool->cancel();
        std::cout << "PASS: ownership migration, restart, atomic ingest, local covers, idempotency, rollback; prepared " << prepared << " SQL templates\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
