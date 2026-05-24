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
                        "singerList": [{"id": "18196", "name": "Test Artist"}],
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
}

} // namespace

int main()
{
    try {
        testSingerSongCoverPrefersSongItemImg3();
        std::cout << "MiguSongSourceParserTest passed." << std::endl;
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "MiguSongSourceParserTest failed: " << e.what() << std::endl;
        return 1;
    }
}
