#include "crawler/application/use_cases/SingerCatalogWorkflow.h"
#include "crawler/infrastructure/music/migu/parser/MiguParser.h"
#include "crawler/infrastructure/music/migu/provider/MiguProvider.h"
#include "mocks/MockAlbumSourceRepository.h"
#include "mocks/MockMusicProvider.h"

#include <nlohmann/json.hpp>

#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace c = orchestrator::modules::crawler;
namespace model = c::application::model;
namespace cases = c::application::use_cases;

namespace {

// 所有断言在发布构建中也执行。
void require(bool condition, const char *message)
{
    if (!condition) throw std::runtime_error(message);
}

// 原始响应经过生产 parser 和 application，避免测试只复制终止条件。
class Provider final : public c::tests::MockMusicProvider {
public:
    std::map<int, std::string> songs;  // 歌曲分页原始响应
    std::map<int, std::string> albums; // 专辑分页原始响应
    std::vector<int> song_requests;    // 实际请求的歌曲页
    std::vector<int> album_requests;   // 实际请求的专辑页
    int failed_page = 0;               // 需要返回 HTTP 503 的页
    std::size_t song_page_size = 50;   // 显式平台分页提示；设为 0 验证未知容量回退
    std::size_t album_page_size = 10;  // 歌手专辑分页独立提示

    // 与当前 Migu 请求契约一致，允许测试独立关闭容量提示。
    std::size_t singerSongPageSize() const override
    {
        return song_page_size;
    }

    // 专辑容量不能借用歌曲容量。
    std::size_t singerAlbumPageSize() const override
    {
        return album_page_size;
    }

    // 缺失 fixture 返回明确空页，不能把不存在的分页当成网络错误。
    std::optional<Response> fetchSingerSongsPage(std::uint64_t, int page) override
    {
        song_requests.push_back(page);
        return make(songs, page);
    }

    // 专辑目录复用同一有界采集规则。
    std::optional<Response> fetchSingerAlbumsPage(std::uint64_t, int page) override
    {
        album_requests.push_back(page);
        return make(albums, page);
    }

private:
    // 只控制外部响应，不代替分页决策或模型解析。
    Response make(const std::map<int, std::string> &pages, int page) const
    {
        Response result;
        result.status_code = page == failed_page ? 503 : 200;
        const auto found = pages.find(page);
        const std::string raw = found == pages.end() ? R"({"data":{"contents":[]}})" : found->second;
        result.body.assign(raw.begin(), raw.end());
        return result;
    }
};

// 事务替身只记录实际经过生产 batch 构建的数量，不参与分页判断。
class Writer final : public c::application::ports::MetadataBatchWriter {
public:
    std::size_t songs = 0;  // 实际交给 writer 的歌曲数
    std::size_t albums = 0; // 实际交给 writer 的专辑数
    bool fail_next = false; // 在提交前返回可恢复的写入失败
    std::set<std::string> unique_songs;  // 已成功提交的歌曲来源集合
    std::set<std::string> unique_albums; // 已成功提交的专辑来源集合
    std::map<std::string, std::string> song_spellings; // 重复响应补充的非空资料
    std::set<std::pair<std::string, std::string>> song_albums; // 同一歌曲的不同专辑上下文
    std::set<std::pair<std::string, std::string>> song_identities; // 实际提交的歌曲、内容身份组合

