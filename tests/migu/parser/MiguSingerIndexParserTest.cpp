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
    assert(singer.source_singer_id == "112");
    assert(singer.source_tag == "热");
    assert(singer.source_metric == "5959566");
    assert(singer.source_country == "华语");
    assert(singer.avatar_url == "https://d.musicapp.migu.cn/data/oss/resource/00/5l/4k/73263a79db124bc5b554541813652daa.webp");
    assert(singer.source_index_tab == "huayu-nan");
    assert(singer.source_index_order == 1);

    auto female_singers = parser.parseSingerSources(toBytes(raw), "huayu-nv");
    assert(female_singers.size() == 1);
    assert(female_singers.front().gender == crawler::domain::Gender::Female);
    assert(female_singers.front().source_index_tab == "huayu-nv");

    auto invalid_tab_singers = parser.parseSingerSources(toBytes(raw), "huayu-hot");
    assert(invalid_tab_singers.empty());

    std::cout << "Migu singer index parser test passed." << std::endl;
}

void test_parse_singer_profile_counts()
{
    std::cout << "Running Migu singer profile count parser test..." << std::endl;

    const std::string raw = R"json(
{
    "code": "000000",
    "data": {
        "contents": [
            {
                "view": "ZJ-SingerDetail-Scroll",
                "contents": [
                    {
                        "view": "ZJ-SingerDetail-Item",
                        "action": "mgmusic://singer-detail-self-revealing-wall?id=112",
                        "txt": "周杰伦"
                    }
                ]
            },
            {
                "view": "ZJ-Tab-Scroll",
                "contents": [
                    {"view": "ZJ-Tab-Item", "action": "song", "txt": "歌曲", "txt2": "410"},
                    {"view": "ZJ-Tab-Item", "action": "mv", "txt": "视频", "txt2": "2479"},
                    {"view": "ZJ-Tab-Item", "action": "album", "txt": "专辑", "txt2": "41"}
                ]
            }
        ]
    }
}
)json";

    migu_parser::MiguParser parser;
    auto profile = parser.parseSingerSourceProfile(toBytes(raw));

    assert(profile.source == "migu");
    assert(profile.source_singer_id == "112");
    assert(profile.source_song_total && *profile.source_song_total == 410);
    assert(profile.source_album_total && *profile.source_album_total == 41);

    std::cout << "Migu singer profile count parser test passed." << std::endl;
}

void test_parse_singer_profile_prefers_action_id()
{
    std::cout << "Running Migu singer profile action id parser test..." << std::endl;

    const std::string raw = R"json(
{
    "code": "000000",
    "data": {
        "contents": [
            {
                "view": "ZJ-SingerDetail-Scroll",
                "contents": [
                    {
                        "view": "ZJ-SingerDetail-Item",
                        "action": "mgmusic://singer-detail-self-revealing-wall?id=18196",
                        "txt": "许嵩",
                        "txt2": "1",
                        "txt4": "340150",
                        "txt5": "7039"
                    }
                ]
            },
            {
                "view": "ZJ-Tab-Scroll",
                "contents": [
                    {"view": "ZJ-Tab-Item", "action": "index", "txt": "主页", "txt2": "0"},
                    {"view": "ZJ-Tab-Item", "action": "song", "txt": "歌曲", "txt2": "263"},
                    {"view": "ZJ-Tab-Item", "action": "mv", "txt": "视频", "txt2": "215"},
                    {"view": "ZJ-Tab-Item", "action": "album", "txt": "专辑", "txt2": "63"}
                ]
            }
        ]
    }
}
)json";

    migu_parser::MiguParser parser;
    auto profile = parser.parseSingerSourceProfile(toBytes(raw));

    assert(profile.source == "migu");
    assert(profile.source_singer_id == "18196");
    assert(profile.source_song_total && *profile.source_song_total == 263);
    assert(profile.source_album_total && *profile.source_album_total == 63);

    std::cout << "Migu singer profile action id parser test passed." << std::endl;
}

void test_parse_singer_profile_missing_tabs_as_zero()
{
    std::cout << "Running Migu singer profile missing tab parser test..." << std::endl;

    migu_parser::MiguParser parser;

    const std::string only_album_raw = R"json(
{
    "code": "000000",
    "data": {
        "contents": [
            {
                "view": "ZJ-SingerDetail-Scroll",
                "contents": [
                    {
                        "view": "ZJ-SingerDetail-Item",
                        "action": "mgmusic://singer-detail-self-revealing-wall?id=1104711463",
                        "txt": "最好的时光三重奏"
                    }
                ]
            },
            {
                "view": "ZJ-Tab-Scroll",
                "contents": [
                    {"view": "ZJ-Tab-Item", "action": "index", "txt": "主页", "txt2": "0"},
                    {"view": "ZJ-Tab-Item", "action": "album", "txt": "专辑", "txt2": "4"}
                ]
            }
        ]
    }
}
)json";

    auto only_album_profile = parser.parseSingerSourceProfile(toBytes(only_album_raw));
    assert(only_album_profile.source_singer_id == "1104711463");
    assert(only_album_profile.source_song_total && *only_album_profile.source_song_total == 0);
    assert(only_album_profile.source_album_total && *only_album_profile.source_album_total == 4);

    const std::string only_song_raw = R"json(
{
    "code": "000000",
    "data": {
        "contents": [
            {
                "view": "ZJ-SingerDetail-Scroll",
                "contents": [
                    {
                        "view": "ZJ-SingerDetail-Item",
                        "action": "mgmusic://singer-detail-self-revealing-wall?id=1001",
                        "txt": "只有歌曲的歌手"
                    }
                ]
            },
            {
                "view": "ZJ-Tab-Scroll",
                "contents": [
                    {"view": "ZJ-Tab-Item", "action": "song", "txt": "歌曲", "txt2": "7"}
                ]
            }
        ]
    }
}
)json";

    auto only_song_profile = parser.parseSingerSourceProfile(toBytes(only_song_raw));
    assert(only_song_profile.source_singer_id == "1001");
    assert(only_song_profile.source_song_total && *only_song_profile.source_song_total == 7);
    assert(only_song_profile.source_album_total && *only_song_profile.source_album_total == 0);

    const std::string no_song_or_album_raw = R"json(
{
    "code": "000000",
    "data": {
        "contents": [
            {
                "view": "ZJ-SingerDetail-Scroll",
                "contents": [
                    {
                        "view": "ZJ-SingerDetail-Item",
                        "action": "mgmusic://singer-detail-self-revealing-wall?id=1002",
                        "txt": "无作品歌手"
                    }
                ]
            },
            {
                "view": "ZJ-Tab-Scroll",
                "contents": [
                    {"view": "ZJ-Tab-Item", "action": "index", "txt": "主页", "txt2": "0"},
                    {"view": "ZJ-Tab-Item", "action": "mv", "txt": "视频", "txt2": "0"}
                ]
            }
        ]
    }
}
)json";

    auto empty_profile = parser.parseSingerSourceProfile(toBytes(no_song_or_album_raw));
    assert(empty_profile.source_singer_id == "1002");
    assert(empty_profile.source_song_total && *empty_profile.source_song_total == 0);
    assert(empty_profile.source_album_total && *empty_profile.source_album_total == 0);

    std::cout << "Migu singer profile missing tab parser test passed." << std::endl;
}

int main()
{
    test_parse_singer_index_sources();
    test_parse_singer_profile_counts();
    test_parse_singer_profile_prefers_action_id();
    test_parse_singer_profile_missing_tabs_as_zero();
    return 0;
}
