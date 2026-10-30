#include "crawler/application/use_cases/MetadataBatchBuilder.h"
#include "crawler/application/use_cases/MetadataPersistenceService.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMetadataBatchWriter.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMusicCatalogQueryRepository.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlSongResourceQueue.h"
#include "crawler/infrastructure/storage/filesystem/FileStorage.h"
#include "migu/support/MySqlTestSupport.h"
#include "migu/support/MetadataTestDiagnostics.h"
#include "mocks/MockHttpClient.h"
#include "mocks/MockMusicProvider.h"
#include "crawler/application/use_cases/SongResourceDownloadWorkflow.h"
#include "crawler/infrastructure/music/migu/parser/MiguSongResourceParser.h"
#include "crawler/infrastructure/music/migu/parser/MiguParser.h"
#include "crawler/infrastructure/http/client/HttpSongResourceContentFetcher.h"

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
    for (const auto *path : {"sql/mysql/migrations/V2026102601__source_identity_and_album_count_semantics.sql", "sql/mysql/migrations/V2026102602__resource_claim_generation.sql", "sql/mysql/migrations/V2026102703__metadata_cover_remote_urls.sql"}) {
        for (const auto &statement : sql::SqlFileLoader::splitStatements(sql::SqlFileLoader::loadOrThrow(path))) {
            co_await connection->async_execute(statement, result, boost::asio::use_awaitable);
        }
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
    require(scalar(pool, "SELECT COUNT(*) FROM song_resources r JOIN resource_cloud_migration_audit a ON a.owner_type='song' AND a.resource_id=r.id AND a.run_id='schema-retirement-v2026102913' AND a.disposition='archived' WHERE r.status=2 AND r.local_path='migu/songs/old.flac' AND JSON_UNQUOTE(JSON_EXTRACT(a.old_values,'$.cloud_path'))='old.flac'") == 1, "migration changed completed resources");
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
            auto text = sql::SqlFileLoader::loadOrThrow(path.string());
            // 分句器会去掉块注释；先替换表达式占位符，避免CAST空表达式。
            for (const std::string token : {"/*__CLOUD_CURRENT__*/", "/*__CLOUD_STATUS__*/"}) {
                for (auto offset = text.find(token); offset != std::string::npos; offset = text.find(token)) text.replace(offset, token.size(), "0");
            }
            for (auto statement : sql::SqlFileLoader::splitStatements(text)) {
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
        auto resource_queue = std::make_shared<mysql::MySqlSongResourceQueue>(pool);
        auto content_fetcher = std::make_shared<http::HttpSongResourceContentFetcher>(client);
        auto provider = std::make_shared<crawler::tests::MockMusicProvider>();
        http::HttpResponse audio;
        audio.status_code = 200;
        audio.content_type = "audio/flac";
        audio.body = {'f', 'L', 'a', 'C', 'f', 'i', 'x', 't', 'u', 'r', 'e'};
        client->mock_responses.emplace("https://example.test/audio.flac", audio);
        http::HttpResponse text;
        text.status_code = 200;
        text.content_type = "text/plain";
        text.body = {'f', 'i', 'x', 't', 'u', 'r', 'e'};
        for (const auto *kind : {"lyric", "mrc", "trc"}) client->mock_responses.emplace(std::string("https://example.test/") + kind, text);
        model::ProviderResponse listen;
        listen.status_code = 200;
        const std::string payload = R"({"code":"000000","data":{"url":"https://example.test/audio.flac","coverUrl":"https://example.test/cover.png","lrcUrl":"https://example.test/lyric","mrcUrl":"https://example.test/mrc","trcUrl":"https://example.test/trc"}})";
        listen.body.assign(payload.begin(), payload.end());
        provider->mock_song_resource_response = listen;
        auto resource_parser = std::make_shared<crawler::infrastructure::music::migu::parser::MiguSongResourceParser>();
        auto resources = std::make_shared<cases::SongResourceDownloadWorkflow>(resource_queue, provider, resource_parser, content_fetcher, files);
        cases::MetadataPersistenceService service(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, {writer, std::make_shared<support::MetadataTestDiagnostics>()});
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
        song.source_album_cover_url = "https://example.test/album-only.png";
        song.copyright_id = "fixture-copyright";
        song.lyric_url = "https://example.test/lyric";
        song.mrc_url = "https://example.test/mrc";
        song.trc_url = "https://example.test/trc";
        song.artist_refs = {{"ownership-singer", "首次歌手", "shoucigeshou", "artist", 1, true, true}};
        song.audio_formats = {{"SQ", "E", "011003", "011002", 40561011, 39768162}};
        require(service.saveSongSources({song}, 1), "first song ingestion failed");
        require(scalar(pool, "SELECT COUNT(*) FROM album_song_refs r JOIN album_source_refs a ON a.id=r.album_source_ref_id JOIN song_source_refs s ON s.id=r.song_source_ref_id WHERE a.source_album_id='ownership-album' AND s.source_content_id='ownership-content'") == 1, "first write lacks album relationship");
        require(scalar(pool, "SELECT COUNT(*) FROM song_artist_refs r JOIN singer_source_refs s ON s.id=r.singer_source_ref_id WHERE s.source_singer_id='ownership-singer' AND s.source_name_pinyin='shoucigeshou' AND r.is_seed=1") == 1, "first write lacks singer or pinyin");
        require(client->last_request_url.empty(), "metadata ingestion unexpectedly downloaded a binary resource");
        require(scalar(pool, "SELECT COUNT(*) FROM album_resources r JOIN album_source_refs a ON a.id=r.album_source_ref_id WHERE a.source_album_id='ownership-album' AND r.status=0 AND r.remote_url='https://example.test/album-only.png' AND r.local_path IS NULL") == 1, "album URL was not queued without downloading");
        require(scalar(pool, "SELECT COUNT(*) FROM song_resources sr JOIN song_source_refs r ON r.id=sr.song_source_ref_id WHERE r.source_content_id='ownership-content' AND sr.resource_kind IN('cover','lyric','mrc','trc') AND sr.status=0") == 4, "song resources were downloaded during metadata ingestion or task inputs were removed");
        auto &queue = *resource_queue;
        model::SongResourceClaimQuery claim;
        claim.source_content_id = "ownership-content";
        claim.resource_kind = "audio";
        claim.format_type = "SQ";
        claim.limit = 1;
        auto pending = queue.claimPending(claim);
        require(pending.size() == 1 && pending[0].remote_url.empty(), "queue depended on a persisted remote URL");
        model::SongResourceFailureUpdate failed;
        failed.resource_id = pending[0].resource_id;
        failed.claim_generation = pending[0].claim_generation;
        failed.error_category = "network";
        failed.result_code = "http_status_404";
        failed.message = "fixture failure";
        // 元数据重采不能解除 worker 的认领或重置错误/永久跳过。
        require(service.saveSongSources({song}, 1), "recollect during resource run failed");
        require(scalar(pool, "SELECT status FROM song_resources WHERE id=" + std::to_string(failed.resource_id)) == 1, "metadata recollection reset a running resource");
        require(queue.markFailed(failed), "URL-free failure update failed");
        auto reclaimed = queue.claimPending(claim);
        require(reclaimed.size() == 1 && reclaimed[0].claim_generation > failed.claim_generation, "reclaim did not advance generation");
        require(!queue.markFailed(failed), "expired worker overwrote a new claim");
        model::SongResourceSuccessUpdate expired;
        expired.resource_id = failed.resource_id;
        expired.claim_generation = failed.claim_generation;
        expired.local_path = "expired-worker.flac";
        require(!queue.markSucceeded(expired), "expired success overwrote a new claim");
        model::SongResourceSkipUpdate expired_skip;
        expired_skip.resource_id = failed.resource_id;
        expired_skip.song_source_ref_id = pending[0].song_source_ref_id;
        expired_skip.claim_generation = failed.claim_generation;
        require(!queue.markSkipped(expired_skip), "expired skip changed a new claim");
        failed.claim_generation = reclaimed[0].claim_generation;
        require(queue.markFailed(failed), "current owner could not finish a failure");

        auto related_song = song;
        related_song.source_content_id = "ownership-related-content";
        related_song.source_song_id = "ownership-related-song";
        related_song.title = "关联任务 fixture";
        related_song.cover_url.clear();
        related_song.lyric_url.clear();
        related_song.mrc_url.clear();
        related_song.trc_url.clear();
        require(service.saveSongSources({related_song}, 1), "related owner fixture failed");
        model::SongResourceRelatedClaimRequest related;
        related.song_source_ref_id = scalar(pool, "SELECT id FROM song_source_refs WHERE source_content_id='ownership-related-content'");
        related.resource_kind = "cover";
        related.remote_url = "https://example.test/fresh-cover";
        auto related_target = queue.claimRelated(related);
        require(related_target && related_target->remote_url == related.remote_url, "related claim lost the current call URL");
        model::SongResourceSuccessUpdate succeeded;
        succeeded.resource_id = related_target->resource_id;
        succeeded.claim_generation = related_target->claim_generation;
        succeeded.local_path = "migu/songs/ownership-related-content/cover/manual.png";
        succeeded.size_bytes = bytes.size();
        require(files->save(succeeded.local_path, image.body), "song cover fixture could not be saved");
        succeeded.checksum = "fixture-checksum";
        require(queue.markSucceeded(succeeded), "URL-free success update failed");
        claim.options.auto_refresh_pacmtoken = false;
        const auto audio_result = resources->processPending(claim);
        require(audio_result.succeeded_count == 1 && files->exists(audio_result.items.back().local_path), "audio did not complete the provider-to-database/file pipeline");
        require(scalar(pool, "SELECT COUNT(*) FROM song_resources sr JOIN song_source_refs r ON r.id=sr.song_source_ref_id WHERE r.source_content_id='ownership-content' AND sr.resource_kind<>'audio' AND sr.claim_generation=1 AND sr.status=2") == 4, "audio redownloaded previously saved related resources");
        song.audio_formats.push_back({"HQ", "2", "020010", "020010", 100, 100});
        require(service.saveSongSources({song}, 1), "new quality metadata failed");
        claim.format_type = "HQ";
        require(resources->processPending(claim).succeeded_count == 1, "second audio quality failed");
        require(scalar(pool, "SELECT COUNT(*) FROM song_resources sr JOIN song_source_refs r ON r.id=sr.song_source_ref_id WHERE r.source_content_id='ownership-content' AND sr.resource_kind<>'audio' AND sr.claim_generation=1 AND sr.status=2") == 4, "second quality redownloaded related resources");
        require(resources->processPending(claim).claimed_count == 0, "successful resource was claimed again");
        // 多音质并发限制共享整首歌的永久跳过语义，第二个 worker 不能误报失败。
        auto skipped_song = related_song;
        skipped_song.source_content_id = "ownership-skip-content";
        skipped_song.source_song_id = "ownership-skip-song";
        skipped_song.audio_formats.push_back({"HQ", "2", "020010", "020010", 100, 100});
        require(service.saveSongSources({skipped_song}, 1), "skip owner fixture failed");
        model::SongResourceClaimQuery skip_claim;
        skip_claim.source_content_id = skipped_song.source_content_id;
        skip_claim.resource_kind = "audio";
        skip_claim.limit = 2;
        const auto skip_targets = queue.claimPending(skip_claim);
        require(!skip_targets.empty(), "skip resources not claimable");
        for (const auto &target : skip_targets) {
            model::SongResourceSkipUpdate update;
            update.resource_id = target.resource_id;
            update.song_source_ref_id = target.song_source_ref_id;
            update.claim_generation = target.claim_generation;
            update.result_code = "440000";
            update.error_category = "copyright";
            require(queue.markSkipped(update), "same permanent skip was rejected by a concurrent worker");
        }
        require(service.saveSongSources({skipped_song}, 1), "skip recollection failed");
        require(queue.claimPending(skip_claim).empty(), "metadata refresh reset a permanent skip");

        require(service.saveSongSources({song}, 1), "idempotent ingestion failed");
        require(scalar(pool, "SELECT COUNT(*) FROM album_resources r JOIN album_source_refs a ON a.id=r.album_source_ref_id WHERE a.source_album_id='ownership-album' AND r.remote_url='https://example.test/album-only.png' AND r.status=0 AND r.local_path IS NULL") == 1, "metadata recollection unexpectedly downloaded an album cover");
        require(scalar(pool, "SELECT COUNT(*) FROM song_source_refs WHERE source_content_id='ownership-content'") == 1, "song ingestion duplicated source");
        require(scalar(pool, "SELECT COUNT(*) FROM album_source_refs WHERE source_album_id='ownership-album'") == 1, "song ingestion duplicated album");
        model::AlbumSourceMetadata album;
        album.source = "migu";
        album.source_album_id = "ownership-album";
        album.source_material_id = "ownership-material";
        album.title = "名称修正";
        album.artist_refs = song.artist_refs;
        album.detail_sync_status = 2;
        album.source_title_pinyin = "xiangxizhuanji";
        album.source_resource_type = "5";
        album.source_album_kind = 2;
        require(service.saveAlbumDetails({album}, {song}), "album detail ingestion failed");
        mysql::MySqlMusicCatalogQueryRepository catalog(pool);
        model::MiguBackfillTargetQuery reset;
        reset.source_ids = {album.source_album_id};
        reset.statuses = {2};
        require(catalog.resetAlbumDetails(reset) == 1, "detail task reset failed");
        require(scalar(pool, "SELECT detail_synced_at FROM album_source_refs WHERE source_album_id='ownership-album'") > 0, "task reset erased previous successful metadata provenance");
        auto sparse_album = album;
        sparse_album.detail_sync_status = 0;
        sparse_album.source_title_pinyin = "liebiaopinyin";
        sparse_album.source_resource_type = "2003";
        sparse_album.source_album_kind = 1;
        require(service.saveAlbumSources({sparse_album}, 1) && service.saveSongSources({song}, 1), "sparse recollection failed");
        require(scalar(pool, "SELECT COUNT(*) FROM album_source_refs WHERE source_album_id='ownership-album' AND source_title_pinyin='xiangxizhuanji' AND source_resource_type='5' AND source_album_kind=2") == 1, "sparse recollection downgraded authoritative album metadata");

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
        require(scalar(pool, "SELECT COUNT(*) FROM singer_resources WHERE status=0 AND remote_url='https://example.test/cover.png' AND local_path IS NULL") == 1, "singer URL missing or downloaded inline");
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
        require(scalar(pool, "SELECT COUNT(*) FROM information_schema.columns WHERE table_schema=DATABASE() AND ((table_name IN('song_source_refs','song_resources') AND column_name IN('remote_url','cover_url','lyric_url','mrc_url','trc_url')) OR (table_name='song_source_refs' AND column_name LIKE 'source_album%') OR (table_name='album_song_refs' AND column_name IN('source','source_song_id','source_album_id','source_content_id')))") == 0, "removed columns remain");
        // 歌曲先入库再采集专辑目录时，数字身份、拼音、物料映射和两类关系必须汇合。
        crawler::infrastructure::music::migu::parser::MiguParser metadata_parser;
        const std::string digital_song_raw = R"({"data":{"contents":[{"contents":[{"songItem":{"contentId":"ownership-digital-song","songName":"数字身份歌曲","album":"数字身份专辑","albumPinyin":"shuzizhuanji","albumId":"ownership-material-id","dalbumId":"ownership-digital-id"}}]}]}})";
        const auto digital_songs = metadata_parser.parseSongSources({digital_song_raw.begin(), digital_song_raw.end()});
        require(digital_songs.size() == 1 && service.saveSongSources(digital_songs, 1), "digital song ingestion failed");
        const std::string digital_album_raw = R"({"data":{"contents":[{"view":"ZJ-Album-Item","resId":"ownership-digital-id","resType":"1","txt":"数字身份专辑"}]}})";
        auto digital_albums = metadata_parser.parseAlbumSources({digital_album_raw.begin(), digital_album_raw.end()});
        require(digital_albums.size() == 1, "digital album list fixture did not parse");
        digital_albums[0].source_artist_id = "ownership-singer";
        digital_albums[0].source_artist_name = "首次歌手";
        require(service.saveAlbumSources(digital_albums, 1) && service.saveSongSources(digital_songs, 1), "digital album recollection failed");
        require(scalar(pool, "SELECT COUNT(*) FROM album_source_refs WHERE source_album_id IN('ownership-digital-id','ownership-material-id')") == 1, "digital album was duplicated by material identity");
        require(scalar(pool, "SELECT COUNT(*) FROM album_source_refs WHERE source_album_id='ownership-digital-id' AND source_title_pinyin='shuzizhuanji' AND source_album_kind=2 AND source_resource_type='5'") == 1, "digital album fields did not merge into the canonical source");
        require(scalar(pool, "SELECT COUNT(*) FROM migu_dalbum_details d JOIN album_source_refs a ON a.id=d.album_source_ref_id WHERE a.source_album_id='ownership-digital-id' AND d.source_material_id='ownership-material-id'") == 1, "digital album material mapping was lost");
        require(scalar(pool, "SELECT COUNT(*) FROM album_song_refs r JOIN album_source_refs a ON a.id=r.album_source_ref_id JOIN song_source_refs s ON s.id=r.song_source_ref_id WHERE a.source_album_id='ownership-digital-id' AND s.source_content_id='ownership-digital-song'") == 1, "digital song relation did not use the canonical album");
        require(scalar(pool, "SELECT COUNT(*) FROM album_artist_refs r JOIN album_source_refs a ON a.id=r.album_source_ref_id WHERE a.source_album_id='ownership-digital-id'") == 1, "digital album artist relation was lost");
        const auto prepared = support::runOnExecutor(pool->executor(), prepareRuntimeSql(pool), "prepare runtime SQL");
        pool->cancel();
        std::cout << "PASS: ownership migration, restart, atomic ingest, stored cover URLs, explicit song downloads, idempotency, rollback; prepared " << prepared << " SQL templates\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
