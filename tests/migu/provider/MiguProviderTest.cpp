#include "crawler/infrastructure/music/migu/provider/MiguPacmtokenClient.h"
#include "crawler/infrastructure/music/migu/provider/MiguProvider.h"
#include "crawler/infrastructure/music/migu/provider/MiguRuntimePacmtokenStore.h"
#include "mocks/MockHttpClient.h"

#include <algorithm>
#include <cassert>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace crawler = orchestrator::modules::crawler;
using crawler::infrastructure::music::migu::provider::MiguProvider;
using crawler::infrastructure::music::migu::provider::MiguPacmtokenClient;
using crawler::infrastructure::music::migu::provider::MiguRuntimePacmtokenStore;

bool containsHeader(const std::vector<std::string> &headers, const std::string &expected)
{
    return std::find(headers.begin(), headers.end(), expected) != headers.end();
}

// 判断请求头中是否存在指定前缀，用于校验敏感 Cookie 不会从环境变量回退发送。
bool containsHeaderPrefix(const std::vector<std::string> &headers, const std::string &prefix)
{
    return std::any_of(headers.begin(), headers.end(), [&prefix](const std::string &header) {
        return header.rfind(prefix, 0) == 0;
    });
}

void test_crawl_url_parsing()
{
    std::cout << "Running MiguProvider Crawl URL Parsing Test..." << std::endl;

    // 1. 初始化依赖
    auto client = std::make_shared<crawler::infrastructure::http::client::MockHttpClient>();

    MiguProvider provider(client);

    // Case 1: 无效 URL (非 Migu 链接)
    {
        std::cout << "[Test] Invalid URL ... ";
        auto response = provider.fetchUrlSync("http://example.com/invalid");
        // 预期：无法识别 URL 模式，直接请求该 URL，解析失败返回空
        assert(!response);
        std::cout << "Passed" << std::endl;
    }

    // Case 2: 格式错误的 Migu URL (ID 非数字)
    {
        std::cout << "[Test] Malformed Album URL (Non-numeric ID) ... ";
        // ID 提取逻辑会尝试 stoull，失败应捕获异常并返回空
        // 且不应发起网络请求
        auto response = provider.fetchUrlSync("https://music.migu.cn/v3/music/album/abc");
        assert(!response);
        // 验证没有发起请求 (last_request_url 应该是空的或者上一次的值，这里假设是新的 client 实例或重置了)
        // 由于 client 是共享的，我们需要小心。但在本例中，crawl 内部在转换失败时直接返回，不会调用 client->get
        std::cout << "Passed" << std::endl;
    }

    // Case 3: 带参数的 URL (验证 ID 截断逻辑)
    {
        // 注意：这是一个真实存在的专辑 ID，如果网络通畅，可能会返回结果
        // 我们主要验证代码是否崩溃，以及是否能正确处理参数
        std::cout << "[Test] URL with Query Params ... ";
        // 假设 ID 提取逻辑能正确截断 ?from=...
        // 如果 ID 提取错误（包含 ?），stoull 会抛出异常，返回空
        // 如果 ID 提取正确，会发起请求。即使请求失败（网络原因），也不会崩溃。

        // 重置 last_request_url
        client->last_request_url = "";

        // 使用一个不存在的 ID 但格式正确，以避免大量数据下载，主要测试 URL 解析不崩溃
        auto response = provider.fetchUrlSync("https://music.migu.cn/v3/music/album/1126756486?from=share");

        // 验证是否发起了请求，且 URL 中包含正确的 ID (1126756486) 且不包含 ?from=share
        assert(client->last_request_url == "https://app.c.nf.migu.cn/v1.0/content/resourceinfo.do?resourceId=1126756486&resourceType=2003&needSimple=01");
        assert(client->last_request_url.find("1126756486") != std::string::npos);
        assert(client->last_request_url.find("?from=share") == std::string::npos);

        // 只要不崩溃就是通过
        std::cout << "Passed (No crash)" << std::endl;
    }

    // Case 4: 歌曲 URL
    {
        std::cout << "[Test] Song URL ... ";
        client->last_request_url = "";
        auto response = provider.fetchUrlSync("https://music.migu.cn/v3/music/song/60054701934");
        // 同样，主要验证逻辑路径覆盖

        assert(client->last_request_url.find("60054701934") != std::string::npos);
        assert(client->last_request_url.find("resourceType=2") != std::string::npos); // 歌曲类型
        std::cout << "Passed (No crash)" << std::endl;
    }

    std::cout << "MiguProvider Crawl Test Completed." << std::endl;
}

void test_singer_index_provider()
{
    std::cout << "Running MiguProvider Singer Index Test..." << std::endl;

    auto client = std::make_shared<crawler::infrastructure::http::client::MockHttpClient>();
    MiguProvider provider(client);

    {
        std::cout << "[Test] Singer index tab combinations ... ";
        const auto &tabs = MiguProvider::singerIndexTabs();
        assert(tabs.size() == 9);
        assert(std::find(tabs.begin(), tabs.end(), "huayu-nan") != tabs.end());
        assert(std::find(tabs.begin(), tabs.end(), "huayu-nv") != tabs.end());
        assert(std::find(tabs.begin(), tabs.end(), "huayu-group") != tabs.end());
        assert(std::find(tabs.begin(), tabs.end(), "oumei-nan") != tabs.end());
        assert(std::find(tabs.begin(), tabs.end(), "oumei-nv") != tabs.end());
        assert(std::find(tabs.begin(), tabs.end(), "oumei-group") != tabs.end());
        assert(std::find(tabs.begin(), tabs.end(), "rihan-nan") != tabs.end());
        assert(std::find(tabs.begin(), tabs.end(), "rihan-nv") != tabs.end());
        assert(std::find(tabs.begin(), tabs.end(), "rihan-group") != tabs.end());
        std::cout << "Passed" << std::endl;
    }

    {
        std::cout << "[Test] Singer index request URL ... ";
        client->last_request_url.clear();
        auto response = provider.fetchSingerIndexPageSync("huayu-nan");
        (void)response;
        assert(client->last_request_url == "https://app.c.nf.migu.cn/pc/bmw/singer-index/list/v1.0?tab=huayu-nan");
        std::cout << "Passed" << std::endl;
    }

    {
        std::cout << "[Test] Singer index invalid tab ... ";
        client->last_request_url = "unchanged";
        auto response = provider.fetchSingerIndexPageSync("huayu-hot");
        assert(!response);
        assert(client->last_request_url == "unchanged");
        std::cout << "Passed" << std::endl;
    }

    std::cout << "MiguProvider Singer Index Test Completed." << std::endl;
}

