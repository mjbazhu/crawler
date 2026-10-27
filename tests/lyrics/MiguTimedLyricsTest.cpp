#include "crawler/infrastructure/music/migu/lyrics/MiguMrcCipher.h"
#include "crawler/infrastructure/music/migu/lyrics/MiguMrcLyricsDecoder.h"
#include "crawler/infrastructure/music/migu/lyrics/MiguMrcTextParser.h"
#include "crawler/infrastructure/music/lyrics/TimedLyricsEncoder.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <fstream>
#include <iostream>
#include <iterator>
#include <stdexcept>
#include <string>

namespace {

namespace crawler = orchestrator::modules::crawler;
namespace mrc = crawler::infrastructure::music::migu::lyrics;

// 测试断言不受 NDEBUG 影响。
void require(bool condition, const std::string &message)
{
    if (!condition) throw std::runtime_error(message);
}

// 只读取人工合成的离线 fixture，不依赖平台或运行期歌曲文件。
std::string fixture(const std::string &name)
{
    std::ifstream stream("tests/lyrics/fixtures/" + name, std::ios::binary);
    require(stream.good(), "fixture should be readable");
    return {std::istreambuf_iterator<char>(stream), std::istreambuf_iterator<char>()};
}

// 固定密文向量同时覆盖 Java 负数右移、中文和 UTF-16 代理项。
void testGoldenCipherAndDecoder()
{
    const auto cipher = fixture("synthetic.mrc");
    const mrc::MiguMrcCipher decoder("test-mrc-key");
    const auto result = decoder.decrypt(cipher);
    require(result.isSuccess(), "golden cipher should decrypt");
    require(*result.data() == fixture("synthetic.expected.mrc"), "plaintext must match independent fixed vector");
    auto uppercase = cipher;
    std::transform(uppercase.begin(), uppercase.end(), uppercase.begin(), [](unsigned char value) { return static_cast<char>(std::toupper(value)); });
    const auto upper = decoder.decrypt("\r\n" + uppercase + "\r\n");
    require(upper.isSuccess() && *upper.data() == *result.data(), "hex case and outer whitespace should not change plaintext");
    const auto lyrics = mrc::MiguMrcLyricsDecoder("test-mrc-key").decode(cipher);
    require(lyrics.isSuccess(), "composed decoder should parse fixture");
    require(lyrics.data()->tags.size() == 3, "duplicate metadata must survive");
    require(lyrics.data()->lines.size() == 2, "zero duration attribution line must survive");
    require(lyrics.data()->lines[1].words[2].text == "😀", "supplementary Unicode character must survive");
    require(lyrics.data()->lines[1].words[0].start_ms == 17100, "word times are absolute, never add line start");
}

// 密文结构和错误密钥必须失败，禁止成功导出不可识别的乱码。
void testMalformedCipher()
{
    const mrc::MiguMrcLyricsDecoder decoder("test-mrc-key");
    require(!decoder.decode("").isSuccess(), "empty cipher must fail");
    require(!decoder.decode(std::string(31, '0')).isSuccess(), "truncated blocks must fail");
    require(!decoder.decode(std::string(32, 'g')).isSuccess(), "non-hex content must fail");
    require(!decoder.decode(std::string(1024 * 1024 + 16, '0')).isSuccess(), "oversized content must fail");
    require(!mrc::MiguMrcLyricsDecoder("").decode(fixture("synthetic.mrc")).isSuccess(), "empty key must fail");
    require(!mrc::MiguMrcLyricsDecoder("wrong-key").decode(fixture("synthetic.mrc")).isSuccess(), "wrong key must fail semantic validation");
}

// 语法失败必须保留错误，尾部遗漏和非法时间不能被静默丢弃。
void testTimingGrammar()
{
    const mrc::MiguMrcTextParser parser;
    const auto value = parser.parse("[10,20]甲(注)(10,10) 乙(20,10)\n");
    require(value.isSuccess(), "literal parentheses should survive");
    require(value.data()->lines[0].words[0].text == "甲(注)", "text parentheses should remain intact");
    require(value.data()->lines[0].words[1].text == " 乙", "leading word space should remain intact");
    for (const std::string invalid : {"[ti:only metadata]\n", "[0,10]甲(0,10)遗漏", "[0,10]甲(-1,10)", "[0,10]甲(0,10", "[0,10]甲(10,1)乙(9,1)", "[9007199254740991,1]甲(0,1)", "[0,0]署名(0,1)遗漏", "[0,0]署名(1,0)遗漏", "[0,0]没有时间片段"}) {
        require(!parser.parse(invalid).isSuccess(), "malformed timing must fail completely");
    }
}

// 平台署名只为前缀标零时间时，保留未单独标时的作者名称，不丢弃整份歌词。
void testUntimedCreditSuffix()
{
    const auto lyrics = mrc::MiguMrcTextParser {}.parse("[0,0]作(0,0)词(0,0)：(0,0)Example Writer\n[10,20]甲(10,20)\n");
    require(lyrics.isSuccess(), "zero-time credit suffix should be retained");
    require(lyrics.data()->lines.size() == 2, "credit and timed lyric should both survive");
    const auto &credit = lyrics.data()->lines[0];
    require(credit.words.size() == 4 && credit.words.back().text == "Example Writer", "untimed author should become a lossless zero-time segment");
    require(credit.words.back().start_ms == 0 && credit.words.back().duration_ms == 0, "credit suffix must keep the explicit zero line timing");
    const auto exported = crawler::infrastructure::music::lyrics::TimedLyricsEncoder {}.encode(*lyrics.data());
    require(exported.isSuccess(), "credit suffix should export to both formats");
    require(nlohmann::json::parse(exported.data()->at(0).content)["lines"][0]["words"][3]["text"] == "Example Writer", "JSON should preserve the complete author name");
    require(exported.data()->at(1).content.find("Example Writer") != std::string::npos, "enhanced LRC should preserve the author name");
}

// JSON 是完整时间数据；LRC 导出须保留毫秒、间隙和独立结束标记。
void testJsonAndEnhancedLrc()
{
    const auto lyrics = mrc::MiguMrcLyricsDecoder("test-mrc-key").decode(fixture("synthetic.mrc"));
    const crawler::infrastructure::music::lyrics::TimedLyricsEncoder encoder;
    const auto encoded = encoder.encode(*lyrics.data());
    require(encoded.isSuccess() && encoded.data()->size() == 2, "both file formats should be exported");
    require(encoded.data()->at(0).filename == "words.json" && encoded.data()->at(1).filename == "words.enhanced.lrc", "export filenames must remain stable without versions or hashes");
    const auto json = nlohmann::json::parse(encoded.data()->at(0).content);
    require(json["schema_version"] == 1 && json["time_base"] == "absolute", "JSON timing contract should be explicit");
    require(json["metadata"].size() == 3, "JSON should preserve repeated tags");
    require(json["lines"][1]["words"][0]["duration_ms"] == 550, "JSON must preserve word duration");
    require(encoded.data()->at(1).content.find("[00:17.100]<00:17.100>甲<00:17.650>乙<00:18.000>😀<00:18.100>") != std::string::npos, "enhanced LRC should use absolute millisecond times");
    auto gap = *lyrics.data();
    gap.lines[1].words[0].duration_ms = 100;
    const auto exported = encoder.encode(gap);
    require(exported.data()->at(1).content.find("甲<00:17.200><00:17.650>乙") != std::string::npos, "word gap should not highlight the previous word");
    gap.lines[1].words[0].text = "<甲>[乙]";
    const auto escaped = encoder.encode(gap);
    require(nlohmann::json::parse(escaped.data()->at(0).content)["lines"][1]["words"][0]["text"] == "<甲>[乙]", "JSON text is lossless");
    require(escaped.data()->at(1).content.find("＜甲＞［乙］") != std::string::npos, "LRC delimiters should not become timing instructions");
}

} // namespace

// 运行纯解码与导出回归，无网络或数据库依赖。
int main()
{
    try {
        testGoldenCipherAndDecoder();
        testMalformedCipher();
        testTimingGrammar();
        testUntimedCreditSuffix();
        testJsonAndEnhancedLrc();
        std::cout << "MiguTimedLyricsTest passed.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "MiguTimedLyricsTest failed: " << error.what() << '\n';
        return 1;
    }
}
