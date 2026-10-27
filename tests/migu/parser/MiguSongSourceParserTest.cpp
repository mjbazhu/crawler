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

} // namespace

int main()
{
    try {
        testSingerSongCoverPrefersSongItemImg3();
        testSongAlbumCanonicalIdentity();
        testDelimitedSongArtists();
        std::cout << "MiguSongSourceParserTest passed." << std::endl;
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "MiguSongSourceParserTest failed: " << e.what() << std::endl;
        return 1;
    }
}
