#pragma once

#include "crawler/api/routing/CrawlerRoutes.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <regex>
#include <set>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace orchestrator::modules::crawler::tests::api_contract {

using Operations = std::set<std::pair<std::string, std::string>>;

// 展开四类补池的公共取值，并消除文档参数名与路由序号的表达差异。
inline std::vector<std::string> expandedPaths(const std::string &path)
{
    const std::string prefix = "/api/migu/backfill/";
    if (path.starts_with(prefix)) {
        const auto end = path.find('/', prefix.size());
        const auto kind = path.substr(prefix.size(), end - prefix.size());
        if (!kind.empty() && kind.front() == '{') {
            std::vector<std::string> paths;
            for (const auto *name : {"singer-songs", "singer-albums", "album-details", "album-songs"}) paths.push_back(prefix + name + path.substr(end));
            return paths;
        }
    }
    return {std::regex_replace(path, std::regex(R"(\{[^}]+\})"), "{}")};
}

// 只收集注册结果，不执行 controller、网络或数据库操作。
class RouteInventory final : public api::routing::RouteSink {
public:
    Operations operations;                           // 展开补池类型后的业务方法/路径
    nlohmann::json routes = nlohmann::json::array(); // 包括 CORS 的原始注册记录

    // OPTIONS 作为公共预检机制单独记录，不要求成为 OpenAPI 业务操作。
    void route(const std::string &method, const std::string &path, api::routing::RequestHandler) override
    {
        routes.push_back({{"method", method}, {"path", path}});
        if (method == "OPTIONS") return;
        auto lower = method;
        std::transform(lower.begin(), lower.end(), lower.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
        for (const auto &expanded : expandedPaths(path)) operations.emplace(lower, expanded);
    }
};

// 所有本地 schema 引用必须有定义，防止路由存在而协议无法解析。
inline void verifyReferences(const nlohmann::json &node, const nlohmann::json &document)
{
    if (node.is_object() && node.contains("$ref")) {
        const auto ref = node.at("$ref").get<std::string>();
        if (ref.starts_with("#/") && !document.contains(nlohmann::json::json_pointer(ref.substr(1)))) throw std::runtime_error("unresolved OpenAPI reference: " + ref);
    }
    if (node.is_structured())
        for (const auto &child : node) verifyReferences(child, document);
}

// 双向核对实际注册与运行期文档，兼容通用补池路径和具体类型路径。
inline void verifyRegisteredRoutes(const nlohmann::json &document)
{
    RouteInventory registered;
    api::routing::CrawlerRoutes(nullptr).registerRoutes(registered);
    Operations documented;
    const std::set<std::string> methods {"get", "post", "put", "patch", "delete", "head", "trace"};
    const std::regex parameter_pattern(R"(\{([^}]+)\})");
    for (const auto &[path, value] : document.at("paths").items()) {
        for (const auto &[method, operation] : value.items()) {
            if (!methods.contains(method)) continue;
            for (const auto &expanded : expandedPaths(path)) documented.emplace(method, expanded);
            // 每个模板参数都必须在本操作或路径级声明为 required path parameter。
            for (std::sregex_iterator it(path.begin(), path.end(), parameter_pattern), end; it != end; ++it) {
                const auto name = (*it)[1].str();
                bool found = false;
                for (const auto *scope : {&value, &operation}) {
                    if (!scope->contains("parameters")) continue;
                    for (const auto &parameter : scope->at("parameters")) {
                        if (parameter.value("name", "") == name && parameter.value("in", "") == "path" && parameter.value("required", false)) found = true;
                    }
                }
                if (!found) throw std::runtime_error("missing OpenAPI path parameter: " + path + " / " + name);
            }
        }
    }
    for (const auto &[method, path] : registered.operations)
        if (!documented.contains({method, path})) throw std::runtime_error("route missing from OpenAPI: " + method + " " + path);
    for (const auto &[method, path] : documented)
        if (!registered.operations.contains({method, path})) throw std::runtime_error("OpenAPI operation has no registered route: " + method + " " + path);
    verifyReferences(document, document);
    if (const char *output = std::getenv("CRAWLER_TEST_ROUTES_OUTPUT"); output && *output) {
        std::ofstream file(output, std::ios::binary);
        file << registered.routes.dump(2);
        if (!file) throw std::runtime_error("could not export route inventory");
    }
}

} // namespace orchestrator::modules::crawler::tests::api_contract
