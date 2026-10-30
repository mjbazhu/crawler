#include "crawler/api/controllers/CrawlerController.h"
#include "crawler/application/use_cases/MetadataBatchBuilder.h"
#include "crawler/application/use_cases/CrawlerServiceDependencies.h"
#include "crawler/infrastructure/music/migu/parser/MiguParser.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMetadataBatchWriter.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlBatchTaskRepository.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMusicCatalogQueryRepository.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlSingerSourceSyncQueue.h"
#include "crawler/infrastructure/music/persistence/sql/SqlFileLoader.h"
#include "migu/support/MySqlTestSupport.h"
#include "mocks/MockMusicProvider.h"

#include <nlohmann/json.hpp>

#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace c = orchestrator::modules::crawler;
namespace model = c::application::model;
namespace use = c::application::use_cases;
namespace mysql = c::infrastructure::music::persistence::mysql;
namespace support = c::tests::migu::support;
using Json = nlohmann::json;
namespace {

// 失败不能依赖 assert 或构建模式。
void require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

// 手动执行后台任务，准确控制刷新前后的目录认领状态。
class Executor final : public c::application::ports::TaskExecutor {
public:
    std::vector<std::function<void()>> tasks; // 尚未执行的批次
    // 记录任务，测试线程显式推进。
    void submit(std::function<void()> task) override
    {
        tasks.push_back(std::move(task));
    }
    // 顺序完成本批任务，不使用真实线程或外部平台。
    void runAll()
    {
        auto pending = std::move(tasks);
        tasks.clear();
        for (auto &task : pending) task();
    }
};

// 只返回保存的资料响应；目录请求一旦发生就让测试失败。
class Provider final : public c::tests::MockMusicProvider {
public:
    std::map<std::string, Response> responses; // 来源 ID 对应的原始响应
    int calls = 0;                             // 资料请求总数
    int failures_remaining = 0;                // 接下来返回异常的请求数
    int failure_status = 503;                  // 瞬时服务错误或不可重试客户端错误
    bool empty_response = false;               // 模拟 HTTP 200 的临时空实体响应
    // 使用离线响应验证生产刷新流程。
    std::optional<Response> fetchSingerInfo(std::uint64_t id) override
    {
        ++calls;
        const auto found = responses.find(std::to_string(id));
        if (found != responses.end() && failures_remaining > 0) {
            --failures_remaining;
            auto response = found->second;
            response.status_code = failure_status;
            if (empty_response) response.body = {'{', '}'};
            return response;
        }
        return found == responses.end() ? std::nullopt : std::optional<Response>(found->second);
    }
    // 禁止资料刷新隐式采集歌曲目录。
    std::optional<Response> fetchSingerSongsPage(std::uint64_t, int) override
    {
        throw std::runtime_error("unexpected song directory request");
    }
    // 禁止资料刷新隐式采集专辑目录。
    std::optional<Response> fetchSingerAlbumsPage(std::uint64_t, int) override
    {
        throw std::runtime_error("unexpected album directory request");
    }
};

// 同一连接执行可重复迁移中的 PREPARE 与会话变量。
boost::asio::awaitable<bool> repeatCleanup(const std::shared_ptr<mysql::MySqlConnectionPool> &pool)
{
    auto connection = co_await pool->coAcquire();
    boost::mysql::results result;
    for (const auto &sql : c::infrastructure::music::persistence::sql::SqlFileLoader::splitStatements(c::infrastructure::music::persistence::sql::SqlFileLoader::loadOrThrow("sql/mysql/migrations/V2026102801__singer_profile_field_cleanup.sql")))
        co_await connection->async_execute(sql, result, boost::asio::use_awaitable);
    co_return true;
}

// 从文件读取已有响应；默认样本为独立离线 fixture。
Json readJson(const std::filesystem::path &path)
{
    std::ifstream file(path);
    require(file.good(), "sample file is missing");
    Json value;
    file >> value;
    return value;
}

// 原始响应只替换头像字段，保留身份和声明总数。
void replaceAvatar(Json &value, const std::string &avatar)
{
    if (value.is_object() && value.value("view", std::string {}) == "ZJ-SingerDetail-Item") value["img"] = avatar;
    if (value.is_structured())
        for (auto &child : value) replaceAvatar(child, avatar);
}

} // namespace

