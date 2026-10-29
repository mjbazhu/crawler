#include "crawler/application/use_cases/MetadataCoverDownloadWorkflow.h"
#include "crawler/infrastructure/http/client/AsioHttpClient.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMetadataBatchWriter.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMetadataCoverQueue.h"
#include "crawler/infrastructure/platform/Environment.h"
#include "crawler/infrastructure/storage/filesystem/MetadataCoverStore.h"
#include "migu/support/MetadataCoverMySqlTestSupport.h"

#include <iostream>
#include <stdexcept>

namespace c = orchestrator::modules::crawler;
namespace model = c::application::model;
namespace support = c::tests::migu::support;
namespace mysql = c::infrastructure::music::persistence::mysql;
namespace platform = c::infrastructure::platform;
namespace storage = c::infrastructure::storage::filesystem;

namespace {
// 只读识别源库 URL 归属，允许从尚未升级的旧来源表取得既有图片样本。
boost::asio::awaitable<bool> sourceHasColumn(std::shared_ptr<mysql::MySqlConnectionPool> pool, std::string database, std::string table, std::string column)
{
    auto connection = co_await pool->coAcquire();
    auto statement = co_await connection->async_prepare_statement("SELECT 1 FROM information_schema.columns WHERE table_schema=? AND table_name=? AND column_name=?", boost::asio::use_awaitable);
    boost::mysql::results result;
    co_await connection->async_execute(statement.bind(database, table, column), result, boost::asio::use_awaitable);
    co_return !result.rows().empty();
}
} // namespace