    // 入库失败路径已有独立事务测试，本测试专注分页完整度与请求上限。
    c::application::result::ServiceResult<model::MetadataWriteResult> saveBatch(const model::MetadataWriteBatch &batch) override
    {
        if (fail_next) {
            fail_next = false;
            return c::application::result::ServiceResult<model::MetadataWriteResult>::failure(c::application::result::ServiceErrorType::InternalError, "injected metadata write failure");
        }
        songs += batch.songs.size();
        albums += batch.albums.size();
        for (const auto &song : batch.songs) {
            song_identities.emplace(song.source_song_id, song.source_content_id);
            unique_songs.insert(song.source_content_id);
            if (!song.source_title_pinyin.empty()) song_spellings[song.source_content_id] = song.source_title_pinyin;
            if (!song.source_album_id.empty()) song_albums.emplace(song.source_content_id, song.source_album_id);
        }
        for (const auto &album : batch.albums) unique_albums.insert(album.source_album_id);
        return c::application::result::ServiceResult<model::MetadataWriteResult>::success({});
    }
};

// 保留真实的歌曲列表与专辑卡片结构，中间页可以短于固定页大小。
std::string page(bool songs, int number, int count)
{
    auto entries = nlohmann::json::array();
    for (int index = 0; index < count; ++index) {
        const auto id = std::to_string(number * 100 + index);
        if (songs)
            entries.push_back({{"songItem", {{"songName", "分页歌曲" + id}, {"contentId", id}, {"songId", id}}}});
        else
            entries.push_back({{"view", "ZJ-Album-Item"}, {"resId", id}, {"txt", "分页专辑" + id}, {"resType", "0"}});
    }
    nlohmann::json contents = songs ? nlohmann::json::array({{{"contents", entries}}}) : entries;
    return nlohmann::json({{"data", {{"contents", contents}}}}).dump();
}

// 每个用例独占 workflow 状态，测试时不启用线程池或真实网络。
class Fixture {
public:
    Fixture()
    {
        auto parser = std::make_shared<c::infrastructure::music::migu::parser::MiguParser>();
        auto dispatch = std::make_shared<cases::ParserDispatch>(parser, parser, parser, parser, parser, parser, nullptr, nullptr);
        auto persistence = std::make_shared<cases::MetadataPersistenceService>(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, cases::MetadataIngestionDependencies {writer, {}});
        workflow = std::make_unique<cases::SingerCatalogWorkflow>(provider, dispatch, persistence, nullptr, nullptr);
        provider->songs = {{1, page(true, 1, 50)}, {2, page(true, 2, 49)}, {3, page(true, 3, 25)}};
        provider->albums = {{1, page(false, 1, 10)}, {2, page(false, 2, 9)}, {3, page(false, 3, 2)}};
    }

