#include "crawler/infrastructure/music/migu/parser/MiguSongResourceParser.h"

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

void testParseSuccess()
{
    const std::string raw = R"({
        "code": "000000",
        "info": "操作成功",
        "data": {
            "version": "4ba63effd7089dea33d95d1b174617e8",
            "url": "https://freetyst.nf.migu.cn/public/product9th/product43/product/24bit/2021/04/10/60054701947/60054701947.flac",
            "audioFormatType": "ZQ24",
            "coverUrl": "/data/oss/resource/00/1y/hd/low-cover.webp",
            "img3": "/data/oss/resource/00/1y/hd/cover.webp",
            "lrcUrl": "https://d.musicapp.migu.cn/data/oss/resource/00/1y/hd/y5",
            "mrcUrl": "https://d.musicapp.migu.cn/data/oss/resource/00/1y/hd/y6",
            "trcUrl": "https://d.musicapp.migu.cn/data/oss/resource/00/1y/hd/y7"
        }
    })";

    migu_parser::MiguSongResourceParser parser;
    const auto result = parser.parseSongResource(toBytes(raw));
    require(result.ok, "resource response should be ok");
    require(result.code == "000000", "resource code");
    require(result.message == "操作成功", "resource message");
    require(result.url.find("60054701947.flac") != std::string::npos, "resource url");
    require(result.version == "4ba63effd7089dea33d95d1b174617e8", "resource version");
    require(result.audio_format_type == "ZQ24", "resource audio format type");
    require(result.cover_url == "https://d.musicapp.migu.cn/data/oss/resource/00/1y/hd/cover.webp", "resource cover url should be normalized");
    require(result.lyric_url == "https://d.musicapp.migu.cn/data/oss/resource/00/1y/hd/y5", "resource lrc url");
    require(result.mrc_url == "https://d.musicapp.migu.cn/data/oss/resource/00/1y/hd/y6", "resource mrc url");
    require(result.trc_url == "https://d.musicapp.migu.cn/data/oss/resource/00/1y/hd/y7", "resource trc url");
}

void testParseNestedSongCoverPriority()
{
    const std::string raw = R"({
        "code": "000000",
        "info": "操作成功",
        "data": {
            "version": "v1",
            "url": "https://example.test/audio.flac",
            "audioFormatType": "PQ",
            "img2": "/data/oss/resource/top-middle.webp",
            "song": {
                "img1": "/data/oss/resource/song-low.webp",
                "img2": "/data/oss/resource/song-middle.webp",
                "img3": "/data/oss/resource/song-high.webp"
            }
        }
    })";

    migu_parser::MiguSongResourceParser parser;
    const auto result = parser.parseSongResource(toBytes(raw));
    require(result.ok, "nested song cover response should be ok");
    require(result.cover_url == "https://d.musicapp.migu.cn/data/oss/resource/song-high.webp", "nested song img3 should win over any img2");
}

void testParseNonSuccessCode()
{
    const std::string raw = R"({
        "code": "100001",
        "info": "resource denied",
        "data": {
            "url": "https://example.invalid/audio.flac",
            "audioFormatType": "ZQ24"
        }
    })";

    migu_parser::MiguSongResourceParser parser;
    const auto result = parser.parseSongResource(toBytes(raw));
    require(!result.ok, "non-success code should not be ok");
    require(result.code == "100001", "non-success code should be preserved");
    require(result.message == "resource denied", "non-success message should be preserved");
    require(result.url == "https://example.invalid/audio.flac", "non-success url should be preserved for diagnostics");
}

void testParseMissingUrl()
{
    const std::string raw = R"({
        "code": "000000",
        "info": "操作成功",
        "data": {
            "audioFormatType": "ZQ24"
        }
    })";

    migu_parser::MiguSongResourceParser parser;
    const auto result = parser.parseSongResource(toBytes(raw));
    require(!result.ok, "missing url should not be ok");
    require(result.code == "000000", "missing url code should be preserved");
    require(result.audio_format_type == "ZQ24", "missing url audio format should be parsed");
}

void testParseCannotCode()
{
    const std::string raw = R"({
        "code": "000000",
        "info": "操作成功",
        "data": {
            "dialogInfo": {
                "text": "会员专属音质，请先登录"
            },
            "cannotCode": "440022",
            "freeListenType": "0"
        }
    })";

    migu_parser::MiguSongResourceParser parser;
    const auto result = parser.parseSongResource(toBytes(raw));
    require(!result.ok, "cannotCode response should not be ok");
    require(result.code == "440022", "cannotCode should become result code");
    require(result.message == "会员专属音质，请先登录", "dialog text should become result message");
}

void testParseCannotCodeKeepsRelatedTextUrls()
{
    const std::string raw = R"({
        "code": "000000",
        "info": "操作成功",
        "data": {
            "dialogInfo": {
                "text": "数字专辑需要购买后下载"
            },
            "cannotCode": "440016",
            "lrcUrl": "https://d.musicapp.migu.cn/data/oss/resource/00/1y/hd/y5",
            "mrcUrl": "https://d.musicapp.migu.cn/data/oss/resource/00/1y/hd/y6",
            "trcUrl": "https://d.musicapp.migu.cn/data/oss/resource/00/1y/hd/y7"
        }
    })";

    migu_parser::MiguSongResourceParser parser;
    const auto result = parser.parseSongResource(toBytes(raw));
    require(!result.ok, "cannotCode without audio url should not be ok");
    require(result.code == "440016", "cannotCode should become result code");
    require(result.lyric_url == "https://d.musicapp.migu.cn/data/oss/resource/00/1y/hd/y5", "cannotCode lrc url should be preserved");
    require(result.mrc_url == "https://d.musicapp.migu.cn/data/oss/resource/00/1y/hd/y6", "cannotCode mrc url should be preserved");
    require(result.trc_url == "https://d.musicapp.migu.cn/data/oss/resource/00/1y/hd/y7", "cannotCode trc url should be preserved");
}

void testParseMissingData()
{
    const std::string raw = R"({
        "code": "000000",
        "info": "操作成功"
    })";

    migu_parser::MiguSongResourceParser parser;
    const auto result = parser.parseSongResource(toBytes(raw));
    require(!result.ok, "missing data should not be ok");
    require(result.code == "000000", "missing data code should be preserved");
}

void testParseInvalidJson()
{
    migu_parser::MiguSongResourceParser parser;
    const auto result = parser.parseSongResource(toBytes("{invalid-json"));
    require(!result.ok, "invalid json should not be ok");
    require(result.code.empty(), "invalid json code should be empty");
}

} // namespace

int main()
{
    try {
        testParseSuccess();
        testParseNestedSongCoverPriority();
        testParseNonSuccessCode();
        testParseMissingUrl();
        testParseCannotCode();
        testParseCannotCodeKeepsRelatedTextUrls();
        testParseMissingData();
        testParseInvalidJson();
        std::cout << "MiguSongResourceParserTest passed." << std::endl;
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "MiguSongResourceParserTest failed: " << e.what() << std::endl;
        return 1;
    }
}
