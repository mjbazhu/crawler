#include "crawler/infrastructure/music/migu/parser/MiguParser.h"

#include <cassert>
#include <iostream>
#include <string>
#include <vector>

namespace crawler = orchestrator::modules::crawler;
namespace migu_parser = crawler::infrastructure::music::migu::parser;

std::vector<char> toBytes(const std::string &text)
{
    return std::vector<char>(text.begin(), text.end());
}

void test_parse_singer_index_sources()
{
    std::cout << "Running Migu singer index parser test..." << std::endl;

    const std::string raw = R"json(
{
    "code": "000000",
    "info": "操作成功",
    "data": {
        "header": {
            "title": "歌手列表",
            "dataVersion": "1779175757192",
            "nextPageNo": 1,
            "nextPageNo2": 1,
            "update": false
        },
        "contents": [
            {
                "view": "ZJ-Singer-Item",
                "viewId": "4002-112",
                "action": "mgmusic://singer-info?id=112",
                "resType": "2002",
                "resId": "112",
                "track": "gsgc_flgs",
                "txt": "周杰伦",
                "txt2": "112",
                "txt3": "热",
                "txt4": "5959566",
                "img": "https://d.musicapp.migu.cn/data/oss/resource/00/5l/4k/73263a79db124bc5b554541813652daa.webp"
            },
            {
                "view": "ZJ-Title",
                "txt": "非歌手卡片"
            }
        ]
    }
}
)json";

    migu_parser::MiguParser parser;
    auto singers = parser.parseSingerSources(toBytes(raw), "huayu-nan");

    assert(singers.size() == 1);
    const auto &singer = singers.front();
    assert(singer.source == "migu");
    assert(singer.name == "周杰伦");
    assert(singer.normalized_name == "周杰伦");
    assert(singer.gender == crawler::domain::Gender::Male);
    assert(singer.source_item_key == "singer:112");
    assert(singer.source_singer_id == "112");
    assert(singer.source_view_id == "4002-112");
    assert(singer.source_res_type == "2002");
    assert(singer.source_action == "mgmusic://singer-info?id=112");
    assert(singer.source_track == "gsgc_flgs");
    assert(singer.source_display_id == "112");
    assert(singer.source_tag == "热");
    assert(singer.source_metric == "5959566");
    assert(singer.source_country == "华语");
    assert(singer.avatar_url == "https://d.musicapp.migu.cn/data/oss/resource/00/5l/4k/73263a79db124bc5b554541813652daa.webp");

    auto female_singers = parser.parseSingerSources(toBytes(raw), "huayu-nv");
    assert(female_singers.size() == 1);
    assert(female_singers.front().gender == crawler::domain::Gender::Female);

    auto invalid_tab_singers = parser.parseSingerSources(toBytes(raw), "huayu-hot");
    assert(invalid_tab_singers.empty());

    std::cout << "Migu singer index parser test passed." << std::endl;
}

int main()
{
    test_parse_singer_index_sources();
    return 0;
}