    std::shared_ptr<Provider> provider = std::make_shared<Provider>(); // 唯一外部接口替身
    std::shared_ptr<Writer> writer = std::make_shared<Writer>();       // 事务输入记录
    std::unique_ptr<cases::SingerCatalogWorkflow> workflow;            // 生产目录采集流程
};

// 已知总数时不能在中间短页停止，顺序和并发窗口都必须得到完整结果。
void verifyShortPages()
{
    for (int window : {1, 4}) {
        Fixture songs;
        const auto result = songs.workflow->crawlSingerSongsAndSaveResult(42, 0, window, 124, 21);
        require(result.success && !result.partial && result.written_count == 124 && songs.writer->songs == 124, "intermediate 49-song page truncated known catalog");
        require(songs.provider->song_requests == std::vector<int> {1, 2, 3}, "short-page collection requested extra pages or skipped a contiguous page");
        Fixture albums;
        const auto album_result = albums.workflow->crawlSingerAlbumsAndSaveResult(42, 0, window, 124, 21);
        require(album_result.success && album_result.written_count == 21 && albums.writer->albums == 21, "intermediate 9-album page truncated known catalog");
        require(albums.provider->album_requests == std::vector<int> {1, 2, 3}, "album pages used the song capacity or an oversized tail window");
        std::cout << "REQUEST_COUNTS window=" << window << " song_pages=" << songs.provider->song_requests.size() << " album_pages=" << albums.provider->album_requests.size() << " songs=" << result.written_count << " albums=" << album_result.written_count << '\n';
        if (window == 4) {
            Fixture original_songs;
            original_songs.provider->song_page_size = 0;
            const auto before_songs = original_songs.workflow->crawlSingerSongsAndSaveResult(42, 0, 4, 124, 21);
            Fixture original_albums;
            original_albums.provider->album_page_size = 0;
            const auto before_albums = original_albums.workflow->crawlSingerAlbumsAndSaveResult(42, 0, 4, 124, 21);
            require(before_songs.success && before_albums.success && original_songs.writer->unique_songs == songs.writer->unique_songs && original_albums.writer->unique_albums == albums.writer->unique_albums, "tail-window optimization changed persisted source identities");
            std::cout << "REQUEST_COMPARISON song_pages=" << original_songs.provider->song_requests.size() << "->" << songs.provider->song_requests.size() << " album_pages=" << original_albums.provider->album_requests.size() << "->" << albums.provider->album_requests.size() << " identical_source_sets=true\n";
        }
    }
}

// 真实空页仍终止，未满足的平台总数必须保持部分完成，不能编造缺失对象。
void verifyIncompleteAndLimit()
{
    Fixture incomplete;
    const auto result = incomplete.workflow->crawlSingerSongsAndSaveResult(42, 0, 1, 125);
    require(!result.success && result.partial && result.written_count == 124 && incomplete.provider->song_requests.back() == 4, "empty final page or unknown missing item was treated as complete");
    Fixture limited;
    const auto limited_result = limited.workflow->crawlSingerSongsAndSaveResult(42, 75, 1, 124);
    require(!limited_result.success && limited_result.partial && limited.writer->songs == 75 && limited.provider->song_requests.back() == 2, "explicit limit was bypassed");
    Fixture failed;
    failed.provider->failed_page = 2;
    const auto failed_result = failed.workflow->crawlSingerSongsAndSaveResult(42, 0, 1, 124);
    require(!failed_result.success && failed_result.partial && failed_result.error == "failed to fetch singer song page", "HTTP failure was mistaken for a valid short page");
}

// 重复卡片只补资料，不占用本轮唯一来源数或调用方的选取限额。
void verifyDuplicateSources()
{
    for (int window : {1, 4}) {
        Fixture songs;
        songs.provider->songs = {{1, page(true, 1, 50)}, {2, page(true, 1, 50)}};
        const auto partial = songs.workflow->crawlSingerSongsAndSaveResult(42, 0, window, 100);
        require(!partial.success && partial.partial && partial.written_count == 50 && songs.writer->unique_songs.size() == 50, "duplicate song pages falsely completed the declared catalog");
        Fixture albums;
        albums.provider->albums = {{1, page(false, 1, 10)}, {2, page(false, 1, 10)}};
        const auto partial_albums = albums.workflow->crawlSingerAlbumsAndSaveResult(42, 0, window, {}, 20);
        require(!partial_albums.success && partial_albums.partial && partial_albums.written_count == 10 && albums.writer->unique_albums.size() == 10, "duplicate album pages falsely completed the declared catalog");

        auto supplement = nlohmann::json::parse(page(true, 1, 50));
        auto &card = supplement["data"]["contents"][0]["contents"][0]["songItem"];
        card["songPinyin"] = "supplemental";
        card["albumId"] = "second-album";
        card["album"] = "重复响应携带的专辑";
        songs.provider->songs = {{1, page(true, 1, 50)}, {2, supplement.dump()}, {3, page(true, 3, 50)}};
        const auto limited = songs.workflow->crawlSingerSongsAndSaveResult(42, 75, window, 100);
        require(limited.partial && limited.written_count == 75 && songs.writer->unique_songs.size() == 75 && songs.provider->song_requests.back() == 3, "duplicate songs consumed the unique selection limit");
        require(songs.writer->song_spellings.at("100") == "supplemental" && songs.writer->song_albums.contains({"100", "second-album"}), "count deduplication discarded supplemental fields or album relations");
        albums.provider->albums.emplace(3, page(false, 3, 10));
        const auto limited_albums = albums.workflow->crawlSingerAlbumsAndSaveResult(42, 15, window, {}, 20);
        require(limited_albums.partial && limited_albums.written_count == 15 && albums.writer->unique_albums.size() == 15 && albums.provider->album_requests.back() == 3, "duplicate albums consumed the unique selection limit");

        const auto full = songs.workflow->crawlSingerSongsAndSaveResult(42, 0, window, 100);
        const auto full_albums = albums.workflow->crawlSingerAlbumsAndSaveResult(42, 0, window, {}, 20);
        require(full.success && full.written_count == 100 && songs.writer->unique_songs.size() == 100, "overlapping song pages hid later unique sources");
        require(full_albums.success && full_albums.written_count == 20 && albums.writer->unique_albums.size() == 20, "overlapping album pages hid later unique sources");
        std::cout << "UNIQUE_SOURCE_COUNTS window=" << window << " partial_songs=" << partial.written_count << "/100 partial_albums=" << partial_albums.written_count << "/20 complete_songs=" << full.written_count << " complete_albums=" << full_albums.written_count << '\n';
    }

    Fixture cards;
    auto duplicate_songs = nlohmann::json::parse(page(true, 1, 50));
    auto &song_cards = duplicate_songs["data"]["contents"][0]["contents"];
    const auto repeated_song = song_cards.front();
    for (int index = 0; index < 50; ++index) song_cards.push_back(repeated_song);
    cards.provider->songs = {{1, duplicate_songs.dump()}};
    require(cards.workflow->crawlSingerSongsAndSaveResult(42, 0, 1, 100).written_count == 50, "within-page duplicate song cards inflated the count");
    auto duplicate_albums = nlohmann::json::parse(page(false, 1, 10));
    auto &album_cards = duplicate_albums["data"]["contents"];
    const auto repeated_album = album_cards.front();
    for (int index = 0; index < 10; ++index) album_cards.push_back(repeated_album);
    cards.provider->albums = {{1, duplicate_albums.dump()}};
    require(cards.workflow->crawlSingerAlbumsAndSaveResult(42, 0, 1, {}, 20).written_count == 10, "within-page duplicate album cards inflated the count");

    // 同一 songId 的替代 contentId 或缺失 contentId 不代表新的来源实体。
    Fixture aliases;
    auto alternative = nlohmann::json::parse(page(true, 1, 50));
    for (auto &item : alternative["data"]["contents"][0]["contents"]) item["songItem"]["contentId"] = "alternative-" + item["songItem"]["songId"].get<std::string>();
    aliases.provider->songs = {{1, page(true, 1, 50)}, {2, alternative.dump()}};
    const auto alias_result = aliases.workflow->crawlSingerSongsAndSaveResult(42, 0, 1, 100);
    require(alias_result.partial && alias_result.written_count == 50, "alternate content identity was counted as a second song source");
    for (auto &item : alternative["data"]["contents"][0]["contents"]) item["songItem"].erase("contentId");
    aliases.provider->songs[2] = alternative.dump();
    require(aliases.workflow->crawlSingerSongsAndSaveResult(42, 0, 1, 100).written_count == 50, "missing content identity duplicated an already selected songId");

    // 兼容仓储也必须保存选中的全部片段，不能再次按原始条数截断唯一来源。
    auto provider = std::make_shared<Provider>();
    provider->albums = {{1, page(false, 1, 10)}, {2, page(false, 1, 10)}, {3, page(false, 3, 10)}};
    auto repository = std::make_shared<c::tests::MockAlbumSourceRepository>();
    auto parser = std::make_shared<c::infrastructure::music::migu::parser::MiguParser>();
    auto dispatch = std::make_shared<cases::ParserDispatch>(parser, parser, parser, parser, parser, parser, nullptr, nullptr);
    auto persistence = std::make_shared<cases::MetadataPersistenceService>(nullptr, nullptr, nullptr, nullptr, nullptr, nullptr, repository, nullptr);
    cases::SingerCatalogWorkflow legacy(provider, dispatch, persistence, nullptr, nullptr);
    const auto legacy_result = legacy.crawlSingerAlbumsAndSaveResult(42, 15, 1, {}, 20);
    std::set<std::string> retained;
    for (const auto &album : repository->saved_albums) retained.insert(album.source_album_id);
    require(legacy_result.partial && legacy_result.written_count == 15 && retained.size() == 15, "legacy album persistence reapplied a raw-card limit after unique selection");
}

// 未被稳定绑定采用的替代 ID 不能污染后续选择，两个身份方向都应遵守限额。
void verifyConflictingIdentityLimits()
{
    for (bool alternate_content : {false, true}) {
        for (int limit : {1, 2}) {
            Fixture fixture;
            nlohmann::json first = {{"songId", "first-song"}, {"contentId", "first-content"}, {"songName", "首个稳定来源"}};
            auto alternative = first;
            alternative[alternate_content ? "contentId" : "songId"] = alternate_content ? "second-content" : "second-song";
            auto second = first;
            second["songId"] = "second-song";
            second["contentId"] = "second-content";
            second["songName"] = "第二个独立来源";
            const nlohmann::json cards = nlohmann::json::array({{{"songItem", first}}, {{"songItem", alternative}}, {{"songItem", second}}});
            fixture.provider->songs = {{1, nlohmann::json({{"data", {{"contents", nlohmann::json::array({{{"contents", cards}}})}}}}).dump()}};
            const auto result = fixture.workflow->crawlSingerSongsAndSaveResult(42, limit, 1, 2);
            std::cout << "IDENTITY_SELECTION alternate_content=" << alternate_content << " limit=" << limit << " reported=" << result.written_count << " selected_second=" << fixture.writer->song_identities.contains({"second-song", "second-content"}) << '\n';
            require(result.written_count == limit && (limit == 1 ? result.partial : result.success), "conflicting alternate identity corrupted the unique source count");
            require(fixture.writer->song_identities.contains({"second-song", "second-content"}) == (limit == 2), "conflicting alternate identity bypassed the unique song limit");
        }
    }
}

// 中间页和写入失败后重新驱动同一 workflow，失败响应不能成为后续重试的完成依据。
void verifyFailureAndRetry()
{
    Fixture songs;
    songs.provider->failed_page = 2;
    const auto partial = songs.workflow->crawlSingerSongsAndSaveResult(42, 0, 1, 124, 21);
    require(!partial.success && partial.partial && partial.written_count == 50 && songs.writer->unique_songs.size() == 50, "failed song page did not preserve the committed prefix");
    songs.provider->failed_page = 0;
    const auto recovered = songs.workflow->crawlSingerSongsAndSaveResult(42, 0, 1, 124, 21);
    require(recovered.success && !recovered.partial && recovered.written_count == 124 && songs.writer->unique_songs.size() == 124, "song-page retry did not replay all expected source identities");

    Fixture albums;
    albums.provider->failed_page = 2;
    const auto partial_albums = albums.workflow->crawlSingerAlbumsAndSaveResult(42, 0, 1, 124, 21);
    require(!partial_albums.success && partial_albums.partial && partial_albums.written_count == 10 && albums.writer->unique_albums.size() == 10, "failed album page lost its available prefix or became complete");
    albums.provider->failed_page = 0;
    const auto recovered_albums = albums.workflow->crawlSingerAlbumsAndSaveResult(42, 0, 1, 124, 21);
    require(recovered_albums.success && recovered_albums.written_count == 21 && albums.writer->unique_albums.size() == 21, "album-page retry did not replay the complete catalog");

    Fixture rejected;
    rejected.writer->fail_next = true;
    const auto failed = rejected.workflow->crawlSingerSongsAndSaveResult(42, 0, 1, 124, 21);
    require(!failed.success && failed.written_count == 0 && rejected.writer->unique_songs.empty(), "failed write was counted as persisted metadata");
    const auto saved = rejected.workflow->crawlSingerSongsAndSaveResult(42, 0, 1, 124, 21);
    require(saved.success && saved.written_count == 124 && rejected.writer->unique_songs.size() == 124, "retry after rejected write lost catalog data");
    Fixture rejected_albums;
    rejected_albums.writer->fail_next = true;
    const auto failed_albums = rejected_albums.workflow->crawlSingerAlbumsAndSaveResult(42, 0, 1, 124, 21);
    require(!failed_albums.success && failed_albums.written_count == 0 && rejected_albums.writer->unique_albums.empty(), "rejected album batch was counted as committed");
    const auto saved_albums = rejected_albums.workflow->crawlSingerAlbumsAndSaveResult(42, 0, 1, 124, 21);
    require(saved_albums.success && saved_albums.written_count == 21 && rejected_albums.writer->unique_albums.size() == 21, "album retry after rejected write lost its catalog");
}

// 动态窗口必须在短中间页后连续推进，不能按总数/容量推导一个绝对末页。
void verifyAdaptiveWindows()
{
    c::infrastructure::music::migu::provider::MiguProvider actual(nullptr);
    require(actual.singerSongPageSize() == 50 && actual.singerAlbumPageSize() == 10, "production provider page-size hints differ from its parser contract");
    Fixture songs;
    songs.provider->songs = {{1, page(true, 1, 50)}, {2, page(true, 2, 1)}, {3, page(true, 3, 1)}, {4, page(true, 4, 48)}};
    const auto result = songs.workflow->crawlSingerSongsAndSaveResult(42, 0, 4, 100);
    require(result.success && result.written_count == 100 && songs.writer->unique_songs.size() == 100 && songs.provider->song_requests == std::vector<int> {1, 2, 3, 4}, "shrinking song window skipped later short-page data");
    Fixture albums;
    albums.provider->albums = {{1, page(false, 1, 10)}, {2, page(false, 2, 1)}, {3, page(false, 3, 1)}, {4, page(false, 4, 8)}};
    const auto album_result = albums.workflow->crawlSingerAlbumsAndSaveResult(42, 0, 4, {}, 20);
    require(album_result.success && album_result.written_count == 20 && albums.writer->unique_albums.size() == 20 && albums.provider->album_requests == std::vector<int> {1, 2, 3, 4}, "shrinking album window discarded required pages");

    Fixture exact;
    exact.provider->songs = {{1, page(true, 1, 50)}, {2, page(true, 2, 50)}};
    const auto exact_result = exact.workflow->crawlSingerSongsAndSaveResult(42, 0, 4, 100);
    require(exact_result.success && exact_result.written_count == 100 && exact.provider->song_requests == std::vector<int> {1, 2, 3}, "full tail lost its single confirmation request or fetched an oversized window");
    Fixture understated;
    understated.provider->songs = {{1, page(true, 1, 50)}, {2, page(true, 2, 50)}, {3, page(true, 3, 50)}, {4, page(true, 4, 1)}};
    const auto extra = understated.workflow->crawlSingerSongsAndSaveResult(42, 0, 4, 100);
    require(extra.success && extra.written_count == 151 && understated.provider->song_requests == std::vector<int> {1, 2, 3, 4, 5, 6, 7}, "stale total did not fall back to empty-page confirmation");
    Fixture stale_short;
    stale_short.provider->songs = {{1, page(true, 1, 50)}, {2, page(true, 2, 1)}, {3, page(true, 3, 49)}};
    const auto stale_short_result = stale_short.workflow->crawlSingerSongsAndSaveResult(42, 0, 4, 50);
    require(stale_short_result.success && stale_short_result.written_count == 100 && stale_short.writer->unique_songs.size() == 100 && stale_short.provider->song_requests.back() >= 4, "outdated total and a short middle page hid later songs");
    Fixture limited;
    const auto limit_result = limited.workflow->crawlSingerSongsAndSaveResult(42, 75, 4, 124);
    require(limit_result.partial && limit_result.written_count == 75 && limited.provider->song_requests == std::vector<int> {1, 2}, "known selection limit fetched unused pages");

    Fixture unknown_total;
    const auto unknown = unknown_total.workflow->crawlSingerSongsAndSaveResult(42, 0, 4);
    require(unknown.written_count == 124 && unknown_total.provider->song_requests == std::vector<int> {1, 2, 3, 4}, "unknown total was guessed from a short page");
    Fixture unknown_capacity;
    unknown_capacity.provider->song_page_size = 0;
    const auto capacity_result = unknown_capacity.workflow->crawlSingerSongsAndSaveResult(42, 0, 4, 124);
    require(capacity_result.success && capacity_result.written_count == 124 && unknown_capacity.provider->song_requests == std::vector<int> {1, 2, 3, 4}, "unknown provider page size was silently guessed");
    Fixture unknown_album_total;
    const auto unknown_albums = unknown_album_total.workflow->crawlSingerAlbumsAndSaveResult(42, 0, 4);
    require(unknown_albums.written_count == 21 && unknown_album_total.provider->album_requests == std::vector<int> {1, 2, 3, 4}, "unknown album total changed its paging behavior");
    Fixture unknown_album_capacity;
    unknown_album_capacity.provider->album_page_size = 0;
    const auto unknown_album_size = unknown_album_capacity.workflow->crawlSingerAlbumsAndSaveResult(42, 0, 4, {}, 21);
    require(unknown_album_size.success && unknown_album_size.written_count == 21 && unknown_album_capacity.provider->album_requests == std::vector<int> {1, 2, 3, 4}, "unknown album capacity was guessed");

    Fixture bounded;
    bounded.provider->songs.clear();
    for (int number = 1; number <= 50; ++number) bounded.provider->songs.emplace(number, page(true, number, 50));
    const auto capped = bounded.workflow->crawlSingerSongsAndSaveResult(42, 0, 4, 2501);
    require(capped.partial && !capped.success && capped.written_count == 2500 && bounded.provider->song_requests.size() == 50 && bounded.provider->song_requests.back() == 50, "adaptive windows bypassed the global pagination guard");
    const auto stale_capped = bounded.workflow->crawlSingerSongsAndSaveResult(42, 0, 4, 100);
    require(stale_capped.partial && !stale_capped.success && stale_capped.written_count == 2500 && stale_capped.error == "singer song pagination limit reached", "page cap falsely completed a catalog with an outdated total");
    Fixture capped_albums;
    capped_albums.provider->albums.clear();
    for (int number = 1; number <= 50; ++number) capped_albums.provider->albums.emplace(number, page(false, number, 10));
    const auto capped_album_result = capped_albums.workflow->crawlSingerAlbumsAndSaveResult(42, 0, 4, {}, 10);
    require(capped_album_result.partial && !capped_album_result.success && capped_album_result.written_count == 500 && capped_album_result.error == "singer album pagination limit reached", "album page cap became a false complete result");
}

} // namespace

int main()
{
    try {
        verifyShortPages();
        verifyIncompleteAndLimit();
        verifyDuplicateSources();
        verifyConflictingIdentityLimits();
        verifyFailureAndRetry();
        verifyAdaptiveWindows();
        std::cout << "PASS: short middle pages, empty termination, known incomplete counts, explicit limits and HTTP failures\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