void test_song_resource_provider()
{
    std::cout << "Running MiguProvider Song Resource Test..." << std::endl;

    auto client = std::make_shared<crawler::infrastructure::http::client::MockHttpClient>();
    MiguProvider provider(client);

    crawler::application::model::SongResourceFetchRequest request;
    request.content_id = "600902000006889266";
    request.resource_type = "2";
    request.copyright_id = "60054701947";
    request.tone_flag = "ZQ24";

    auto response = provider.fetchSongResourceSync(request);
    (void)response;

    assert(client->last_request_url == "http://app.c.nf.migu.cn/MIGUM3.0/strategy/pc/listen/v1.0?contentId=600902000006889266&netType=01&resourceType=2&copyrightId=60054701947&toneFlag=ZQ24");
    assert(containsHeader(client->last_request_headers, "channel: 0146921"));
    assert(!containsHeaderPrefix(client->last_request_headers, "Cookie:"));

    auto runtime_store = std::make_shared<MiguRuntimePacmtokenStore>();
    runtime_store->updatePacmtoken("runtime-pacmtoken", 123);
    MiguProvider runtime_provider(client, runtime_store);
    client->last_request_headers.clear();
    response = runtime_provider.fetchSongResourceSync(request);
    (void)response;
    assert(containsHeader(client->last_request_headers, "Cookie: pacmtoken=runtime-pacmtoken"));

    client->last_request_url = "unchanged";
    request.copyright_id.clear();
    response = provider.fetchSongResourceSync(request);
    assert(!response);
    assert(client->last_request_url == "unchanged");

    std::cout << "MiguProvider Song Resource Test Completed." << std::endl;
}

void test_pacmtoken_client()
{
    std::cout << "Running MiguProvider Pacmtoken Client Test..." << std::endl;

    auto client = std::make_shared<crawler::infrastructure::http::client::MockHttpClient>();
    client->mock_responses["https://c.musicapp.migu.cn/mgateway/api/checkPacMtoken"] = crawler::infrastructure::http::client::HttpResponse {
        200,
        {'{', '"', 'c', 'o', 'd', 'e', '"', ':', '"', '0', '"', '}'},
        "application/json",
        {{"pacmtoken", "new-pacmtoken"}}
    };

    MiguPacmtokenClient refresher(client);
    const auto result = refresher.refreshPacmtoken("old-pacmtoken");
    assert(result.success);
    assert(result.refreshed);
    assert(result.pacmtoken == "new-pacmtoken");
    assert(result.upstream_status == 200);
    assert(containsHeader(client->last_request_headers, "pacmtoken: old-pacmtoken"));

    std::cout << "MiguProvider Pacmtoken Client Test Completed." << std::endl;
}

void test_singer_album_provider()
{
    std::cout << "Running MiguProvider Singer Album Test..." << std::endl;

    auto client = std::make_shared<crawler::infrastructure::http::client::MockHttpClient>();
    MiguProvider provider(client);

    {
        std::cout << "[Test] Singer album request URL ... ";
        client->last_request_url.clear();
        auto response = provider.fetchSingerAlbumsPageSync(112, 2);
        (void)response;
        assert(client->last_request_url == "https://app.c.nf.migu.cn/pc/bmw/singer/album/v1.0?pageNo=2&singerId=112");
        std::cout << "Passed" << std::endl;
    }

    {
        std::cout << "[Test] Album detail request URL ... ";
        client->last_request_url.clear();
        auto response = provider.fetchAlbumDetailSync("600927015009001083", "1");
        (void)response;
        assert(client->last_request_url == "https://app.c.nf.migu.cn/v1.0/content/resourceinfo.do?resourceId=600927015009001083&resourceType=5&needSimple=01");
        std::cout << "Passed" << std::endl;
    }

    {
        std::cout << "[Test] Normal album detail request URL ... ";
        client->last_request_url.clear();
        auto response = provider.fetchAlbumDetailSync("1141456456", "0");
        (void)response;
        assert(client->last_request_url == "https://app.c.nf.migu.cn/v1.0/content/resourceinfo.do?resourceId=1141456456&resourceType=2003&needSimple=01");
        std::cout << "Passed" << std::endl;
    }

    {
        std::cout << "[Test] Album detail invalid ID ... ";
        client->last_request_url = "unchanged";
        auto response = provider.fetchAlbumDetailSync("abc", "1");
        assert(!response);
        assert(client->last_request_url == "unchanged");
        std::cout << "Passed" << std::endl;
    }
}

int main()
{
    test_crawl_url_parsing();
    test_singer_index_provider();
    test_song_resource_provider();
    test_pacmtoken_client();
    test_singer_album_provider();
    return 0;
}
