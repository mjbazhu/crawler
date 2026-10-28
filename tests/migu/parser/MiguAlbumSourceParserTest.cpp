#include "crawler/infrastructure/music/migu/parser/MiguParser.h"

#include <nlohmann/json.hpp>

#include <iostream>
#include <stdexcept>
#include <string>
#include <vector>

namespace crawler = orchestrator::modules::crawler;
namespace migu_parser = crawler::infrastructure::music::migu::parser;

namespace {

// 测试断言失败时抛出异常，避免依赖 assert 宏影响 clangd 检查。
void require(bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

// 将离线响应文本转换为解析器输入。
std::vector<char> toBytes(const std::string &text)
{
    return std::vector<char>(text.begin(), text.end());
}

// 验证专辑列表字段和尾页识别。
void testParseSingerAlbumList()
{
    const std::string raw = R"({
        "code": "000000",
        "data": {
            "header": {"title": "专辑列表页"},
            "contents": [{
                "view": "ZJ-Album-Item",
                "action": "mgmusic://digital-album-info?id=600927015009001083",
                "resType": "1",
                "resId": "600927015009001083",
                "img": "https://example.test/cover.webp",
                "txt": "太阳之子",
                "txt2": "周杰伦",
                "txt3": "2026-03-19"
            }]
        }
    })";

    migu_parser::MiguParser parser;
    const auto albums = parser.parseAlbumSources(toBytes(raw));
    require(albums.size() == 1, "albums.size() == 1");
    require(albums.front().source_album_id == "600927015009001083", "list source album id");
    require(albums.front().title == "太阳之子", "list title");
    require(albums.front().artist_name == "周杰伦", "list artist name");
    require(albums.front().source_album_kind == 2, "albums.front().source_album_kind == 2");
    require(albums.front().detail_sync_status == 0, "albums.front().detail_sync_status == 0");

    const auto page_info = parser.inspectSingerAlbumPage(toBytes(raw));
    require(page_info.album_count == 1, "page_info.album_count == 1");
    require(page_info.should_stop_paging, "page_info.should_stop_paging");
}

// 满页不能结束翻页，最后一页及空页才停止；对应真实的 41 张专辑响应。
void testSingerAlbumPagingUsesTenItemPages()
{
    migu_parser::MiguParser parser;
    std::size_t count = 0;
    for (std::size_t page = 0; page < 5; ++page) {
        auto root = nlohmann::json::parse(R"({"data":{"contents":[]}})");
        const std::size_t size = page == 4 ? 1 : 10;
        for (std::size_t index = 0; index < size; ++index) {
            root["data"]["contents"].push_back({{"view", "ZJ-Album-Item"}, {"resId", std::to_string(page * 10 + index)}, {"txt", "分页专辑"}});
        }
        const auto raw = toBytes(root.dump());
        const auto info = parser.inspectSingerAlbumPage(raw);
        require(info.album_count == size, "album page size was not counted");
        require(info.should_stop_paging == (page == 4), "full ten-item album page ended pagination early");
        count += parser.parseAlbumSources(raw).size();
    }
    require(count == 41, "five album pages did not retain all 41 albums");
    require(parser.inspectSingerAlbumPage(toBytes(R"({"data":{"contents":[]}})")).should_stop_paging, "empty album page did not stop pagination");
}

