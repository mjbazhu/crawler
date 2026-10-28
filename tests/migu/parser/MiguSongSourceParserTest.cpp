#include "crawler/infrastructure/music/migu/parser/MiguParser.h"

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

// 将 JSON 文本转换为原始响应字节。
std::vector<char> toBytes(const std::string &text)
{
    return std::vector<char>(text.begin(), text.end());
}

// 歌曲封面和歌手拼音均取自当前歌曲响应。
void testSingerSongCoverPrefersSongItemImg3()
{
    const std::string raw = R"({
        "code": "000000",
        "data": {
            "contents": [{
                "contents": [{
                    "view": "ZJ-Singer-Song-Item",
                    "viewId": "4001-1140708908",
                    "action": "mgmusic://song-player?id=600929000002040577",
                    "resType": "4001",
                    "resId": "600929000002040577",
                    "img": "/data/oss/resource/low-wrapper.webp",
                    "txt": "Cover Source Priority",
                    "txt2": "Test Artist",
                    "txt3": "Test Album",
                    "songItem": {
                        "resourceType": "2",
                        "contentId": "600929000002040577",
                        "songId": "1140708908",
                        "songName": "Cover Source Priority",
                        "albumId": "1140708907",
                        "album": "Test Album",
                        "img1": "/data/oss/resource/low-img1.webp",
                        "img2": "/data/oss/resource/middle-img2.webp",
                        "img3": "/data/oss/resource/high-img3.webp",
                        "singerList": [{"id": "18196", "name": "Test Artist", "nameSpelling": "testartist"}],
                        "audioFormats": [{"resourceType": "2", "formatType": "HQ"}]
                    }
                }]
            }]
        }
    })";

    migu_parser::MiguParser parser;
    const auto songs = parser.parseSongSources(toBytes(raw));
    require(songs.size() == 1, "songs.size() == 1");
    require(songs.front().cover_url == "https://d.musicapp.migu.cn/data/oss/resource/high-img3.webp", "song cover should prefer songItem.img3 and add Migu prefix");
    require(songs.front().artist_refs.size() == 1, "song artist refs should be parsed");
    require(songs.front().artist_refs.front().source_artist_name_pinyin == "testartist", "song artist nameSpelling should be parsed for singer pinyin backfill");
}

// 数字专辑目录身份与物料身份分开，普通专辑和缺少 dalbumId 的响应保持兼容。
void testSongAlbumCanonicalIdentity()
{
    migu_parser::MiguParser parser;
    const auto songs = parser.parseSongSources(toBytes(R"({"data":{"contents":[{"contents":[
        {"songItem":{"contentId":"digital-song","songName":"数字单曲","album":"数字专辑","albumId":"material-id","dalbumId":"digital-id","albumPinyin":"shuzizhuanji"}},
        {"songItem":{"contentId":"catalog-song","songName":"普通单曲","album":"普通专辑","albumId":"catalog-id"}},
        {"songItem":{"contentId":"empty-digital-song","songName":"空数字身份","album":"普通专辑","albumId":"catalog-id","dalbumId":""}}
    ]}]}})"));
    require(songs.size() == 3, "album identity fixture did not parse all songs");
    require(songs[0].source_album_id == "digital-id", "digital album identity did not use dalbumId");
    require(songs[0].source_album_material_id == "material-id", "digital album material ID was lost");
    require(songs[0].source_album_pinyin == "shuzizhuanji", "canonical digital album lost its pinyin");
    require(songs[1].source_album_id == "catalog-id" && songs[2].source_album_id == "catalog-id", "ordinary album fallback changed");
}