// 手动真实图片探测，不注册到 CTest；源库只读，写入和文件均使用独立测试位置。
int main()
{
    std::string stage = "configuration";
    try {
        if (!platform::Environment::getBoolOr("CRAWLER_TEST_COVER_LIVE", false)) {
            std::cout << "SKIP: live cover probe requires explicit enablement\n";
            return 77;
        }
        const auto config = support::makeMySqlConfigFromEnv();
        const auto source_db = platform::Environment::getStringOr("CRAWLER_TEST_COVER_SOURCE_DATABASE", "");
        const auto root = platform::Environment::getStringOr("CRAWLER_TEST_COVER_STORAGE_ROOT", "");
        if (!support::safeCoverDatabaseName(source_db) || source_db == config.database || root.empty()) throw std::invalid_argument("live cover probe requires a separate read-only source database and storage root");
        stage = "initialize_test_database";
        auto pool = support::makeCoverTestPool();
        stage = "read_source_urls";
        const auto source = '`' + source_db + '`' + '.';
        // 只复用来源库中已有的公开平台图片 URL，不补查平台元数据。
        const auto column = [&](std::string table, std::string name) { return support::runOnExecutor(pool->executor(), sourceHasColumn(pool, source_db, std::move(table), std::move(name)), "inspect read-only cover source"); };
        const bool singer_resources = column("singer_resources", "remote_url");
        const bool album_resources = column("album_resources", "remote_url");
        if ((!singer_resources && !column("singer_source_refs", "avatar_url")) || (!album_resources && !column("album_source_refs", "cover_url"))) {
            std::cout << "SKIP: source database has no persisted singer/album cover URL columns\n";
            return 77;
        }
        const std::string singer_url = singer_resources ? "r.remote_url" : "s.avatar_url";
        const std::string album_url = album_resources ? "r.remote_url" : "s.cover_url";
        const auto singers = support::runOnExecutor(pool->executor(), pool->coExecute("SELECT s.source_singer_id,n.name," + singer_url + " FROM " + source + "singer_source_refs s JOIN " + source + "singers n ON n.id=s.singer_db_id " + (singer_resources ? "JOIN " + source + "singer_resources r ON r.singer_source_ref_id=s.id " : "") + "WHERE s.source='migu' AND " + singer_url + " LIKE 'http%' AND " + singer_url + " NOT LIKE '%example.test%' ORDER BY s.id LIMIT 2"), "read singer cover samples");
        const auto albums = support::runOnExecutor(pool->executor(), pool->coExecute("SELECT s.source_album_id,n.title," + album_url + " FROM " + source + "album_source_refs s JOIN " + source + "albums n ON n.id=s.album_db_id " + (album_resources ? "JOIN " + source + "album_resources r ON r.album_source_ref_id=s.id " : "") + "WHERE s.source='migu' AND " + album_url + " LIKE 'http%' AND " + album_url + " NOT LIKE '%example.test%' ORDER BY s.id LIMIT 2"), "read album cover samples");
        if (singers.rows().empty() || albums.rows().empty()) {
            std::cout << "SKIP: no stored live singer/album cover samples\n";
            pool->cancel();
            return 77;
        }
        model::MetadataWriteBatch input;
        for (auto row : singers.rows()) {
            model::SingerSourceMetadata singer;
            singer.source = "migu";
            singer.source_singer_id = mysql::MySqlValueReader::stringAt(row, 0);
            singer.name = mysql::MySqlValueReader::stringAt(row, 1);
            singer.avatar_url = mysql::MySqlValueReader::stringAt(row, 2);
            input.singers.push_back(std::move(singer));
        }
        for (auto row : albums.rows()) {
            model::AlbumSourceMetadata album;
            album.source_album_id = mysql::MySqlValueReader::stringAt(row, 0);
            album.title = mysql::MySqlValueReader::stringAt(row, 1);
            album.cover_url = mysql::MySqlValueReader::stringAt(row, 2);
            input.albums.push_back(std::move(album));
        }
        stage = "persist_samples";
        mysql::MySqlMetadataBatchWriter writer(pool);
        if (!writer.saveBatch(input).isSuccess()) throw std::runtime_error("live cover fixture write failed");
        auto queue = std::make_shared<mysql::MySqlMetadataCoverQueue>(pool);
        c::infrastructure::http::client::AsioHttpClient::Options http_options;
        http_options.verify_tls = true;
        auto http = std::make_shared<c::infrastructure::http::client::AsioHttpClient>(http_options);
        auto files = std::make_shared<storage::FileStorage>(root);
        auto store = std::make_shared<storage::MetadataCoverStore>(http, files, std::vector<std::string> {"User-Agent: Crawler/1.0", "Accept: image/*"});
        c::application::use_cases::MetadataCoverDownloadWorkflow workflow(queue, store, store);
        model::MetadataCoverOptions options;
        stage = "download_images";
        int verified = 0;
        for (auto owner : {model::CoverOwner::Singer, model::CoverOwner::Album}) {
            model::MetadataCoverQuery query;
            query.owner = owner;
            const auto page = queue->list(query);
            if (!page.isSuccess()) throw std::runtime_error("live cover queue read failed");
            for (const auto &target : page.data()->items) {
                const auto item = workflow.process(target, options);
                if (!item.success || !store->read(item.record.file)) {
                    std::cerr << "FAIL owner=" << model::coverOwnerName(owner) << " resource_id=" << target.resource_id << " code=" << item.record.file.result_code << '\n';
                    return 1;
                }
                const auto again = workflow.process(item.record, options);
                if (!again.success || !again.skipped) throw std::runtime_error("live cover second pass did not reuse the committed file");
                ++verified;
                std::cout << "VERIFIED owner=" << model::coverOwnerName(owner) << " resource_id=" << target.resource_id << " size=" << item.record.file.size_bytes << " checksum=" << item.record.file.checksum << " second_pass=skipped\n";
            }
        }
        pool->cancel();
        std::cout << "PASS: " << verified << " live local cover files verified with TLS enabled; source database remained read-only\n";
        return 0;
    } catch (const boost::system::system_error &error) {
        std::cerr << "FAIL: live cover stage=" << stage << " database_code=" << error.code().value() << '\n';
        return 1;
    } catch (const std::exception &) {
        std::cerr << "FAIL: live cover stage=" << stage << " could not complete; check isolated configuration and platform access\n";
        return 1;
    }
}
