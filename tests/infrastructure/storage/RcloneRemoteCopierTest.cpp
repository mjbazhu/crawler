#include "crawler/infrastructure/storage/rclone/RcloneRemoteCopier.h"

#include <nlohmann/json.hpp>

#include <condition_variable>
#include <deque>
#include <exception>
#include <functional>
#include <iostream>
#include <mutex>
#include <optional>
#include <stdexcept>
#include <string>
#include <thread>
#include <utility>
#include <vector>

namespace crawler = orchestrator::modules::crawler;
namespace app_async = crawler::application::async;
namespace model = crawler::application::model;
namespace ports = crawler::application::ports;
namespace http_client = crawler::infrastructure::http::client;
namespace rclone = crawler::infrastructure::storage::rclone;

namespace {

void require(bool condition, const std::string &message)
{
    if (!condition) {
        throw std::runtime_error(message);
    }
}

std::vector<char> bytes(const std::string &text)
{
    return std::vector<char>(text.begin(), text.end());
}

http_client::HttpResponse jsonResponse(std::string body)
{
    http_client::HttpResponse response;
    response.status_code = 200;
    response.content_type = "application/json";
    response.body = bytes(body);
    return response;
}

class ThreadedAsyncRuntime : public ports::AsyncRuntime {
public:
    ~ThreadedAsyncRuntime() override
    {
        for (auto &thread : threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }

    bool spawn(app_async::Task<void> task) override
    {
        task.startDetached();
        return true;
    }

protected:
    void postBlockingCallback(std::function<void()> task) override
    {
        std::lock_guard<std::mutex> lock(mutex_);
        threads_.emplace_back(std::move(task));
    }

private:
    std::mutex mutex_;                 // 保护线程列表
    std::vector<std::thread> threads_; // 阻塞任务线程
};

struct PostRecord {
    std::string url;                  // 请求 URL
    std::string body;                 // 请求体
    std::vector<std::string> headers; // 请求头
};

class FakeHttpClient : public http_client::HttpClient {
public:
    std::deque<std::optional<http_client::HttpResponse>> scripted_responses; // 顺序响应
    std::vector<PostRecord> posts;                                           // POST 调用记录

    std::optional<http_client::HttpResponse> get(std::string_view, const std::vector<std::string> & = {}) override
    {
        return std::nullopt;
    }

    std::optional<http_client::HttpResponse> post(std::string_view url, std::string_view body, const std::vector<std::string> &headers = {}) override
    {
        {
            std::lock_guard<std::mutex> lock(mutex_);
            posts.push_back({std::string(url), std::string(body), headers});
            if (scripted_responses.empty()) {
                return std::nullopt;
            }
            auto response = scripted_responses.front();
            scripted_responses.pop_front();
            return response;
        }
    }

    AwaitableResponse coGet(std::string url, std::vector<std::string> headers = {}) override
    {
        co_return get(url, headers);
    }

    AwaitableResponse coPost(std::string url, std::string body, std::vector<std::string> headers = {}) override
    {
        co_return post(url, body, headers);
    }

private:
    std::mutex mutex_; // 保护响应脚本和记录
};

bool hasHeader(const std::vector<std::string> &headers, const std::string &expected)
{
    for (const auto &header : headers) {
        if (header == expected) {
            return true;
        }
    }
    return false;
}

rclone::RcloneConfig baseConfig()
{
    rclone::RcloneConfig config;
    config.enabled = true;
    config.rc_url = "http://rclone.local:5572/";
    config.rc_user = "user";
    config.rc_password = "pass";
    config.target_fs = "gcloud:Media";
    config.job_poll_interval_ms = 1;
    config.job_timeout_ms = 1000;
    return config;
}

model::SongResourceRemoteCopyRequest baseRequest()
{
    model::SongResourceRemoteCopyRequest request;
    request.source_url = "https://example.test/audio.flac?Key=secret";
    request.relative_path_hint = "migu/songs/1/audio/zq24.flac";
    request.timeout_ms = 1000;
    return request;
}

void testCopyUrlPostsExpectedPayloadAndAuth()
{
    auto runtime = std::make_shared<ThreadedAsyncRuntime>();
    auto http = std::make_shared<FakeHttpClient>();
    http->scripted_responses.push_back(jsonResponse(R"({"jobid":42})"));
    http->scripted_responses.push_back(jsonResponse(R"({"finished":false,"error":""})"));
    http->scripted_responses.push_back(jsonResponse(R"({"finished":true,"error":""})"));
    http->scripted_responses.push_back(jsonResponse(R"({"item":{"Size":12,"Hashes":{"MD5":"abc123"}}})"));

    rclone::RcloneRemoteCopier copier(http, runtime, baseConfig());
    const auto result = runtime->runForTest(copier.copyUrl(baseRequest()));

    require(result.ok, "rclone copy should succeed");
    require(result.local_path == "migu/songs/1/audio/zq24.flac", "rclone result should preserve remote path");
    require(result.size_bytes == 12, "rclone result should use stat size");
    require(result.checksum == "rclone:md5:abc123", "rclone result should expose stat hash");
    require(http->posts.size() == 4, "rclone copier should call copyurl, status twice and stat");

    require(http->posts[0].url == "http://rclone.local:5572/operations/copyurl", "copyurl endpoint should use normalized rc url");
    require(hasHeader(http->posts[0].headers, "Authorization: Basic dXNlcjpwYXNz"), "copyurl should include basic auth header");
    const auto copy_body = nlohmann::json::parse(http->posts[0].body);
    require(copy_body.at("fs") == "gcloud:Media", "copyurl fs should use target fs");
    require(copy_body.at("remote") == "migu/songs/1/audio/zq24.flac", "copyurl remote should use planned path");
    require(copy_body.at("url") == "https://example.test/audio.flac?Key=secret", "copyurl url should use source url");
    require(copy_body.at("autoFilename") == false, "copyurl should disable autoFilename");
    require(copy_body.at("_async") == true, "copyurl should run async");

    const auto status_body = nlohmann::json::parse(http->posts[1].body);
    require(status_body.at("jobid") == 42, "job/status should poll returned jobid");
    const auto stat_body = nlohmann::json::parse(http->posts[3].body);
    require(stat_body.at("fs") == "gcloud:Media", "stat fs should use target fs");
    require(stat_body.at("remote") == "migu/songs/1/audio/zq24.flac", "stat remote should use copied path");
}

void testCopyUrlRedactsJobErrorUrlQuery()
{
    auto runtime = std::make_shared<ThreadedAsyncRuntime>();
    auto http = std::make_shared<FakeHttpClient>();
    http->scripted_responses.push_back(jsonResponse(R"({"jobid":43})"));
    http->scripted_responses.push_back(jsonResponse(R"({"finished":true,"error":"download failed: https://example.test/audio.flac?Key=secret&Tim=1"})"));

    rclone::RcloneRemoteCopier copier(http, runtime, baseConfig());
    const auto result = runtime->runForTest(copier.copyUrl(baseRequest()));

    require(!result.ok, "rclone job error should fail copy");
    require(result.result_code == "rclone_job_failed", "rclone job error should use stable result code");
    require(result.message.find("Key=secret") == std::string::npos, "rclone error message should redact temporary url query");
}

} // namespace

int main()
{
    try {
        testCopyUrlPostsExpectedPayloadAndAuth();
        testCopyUrlRedactsJobErrorUrlQuery();
        std::cout << "RcloneRemoteCopierTest passed." << std::endl;
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "RcloneRemoteCopierTest failed: " << e.what() << std::endl;
        return 1;
    }
}