// 单曲兼容旧分隔格式，数组仍优先；无法对应的姓名不猜测归属。
void testDelimitedSongArtists()
{
    migu_parser::MiguParser parser;
    const auto songs = parser.parseSongSources(toBytes(R"([
        {"contentId":"legacy-duet","songName":"旧合唱","singerId":"one|two","singer":"歌手一|歌手二"},
        {"contentId":"array-duet","songName":"数组合唱","singerList":[],"artists":[{"id":"real","name":"真实歌手"}],"singerId":"one|two","singer":"歌手一|歌手二"},
        {"contentId":"unknown-names","songName":"无法对应姓名","singerId":"one|two","singer":"合唱显示名"}
    ])"));
    require(songs.size() == 3, "delimited song fixture did not parse");
    require(songs[0].artist_refs.size() == 2 && songs[0].source_primary_artist_id == "one", "delimited song ID became a compound singer endpoint");
    require(songs[0].artist_refs[1].source_artist_name == "歌手二" && songs[0].artist_refs[1].artist_order == 2, "delimited song artist order or name was lost");
    require(songs[1].artist_refs.size() == 1 && songs[1].artist_refs[0].source_artist_id == "real", "artist array did not override the delimited fallback");
    require(songs[2].artist_refs.size() == 2 && songs[2].artist_refs[0].source_artist_name.empty() && songs[2].artist_refs[1].source_artist_name.empty(), "ambiguous display name was assigned to an individual artist");
}

// 共享占位值不能成为歌曲身份；没有真实歌曲或内容 ID 的条目不入库。
void testPlaceholderContentIdentity()
{
    migu_parser::MiguParser parser;
    const auto songs = parser.parseSongSources(toBytes(R"([
        {"songId":"1140823207","contentId":"600000000000000000","songName":"Players And Losers"},
        {"songId":"1140328044","contentId":"600000000000000000","songName":"Man I Am"},
        {"songId":"1140420018","contentId":"600000000000000000","songName":"Orion"},
        {"contentId":"600000000000000000","songName":"Missing identity"},
        {"contentId":"600000000000000000","refId":"valid-content","songName":"Valid fallback"},
        {"resId":"600000000000000000","action":"mgmusic://song-player?id=600000000000000000","songItem":{"songId":"wrapper-song","contentId":"600000000000000000","songName":"Wrapper placeholder"}},
        {"contentId":"ordinary-content","songName":"Ordinary song"}
    ])"));
    require(songs.size() == 6, "placeholder-only identity was accepted or a real song was lost");
    require(songs[0].source_song_id == "1140823207" && songs[1].source_song_id == "1140328044" && songs[2].source_song_id == "1140420018", "distinct placeholder songs lost their real song IDs");
    require(songs[0].source_content_id.empty() && songs[1].source_content_id.empty() && songs[2].source_content_id.empty() && songs[4].source_content_id.empty(), "shared placeholder survived parsing");
    require(songs[3].source_content_id == "valid-content" && songs[5].source_content_id == "ordinary-content", "valid content IDs or fallbacks were discarded");
}

// i18 第 2 页的实测字段：歌曲身份和专辑都存在，singerList 为空且没有其他歌手字段。
void testAlbumTrackMissingArtist()
{
    migu_parser::MiguParser parser;
    const auto page = parser.parseAlbumSongPage(toBytes(R"json({"code":"000000","data":{"totalCount":80,"songList":[
        {"contentId":"600913000001316566","songId":"1114037273","songName":"围城(i18版)","songPinyin":"weicheng","albumId":"1114072945","album":"i18","singerList":[]},
        {"contentId":"artist-control","songId":"artist-control-id","songName":"有明确歌手的对照曲目","albumId":"1114072945","album":"i18","singerList":[{"id":"explicit-singer","name":"明确歌手"}]}
    ]}})json"));
    require(page.songs.size() == 2 && page.source_song_total == 80, "missing-artist track lost its source or album context");
    const auto &song = page.songs.front();
    require(song.source_song_id == "1114037273" && song.source_album_id == "1114072945" && song.source_album_name == "i18", "i18 track identity or album was discarded");
    require(song.artist_refs.empty() && song.source_primary_artist_id.empty(), "missing upstream artist was replaced by a guessed identity");
    require(page.songs.back().artist_refs.size() == 1 && page.songs.back().artist_refs.front().source_artist_id == "explicit-singer", "explicit artist was dropped with its missing-artist neighbor");
}

} // namespace

int main()
{
    try {
        testSingerSongCoverPrefersSongItemImg3();
        testSongAlbumCanonicalIdentity();
        testDelimitedSongArtists();
        testPlaceholderContentIdentity();
        testAlbumTrackMissingArtist();
        std::cout << "MiguSongSourceParserTest passed." << std::endl;
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "MiguSongSourceParserTest failed: " << e.what() << std::endl;
        return 1;
    }
}
