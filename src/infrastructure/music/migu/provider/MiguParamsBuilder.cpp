#include "crawler/infrastructure/music/migu/provider/MiguParamsBuilder.h"

#include <cctype>
#include <iomanip>
#include <sstream>

namespace orchestrator::modules::crawler::infrastructure::music::migu::provider {

namespace {

// 将查询参数值按 RFC 3986 百分号编码，避免 URL escape 依赖出站 HTTP 库。
std::string percentEncode(const std::string &value)
{
    std::ostringstream encoded;
    encoded << std::uppercase << std::hex << std::setfill('0');

    for (unsigned char ch : value) {
        if (std::isalnum(ch) || ch == '-' || ch == '_' || ch == '.' || ch == '~') {
            encoded << static_cast<char>(ch);
            continue;
        }
        encoded << '%' << std::setw(2) << static_cast<int>(ch);
    }

    return encoded.str();
}

} // namespace

std::string toQueryString(const Params &p)
{
    std::ostringstream oss;
    bool first = true;

    auto append = [&](const std::string &key, uint64_t value) {
        if (!first)
            oss << "&";
        first = false;
        oss << key << "=" << value;
    };
    auto appendEncoded = [&](const std::string &key, const std::string &value) {
        if (!first)
            oss << "&";
        first = false;
        oss << key << "=" << percentEncode(value);
    };

    if (p.contentId)
        appendEncoded("contentId", *p.contentId);
    if (p.netType)
        appendEncoded("netType", *p.netType);
    if (p.text)
        appendEncoded("text", *p.text);
    if (p.pageNo)
        append("pageNo", *p.pageNo);
    if (p.pageSize)
        append("pageSize", *p.pageSize);
    if (p.searchSwitch)
        appendEncoded("searchSwitch", *p.searchSwitch);
    if (p.resourceId)
        append("resourceId", *p.resourceId);
    if (p.resourceType)
        append("resourceType", *p.resourceType);
    else if (p.resourceTypeText)
        appendEncoded("resourceType", *p.resourceTypeText);
    if (p.copyrightId)
        appendEncoded("copyrightId", *p.copyrightId);
    if (p.toneFlag)
        appendEncoded("toneFlag", *p.toneFlag);
    if (p.singerId)
        append("singerId", *p.singerId);
    if (p.type)
        append("type", *p.type);
    if (p.tab)
        appendEncoded("tab", *p.tab);
    if (p.needSimple)
        appendEncoded("needSimple", *p.needSimple);

    return oss.str();
}

} // namespace orchestrator::modules::crawler::infrastructure::music::migu::provider
