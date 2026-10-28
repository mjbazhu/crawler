#pragma once

#include <cstdint>
#include <string>
#include <unordered_map>
#include <vector>

#include "crawler/domain/music/model/Song.h"

namespace orchestrator::modules::crawler::domain {

// 性别
enum class Gender {
    Unknown,
    Male,
    Female,
    Group
};

// gender-country
struct CountryGender {
    std::string country;
    Gender gender;
};

// 搜索组合
static const std::unordered_map<std::string, CountryGender> kTabMap {
    {"huayu-nan", {"华语", Gender::Male}},
    {"huayu-nv", {"华语", Gender::Female}},
    {"huayu-group", {"华语", Gender::Group}},
    {"oumei-nan", {"欧美", Gender::Male}},
    {"oumei-nv", {"欧美", Gender::Female}},
    {"oumei-group", {"欧美", Gender::Group}},
    {"rihan-nan", {"日韩", Gender::Male}},
    {"rihan-nv", {"日韩", Gender::Female}},
    {"rihan-group", {"日韩", Gender::Group}},
};

// 歌手信息
struct Singer {
    int64_t singer_id = 0;
    std::string name;
    std::string artwork;
    std::string country;
    std::string genre; // 歌手类型
    Gender gender = Gender::Unknown; // 0-未知，1-男，2-女，3-组合
    uint64_t created_at = 0;
    uint64_t updated_at = 0;
};

// 歌手列表
struct SingerList {
    std::vector<Singer> singers;
    int total = 0;
    bool isEnd = false;
};

} // namespace orchestrator::modules::crawler::domain