// 数字专辑详情的外层身份优先于歌曲自带的其他专辑身份。
void testParseDigitalAlbumDetail()
{
    const std::string raw = R"({
        "code": "000000",
        "resource": [{
            "resourceType": "5",
            "copyrightId": "60050403770",
            "contentId": "600927015009001083",
            "materialId": "1142521343",
            "imgItem": [{"imgSizeType": "03", "img": "https://example.test/detail.webp"}],
            "title": "太阳之子",
            "singer": "周杰伦",
            "singerId": "112",
            "summary": "专辑简介",
            "price": "4000",
            "totalCount": "13",
            "presaleDate": "",
            "firstStartDate": "2026-03-19",
            "firstEndDate": "2030-12-31",
            "onlineDate": "20260319130000",
            "saleEndDate": "20301231235959",
            "invalidateDate": "20301231",
            "isBeforeSaleEndDate": "1",
            "dalbumPeriodStatus": "1",
            "itemId": "87688759",
            "redHareId": "102152",
            "publishDate": "2026-03-25",
            "foreverListenFlag": "1",
            "foreverListen": true,
            "isAppointment": "0",
            "opNumItem": {"playNum": 9481068, "playNumDesc": "948.1万"},
            "songItems": [{
                "resourceType": "2",
                "refId": "600927000001",
                "songId": "1142521344",
                "songName": "太阳之子",
                "albumId": "1142521343",
                "dalbumId": "other-digital-context",
                "duration": 297,
                "singer": "周杰伦",
                "singerId": "112",
                "singerList": [
                    {"id": "112", "name": "周杰伦", "img": "https://example.test/jay.webp", "nameSpelling": "zhoujielun"},
                    {"id": "266", "name": "林俊杰", "img": "https://example.test/jj.webp", "nameSpelling": "linjunjie"}
                ],
                "ext": {"disc": "Disc 2"},
                "lrcUrl": "https://example.test/song.lrc",
                "audioFormats": [{
                    "resourceType": "2",
                    "formatType": "HQ",
                    "isize": "9844195",
                    "asize": "9844195",
                    "iformat": "020010",
                    "aformat": "020010"
                }]
            }]
        }]
    })";

    migu_parser::MiguParser parser;
    const auto albums = parser.parseAlbumSources(toBytes(raw));
    require(albums.size() == 1, "albums.size() == 1");
    require(albums.front().source_album_id == "600927015009001083", "digital source album id");
    require(albums.front().source_material_id == "1142521343", "digital material id");
    require(albums.front().source_album_kind == 2, "albums.front().source_album_kind == 2");
    require(albums.front().summary == "专辑简介", "digital summary");
    require(albums.front().source_item_id == "87688759", "digital item id");
    require(albums.front().source_campaign_id == "102152", "digital campaign id");
    require(albums.front().first_start_date == "2026-03-19", "digital first start date");
    require(albums.front().sale_end_date == "20301231235959", "digital sale end date");
    require(albums.front().digital_period_status == "1", "digital period status");
    require(albums.front().forever_listen == 1, "albums.front().forever_listen == 1");
    require(albums.front().is_before_sale_end == 1, "albums.front().is_before_sale_end == 1");
    require(albums.front().is_appointment == 0, "albums.front().is_appointment == 0");
    require(albums.front().play_num == 9481068, "albums.front().play_num == 9481068");
    require(albums.front().detail_sync_status == 2, "albums.front().detail_sync_status == 2");

    const auto songs = parser.parseSongSources(toBytes(raw));
    require(songs.size() == 1, "songs.size() == 1");
    require(songs.front().source_content_id == "600927000001", "song source content id from refId");
    require(songs.front().source_album_id == "600927015009001083", "song source album id from album detail");
    require(songs.front().source_album_material_id == "1142521343", "song material mapping from album detail");
    require(songs.front().source_album_name == "太阳之子", "song source album name from album detail");
    require(songs.front().source_primary_artist_id == "112", "song source singer id");
    require(songs.front().artist_refs.size() == 2, "song source artist refs size");
    require(songs.front().artist_refs.back().source_artist_id == "266", "song source coartist id");
    require(songs.front().album_disc_no == 2, "song source disc no");
    require(songs.front().album_track_no == 1, "song source track no");
    require(songs.front().album_song_order == 1, "song source album song order");
    require(songs.front().audio_formats.size() == 1, "song source audio format size");
    require(songs.front().audio_formats.front().format_type == "HQ", "song source audio format type");
}

