#pragma once

#include "crawler/infrastructure/music/migu/parser/strategy/IParserStrategy.h"

namespace orchestrator::modules::crawler::infrastructure::music::migu::parser {
namespace strategy {

// 策略 A: 解析 songResultData 结构 (歌单/榜单)
class MiguSongListStrategy : public IParserStrategy {
public:
    bool parse(const nlohmann::json &root, std::vector<domain::ParseResult> &results) const override;
};

// 策略 B: 解析直接的歌曲列表结构 (搜索结果)
class MiguSearchStrategy : public IParserStrategy {
public:
    bool parse(const nlohmann::json &root, std::vector<domain::ParseResult> &results) const override;
};

// 策略 C: 解析歌手歌曲 Tab 的 data -> contents -> contents -> songItem 结构
class MiguSingerSongStrategy : public IParserStrategy {
public:
    bool parse(const nlohmann::json &root, std::vector<domain::ParseResult> &results) const override;
};

// 策略 D: 解析 resource -> songItems 结构 (专辑详情)
class MiguAlbumDetailStrategy : public IParserStrategy {
public:
    bool parse(const nlohmann::json &root, std::vector<domain::ParseResult> &results) const override;
};

// 策略 E: 解析歌手列表
class MiguSingerListStrategy : public IParserStrategy {
public:
    bool parse(const nlohmann::json &root, std::vector<domain::ParseResult> &results) const override;
};

// 策略 F: 解析 albumResultData 结构 (专辑列表)
class MiguAlbumListStrategy : public IParserStrategy {
public:
    bool parse(const nlohmann::json &root, std::vector<domain::ParseResult> &results) const override;
};

// 策略 G: 解析data-> result 结构 (专辑列表)
class MiguAlbumListSearchStrategy : public IParserStrategy {
public:
    bool parse(const nlohmann::json &root, std::vector<domain::ParseResult> &results) const override;
};

}
} // namespace orchestrator::modules::crawler::infrastructure::music::migu::parser::strategy