// 在新建独立库验证真实 adapter、旧结构升级和后台刷新，绝不请求平台。
int main(int argc, char **argv)
{
    try {
        auto config = support::makeMySqlConfigFromEnv();
        require(config.database.starts_with("crawler_ownership_test_singer_profile_"), "refusing non-profile-test database");
        auto old = config;
        const auto migrations = std::filesystem::temp_directory_path() / (config.database + "_before_cleanup");
        std::filesystem::create_directories(migrations);
        for (const auto &entry : std::filesystem::directory_iterator("sql/mysql/migrations"))
            if (entry.path().extension() == ".sql" && entry.path().filename().string() < "V2026102801") std::filesystem::copy_file(entry.path(), migrations / entry.path().filename(), std::filesystem::copy_options::overwrite_existing);
        old.migrations_path = migrations.string();
        auto pool = std::make_shared<mysql::MySqlConnectionPool>(old);
        require(support::runOnExecutor(pool->executor(), pool->coInitializeSchema(), "old singer schema"), "old schema initialization failed");
        auto execute = [&](std::string sql) { return support::runOnExecutor(pool->executor(), pool->coExecute(std::move(sql)), "profile verification SQL"); };
        auto scalar = [&](std::string sql) { const auto result = execute(std::move(sql)); return mysql::MySqlValueReader::uint64At(result.rows().front(), 0); };
        require(scalar("SELECT COUNT(*) FROM singer_source_refs") == 0, "profile fixture requires an empty database");

        auto provider = std::make_shared<Provider>();
        auto parser = std::make_shared<c::infrastructure::music::migu::parser::MiguParser>();
        auto writer = std::make_shared<mysql::MySqlMetadataBatchWriter>(pool);
        Json samples = Json::array();
        std::filesystem::path saved;
        if (argc == 3 && std::string(argv[1]) == "--saved-profiles") {
            saved = argv[2];
            samples = readJson(saved / "selected.json");
        } else {
            samples = Json::array({{{"source_singer_id", "70001"}, {"name", "索引歌手"}, {"source_gender", 2}, {"source_country", "华语"}, {"source_name_pinyin", "suoyin"}, {"source_song_total", nullptr}, {"source_album_total", nullptr}, {"indexed", 1}}, {{"source_singer_id", "70002"}, {"name", "关联歌手"}, {"source_gender", 0}, {"source_country", nullptr}, {"source_name_pinyin", "guanlian"}, {"source_song_total", 0}, {"source_album_total", 0}, {"indexed", 0}}});
        }
        int expected_missing = 0;
        model::MetadataWriteBatch seed;
        std::vector<model::SingerSourceProfile> expected;
        for (const auto &sample : samples) {
            model::SingerSourceMetadata singer;
            singer.source = "migu";
            singer.source_singer_id = sample.at("source_singer_id").get<std::string>();
            singer.name = singer.normalized_name = sample.at("name").get<std::string>();
            singer.gender = static_cast<c::domain::Gender>(sample.at("source_gender").get<int>());
            singer.source_country = sample.at("source_country").is_null() ? "" : sample.at("source_country").get<std::string>();
            singer.source_name_pinyin = sample.at("source_name_pinyin").is_null() ? "" : sample.at("source_name_pinyin").get<std::string>();
            if (sample.value("indexed", 0)) singer.source_index_tab = "huayu-nv";
            seed.singers.push_back(singer);
            model::SingerSourceProfile initial;
            initial.source_singer_id = singer.source_singer_id;
            if (!sample.at("source_song_total").is_null()) initial.source_song_total = sample.at("source_song_total").get<int>();
            if (!sample.at("source_album_total").is_null()) initial.source_album_total = sample.at("source_album_total").get<int>();
            if (!initial.source_song_total || !initial.source_album_total) ++expected_missing;
            seed.profiles.push_back(initial);
            Json body;
            if (!saved.empty())
                body = readJson(saved / "responses" / (singer.source_singer_id + ".json"));
            else
                body = {{"code", "000000"}, {"data", {{"contents", Json::array({{{"view", "ZJ-SingerDetail-Item"}, {"resId", singer.source_singer_id}, {"txt", singer.name}, {"img", "https://example.test/profile.png"}}, {{"view", "ZJ-Tab-Scroll"}, {"contents", Json::array({{{"view", "ZJ-Tab-Item"}, {"action", "song"}, {"txt2", "0"}}, {{"view", "ZJ-Tab-Item"}, {"action", "album"}, {"txt2", "3"}}})}}})}}}};
            model::ProviderResponse response;
            response.status_code = 200;
            const auto text = body.dump();
            response.body.assign(text.begin(), text.end());
            expected.push_back(parser->parseSingerSourceProfile(response.body));
            provider->responses.emplace(singer.source_singer_id, std::move(response));
        }
        require(writer->saveBatch(use::buildMetadataWriteBatch(seed)).isSuccess(), "old-schema fixture write failed");
        execute("UPDATE singers SET gender=2,status=3");
        execute("UPDATE singer_source_refs SET metadata_status=3,songs_sync_status=1,songs_synced_at=101,songs_sync_count=7,songs_sync_error='kept-song-error',songs_claim_token='other-song-worker',songs_claim_owner='song-owner',songs_claimed_at=102,albums_sync_status=2,albums_synced_at=201,albums_sync_count=8,albums_sync_error='kept-album-error',albums_claim_token='other-album-worker',albums_claim_owner='album-owner',albums_claimed_at=202");
        auto snapshot = [&] {
            const auto result = execute("SELECT SHA2(CAST(JSON_ARRAY(id,source,source_singer_id,source_gender,source_country,source_name_pinyin,songs_sync_status,songs_synced_at,songs_sync_count,songs_sync_error,songs_claim_token,songs_claim_owner,songs_claimed_at,albums_sync_status,albums_synced_at,albums_sync_count,albums_sync_error,albums_claim_token,albums_claim_owner,albums_claimed_at) AS CHAR),256) FROM singer_source_refs ORDER BY id");
            std::vector<std::string> hashes;
            for (const auto &row : result.rows()) hashes.push_back(mysql::MySqlValueReader::stringAt(row, 0));
            return hashes;
        };
        const auto protected_state = snapshot();
        writer.reset();
        pool->cancel();
        pool = support::makeInitializedPool();
        writer = std::make_shared<mysql::MySqlMetadataBatchWriter>(pool);
        require(snapshot() == protected_state, "cleanup changed source identities or directory state");
        require(scalar("SELECT COUNT(*) FROM information_schema.columns WHERE table_schema=DATABASE() AND ((table_name='singers' AND column_name IN('gender','status')) OR (table_name='singer_source_refs' AND column_name='metadata_status'))") == 0, "unused singer columns remain");
        require(support::runOnExecutor(pool->executor(), repeatCleanup(pool), "repeat cleanup") && snapshot() == protected_state, "cleanup is not safely repeatable");

        auto executor = std::make_shared<Executor>();
        auto catalog = std::make_shared<mysql::MySqlMusicCatalogQueryRepository>(pool);
        use::CrawlerServiceDependencies dependencies;
        dependencies.providers.singer_catalog_provider = provider;
        dependencies.parsers.parse_result_parser = parser;
        dependencies.parsers.song_source_parser = parser;
        dependencies.parsers.singer_source_parser = parser;
        dependencies.parsers.album_source_parser = parser;
        dependencies.parsers.singer_profile_parser = parser;
        dependencies.parsers.page_inspector = parser;
        dependencies.persistence.metadata_batch_writer = writer;
        dependencies.persistence.catalog_query_repository = catalog;
        dependencies.persistence.batch_task_repository = std::make_shared<mysql::MySqlBatchTaskRepository>(pool);
        dependencies.persistence.singer_source_sync_queue = std::make_shared<mysql::MySqlSingerSourceSyncQueue>(pool);
        dependencies.executors.batch_executor = executor;
        dependencies.options.runtime_settings.max_retries = 0;
        dependencies.options.max_retries = 0;
        auto service = std::make_shared<use::CrawlerService>(std::move(dependencies));
        c::api::controllers::CrawlerController controller(service);
        c::api::http::HttpRequest request;
        request.method = "POST";
        request.path = "/api/migu/singers/profile/refresh-from-db";
        request.body = R"({"limit":0})";
        require(controller.handleRefreshMiguSingerProfilesFromDb(request).status_code == 202, "profile-only batch was not accepted");
        executor->runAll();
        require(provider->calls == expected_missing, "default refresh requested known totals or skipped unknown classification");
        require(snapshot() == protected_state, "profile refresh changed directory claims, timestamps, counts or classification");
        require(controller.handleRefreshMiguSingerProfilesFromDb(request).status_code == 200, "known zero or completed profile was selected again");
        request.body = R"({"limit":0,"force":true})";
        require(controller.handleRefreshMiguSingerProfilesFromDb(request).status_code == 202, "forced profile refresh was not accepted");
        executor->runAll();
        require(provider->calls == expected_missing + static_cast<int>(samples.size()), "force did not refresh each profile exactly once");
        require(snapshot() == protected_state, "forced profile refresh changed protected source fields");
        for (const auto &profile : expected) {
            const auto record = catalog->findSinger("migu", profile.source_singer_id);
            require(record && record->source_song_total == profile.source_song_total && record->source_album_total == profile.source_album_total && !record->avatar_url.empty(), "profile totals or avatar URL were not saved");
        }
        const auto first_id = samples.front().at("source_singer_id");
        request.body = Json {{"source_ids", Json::array({first_id, first_id, samples.back().at("source_singer_id")})}, {"limit", 1}, {"force", true}}.dump();
        require(controller.handleRefreshMiguSingerProfilesFromDb(request).status_code == 202, "explicit profile source selection failed");
        executor->runAll();
        require(provider->calls == expected_missing + static_cast<int>(samples.size()) + 1 && snapshot() == protected_state, "explicit IDs ignored deduplication, limit or protected state");

        const auto source_id = first_id.get<std::string>();
        auto run_single = [&] {
            request.body = Json {{"source_ids", Json::array({source_id})}, {"limit", 0}, {"force", true}}.dump();
            const auto accepted = controller.handleRefreshMiguSingerProfilesFromDb(request);
            require(accepted.status_code == 202, "single profile test was not accepted");
            const auto batch_id = Json::parse(accepted.body).at("batch_id").get<std::string>();
            executor->runAll();
            return batch_id;
        };
        const auto original = provider->responses.at(source_id);
        const auto previous_avatar = catalog->findSinger("migu", source_id)->avatar_url;
        auto default_body = Json::parse(original.body);
        replaceAvatar(default_body, "https://d.musicapp.migu.cn/prod/file-service/file-down/bcb5ddaf77828caee4eddc172edaa105/33f90367b2251b76a025d9dadf2c6467/31e311fd0affc0ae0809011e265f79a5");
        const auto default_text = default_body.dump();
        provider->responses[source_id].body.assign(default_text.begin(), default_text.end());
        (void)run_single();
        require(catalog->findSinger("migu", source_id)->avatar_url == previous_avatar, "platform default avatar overwrote an existing URL");
        provider->responses[source_id] = original;

        model::CrawlerRuntimeSettingsUpdate retry_settings;
        retry_settings.max_retries = 1;
        require(!service->updateRuntimeSettings(retry_settings), "retry setting update failed");
        int calls_before = provider->calls;
        provider->failures_remaining = 1;
        auto retried = run_single();
        require(provider->calls == calls_before + 2 && scalar("SELECT retry_count FROM crawl_batch_items WHERE batch_id='" + retried + "'") == 1, "temporary failure was not retried once");
        require(scalar("SELECT COUNT(*) FROM crawl_batch_items WHERE batch_id='" + retried + "' AND state='Completed' AND last_error IS NULL") == 1, "successful retry retained a failure or wrong state");

        calls_before = provider->calls;
        provider->failure_status = 200;
        provider->empty_response = true;
        provider->failures_remaining = 1;
        retried = run_single();
        require(provider->calls == calls_before + 2 && scalar("SELECT retry_count FROM crawl_batch_items WHERE batch_id='" + retried + "'") == 1, "temporary empty profile was not retried");
        provider->empty_response = false;

        calls_before = provider->calls;
        provider->failure_status = 503;
        provider->failures_remaining = 10;
        retried = run_single();
        require(provider->calls == calls_before + 2 && scalar("SELECT COUNT(*) FROM crawl_batch_items WHERE batch_id='" + retried + "' AND state='Failed' AND retry_count=1") == 1, "retry exceeded its limit or hid a final failure");

        calls_before = provider->calls;
        provider->failure_status = 401;
        provider->failures_remaining = 10;
        retried = run_single();
        require(provider->calls == calls_before + 1 && scalar("SELECT retry_count FROM crawl_batch_items WHERE batch_id='" + retried + "'") == 0, "permanent HTTP error was retried");
        provider->failures_remaining = 0;

        calls_before = provider->calls;
        auto rejected_body = Json::parse(original.body);
        rejected_body["code"] = "999999";
        const auto rejected_text = rejected_body.dump();
        provider->responses[source_id].body.assign(rejected_text.begin(), rejected_text.end());
        retried = run_single();
        require(provider->calls == calls_before + 1 && scalar("SELECT retry_count FROM crawl_batch_items WHERE batch_id='" + retried + "'") == 0, "explicit business rejection was retried");
        provider->responses[source_id] = original;

        calls_before = provider->calls;
        provider->responses[source_id] = provider->responses.at(samples.back().at("source_singer_id").get<std::string>());
        retried = run_single();
        require(provider->calls == calls_before + 1 && scalar("SELECT retry_count FROM crawl_batch_items WHERE batch_id='" + retried + "'") == 0, "identity mismatch was retried");
        provider->responses[source_id] = original;
        require(snapshot() == protected_state, "default-avatar protection or retries changed directory state");
        require(scalar("SELECT COUNT(*) FROM singer_resources WHERE local_path IS NOT NULL OR fetched_at<>0 OR status<>0") == 0, "profile refresh downloaded images");
        require(scalar("SELECT COUNT(*) FROM songs") == 0 && scalar("SELECT COUNT(*) FROM albums") == 0, "profile refresh expanded into directories");
        const auto schema = catalog->schemaStatus();
        require(schema.ok && schema.latest_available_version == 2026102912, "schema status disagrees with the combined migrations");
        pool->cancel();
        std::cout << "MYSQL_PROFILE_REFRESH samples=" << samples.size() << " missing_refreshed=" << expected_missing << " forced_refreshed=" << samples.size() << " directory_state_preserved=1 downloads=0\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