void testParseCatalogAlbumDetail()
{
    const std::string raw = R"({
        "code": "000000",
        "resource": [{
            "resourceType": "2003",
            "albumId": "1141456456",
            "imgItems": [{"imgSizeType": "03", "img": "https://example.test/catalog.webp"}],
            "title": "即兴曲",
            "singer": "周杰伦",
            "singerId": "112",
            "summary": "普通专辑简介",
            "totalCount": "1",
            "publishTime": "2025-06-06",
            "publishDate": "2025-06-06",
            "opNumItem": {"playNum": 22318, "playNumDesc": "2.2万"}
        }]
    })";

    migu_parser::MiguParser parser;
    const auto albums = parser.parseAlbumSources(toBytes(raw));
    require(albums.size() == 1, "albums.size() == 1");
    require(albums.front().source_album_id == "1141456456", "catalog source album id");
    require(albums.front().source_resource_type == "2003", "catalog resource type");
    require(albums.front().source_album_kind == 1, "albums.front().source_album_kind == 1");
    require(albums.front().source_artist_id == "112", "catalog artist id");
    require(albums.front().cover_url == "https://example.test/catalog.webp", "catalog cover url");
    require(albums.front().release_date == "2025-06-06", "catalog release date");
    require(albums.front().play_num == 22318, "albums.front().play_num == 22318");
    require(albums.front().detail_sync_status == 2, "albums.front().detail_sync_status == 2");
}

// 合唱专辑的旧 singerId/singer 以竖线一一对应，不得形成复合身份端点。
void testDelimitedAlbumArtists()
{
    migu_parser::MiguParser parser;
    const auto albums = parser.parseAlbumSources(toBytes(R"({"resource":[{"albumId":"duet-album","resourceType":"2003","title":"合唱专辑","singerId":"artist-one| artist-two ","singer":"歌手一| 歌手二 ","totalCount":"1","songItems":[{"contentId":"duet-song","songName":"合唱歌曲","artists":[{"id":"artist-one","name":"歌手一"},{"id":"artist-two","name":"歌手二"}]}]}]})"));
    require(albums.size() == 1 && albums[0].artist_refs.size() == 2, "delimited album artists were lost");
    require(albums[0].artist_refs[0].source_artist_id == "artist-one" && albums[0].artist_refs[0].source_artist_name == "歌手一", "first album artist identity or name is wrong");
    require(albums[0].artist_refs[1].source_artist_id == "artist-two" && albums[0].artist_refs[1].source_artist_name == "歌手二", "second album artist identity or name is wrong");
    require(albums[0].artist_refs[0].is_primary && !albums[0].artist_refs[1].is_primary && albums[0].artist_refs[1].artist_order == 2, "delimited album artist order is wrong");
}

// 详情歌手头像必须绑定明确的单个 singerId，不得变成专辑封面或猜给合唱歌手。
void testAlbumSingerImages()
{
    migu_parser::MiguParser parser;
    const auto single = parser.parseAlbumSources(toBytes(R"({"resource":[{"albumId":"album-image","title":"专辑","singerId":"solo","singer":"独立歌手","singerImgs":[{"imgSizeType":"02","img":"//example.test/singer.webp"}],"imgItems":[{"img":"https://example.test/album.webp"}]}]})"));
    require(single.size() == 1 && single[0].artist_refs.size() == 1, "single singer image fixture failed");
    require(single[0].artist_refs[0].source_artist_image == "https://example.test/singer.webp", "singerImgs was lost or assigned to another owner");
    require(single[0].cover_url == "https://example.test/album.webp", "singer image replaced album cover");
    const auto shared = parser.parseAlbumSources(toBytes(R"({"resource":[{"albumId":"album-shared","title":"合唱专辑","singerId":"one|two","singer":"歌手一|歌手二","singerImgs":[{"img":"https://example.test/ambiguous.webp"}]}]})"));
    require(shared.size() == 1 && shared[0].artist_refs.size() == 2, "shared singer image fixture failed");
    require(shared[0].artist_refs[0].source_artist_image.empty() && shared[0].artist_refs[1].source_artist_image.empty(), "ambiguous image was assigned to an individual singer");
}

} // namespace

int main()
{
    try {
        testParseSingerAlbumList();
        testSingerAlbumPagingUsesTenItemPages();
        testParseDigitalAlbumDetail();
        testParseCatalogAlbumDetail();
        testDelimitedAlbumArtists();
        testAlbumSingerImages();
        std::cout << "MiguAlbumSourceParserTest passed." << std::endl;
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "MiguAlbumSourceParserTest failed: " << e.what() << std::endl;
        return 1;
    }
}
