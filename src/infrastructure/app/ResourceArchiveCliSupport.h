#pragma once
#include "crawler/application/model/ResourceDiscovery.h"
#include "crawler/application/result/ServiceResult.h"
#include "crawler/application/ports/storage/ResourceDiscoveryProbe.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceDiscovery.h"
#include <charconv>
#include <initializer_list>
#include <map>
#include <memory>
#include <stdexcept>
#include <string>
#include <string_view>
#include <nlohmann/json.hpp>
namespace orchestrator::modules::crawler::infrastructure::app::archive_cli {
using Options = std::map<std::string, std::string>;
namespace model = application::model;
// 拒绝未知选项，不回显用户输入。
inline void allowed(const Options &args, std::initializer_list<std::string_view> names)
{
    for (const auto &[key, value] : args) {
        bool found = false;
        for (auto name : names)
            if (key == name) found = true;
        if (!found) throw std::invalid_argument("unsupported archive option");
    }
}
// 读取非空必填值。
inline const std::string &required(const Options &args, const char *key)
{
    const auto found = args.find(key);
    if (found == args.end() || found->second.empty()) throw std::invalid_argument("missing archive option");
    return found->second;
}
// 整数必须完整解析且不得溢出。
inline std::uint64_t number(const std::string &value)
{
    std::uint64_t result = 0;
    const auto parsed = std::from_chars(value.data(), value.data() + value.size(), result);
    if (value.empty() || parsed.ec != std::errc {} || parsed.ptr != value.data() + value.size()) throw std::invalid_argument("invalid unsigned archive option");
    return result;
}
// 转成窄整数前先检查上限。
inline int bounded(const Options &args, const char *key, int fallback, int maximum)
{
    if (!args.contains(key)) return fallback;
    const auto value = number(args.at(key));
    if (!value || value > static_cast<std::uint64_t>(maximum)) throw std::invalid_argument("archive option exceeds bound");
    return static_cast<int>(value);
}
// 仓储只返回固定脱敏诊断。
template <class T>
T get(application::result::ServiceResult<T> value)
{
    if (!value.isSuccess()) throw std::invalid_argument(value.message());
    return *value.data();
}
// 不变范围及检查点的报告表示，不含认证。
nlohmann::json runJson(const model::ResourceDiscoveryRun &run);
// 独占创建报告目录，冻结已提交游标，分页流式导出。
int exportReport(music::persistence::mysql::MySqlResourceDiscovery &runs, const model::ResourceDiscoveryRun &run, const std::string &directory);
// 复用服务配置与真实身份核验，仅暴露元数据读取。
std::unique_ptr<application::ports::ResourceDiscoveryProbe> probe(model::ResourceDestination destination);
} // namespace orchestrator::modules::crawler::infrastructure::app::archive_cli
