#pragma once

#include "crawler/infrastructure/http/client/HttpClient.h"
#include "infrastructure/storage/support/CloudTransferTestSupport.h"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <optional>
#include <utility>

namespace orchestrator::modules::crawler::tests::cloud_transfer {
// 新 RC 流式端口替身，状态由单个测试线程驱动；百度屏障可在另一线程运行。
class RcClient final : public http::HttpClient {
public:
    std::string expected_fs = "archive:Media";                      // 工具配置不代表平台，独立核对 remote/root
    std::string expected_rc_url = "http://127.0.0.1:5572";          // 可复用或逐端覆盖的 RC 地址
    std::string required_authorization;                             // 只比较测试认证，不打印它
    std::uint64_t expected_rate = 0;                                // 冻结的逐任务限速，不修改 RC 全局设置
    bool sha_hash = false;                                          // 支持远端完整 SHA-256 的场景
    char expected_byte = 's';                                       // 大文件按需内容样本
    files::ResourceFileDigest expected;                             // 远端 fixture 内容
    std::map<std::string, bool> objects;                            // 精确相对对象路径
    bool unknown = false, wrong_hash = false, directory = false;    // 元信息失败与冲突
    bool no_hash = false, malformed = false, dropped_reply = false; // 核对降级与响应未知
    bool wrong_readback_type = false;                               // 远端不能用其他算法冒充 SHA-256
    int uploads = 0, probes = 0, readbacks = 0;                     // 内容操作计数
    std::size_t largest_chunk = 0;                                  // 有界供给观察
    std::function<void()> on_upload;                                // 模拟取消或来源变更
    std::optional<http::HttpResponse> get(std::string_view, const std::vector<std::string> &) override
    {
        return {};
    } // 无 GET
    std::optional<http::HttpResponse> post(std::string_view, std::string_view, const std::vector<std::string> &) override
    {
        throw std::runtime_error("aggregated POST used for resource transfer");
    } // 禁止旧 bytes 回退
    AwaitableResponse coGet(std::string, std::vector<std::string>) override
    {
        co_return std::nullopt;
    } // 不使用协程替身
    AwaitableResponse coPost(std::string, std::string, std::vector<std::string>) override
    {
        co_return std::nullopt;
    } // 同上
    // 真正消费按需正文，multipart 只保留小型头尾及正文计数。
    http::HttpUploadResult streamPostWithOptions(std::string_view url, http::HttpUploadBody body, const std::vector<std::string> &headers, const http::HttpRequestOptions &options) override
    {
        require(body.read && body.cancelled && options.request_timeout.count() > 0 && options.max_response_bytes > 0, "RC call lacks independent bounds");
        require(url.starts_with(expected_rc_url + '/'), "RC endpoint was not reused or isolated as configured");
        if (!required_authorization.empty()) require(std::find(headers.begin(), headers.end(), required_authorization) != headers.end(), "existing RC authentication was dropped");
        if (unknown) return {{}, 0, false, "network_failed"};
        const bool uploading = url.find("operations/uploadfile?") != std::string_view::npos;
        std::array<char, 32 * 1024> chunk {};
        std::uint64_t total = 0, payload = 0;
        std::string small, tail, remote;
        bool file_started = false;
        for (;;) {
            if (body.cancelled()) return {{}, total, total > 0, "cancelled"};
            largest_chunk = (std::max)(largest_chunk, chunk.size());
            const auto count = body.read(std::span<char>(chunk));
            if (!count || *count > chunk.size()) return {{}, total, true, "upload_source_failed"};
            if (!*count) break;
            total += *count;
            std::string_view part(chunk.data(), *count);
            if (!uploading) {
                small.append(part);
                require(small.size() <= 16384, "unbounded RPC body");
                continue;
            }
            if (!file_started) {
                small.append(part);
                const auto end = small.find("\r\n\r\n");
                if (end == std::string::npos) {
                    require(small.size() < 4096, "unbounded multipart prefix");
                    continue;
                }
                require(small.find("filename=\"") != std::string::npos, "multipart lacks resource filename");
                file_started = true;
                part = std::string_view(small).substr(end + 4);
            }
            const auto bytes = static_cast<std::size_t>((std::min<std::uint64_t>)(part.size(), expected.size_bytes - payload));
            if (part.substr(0, bytes).find_first_not_of(expected_byte) != std::string_view::npos) return {{}, total, true, "upload_source_failed"};
            payload += bytes;
            tail.append(part.substr(bytes));
            require(tail.size() < 256, "multipart trailer or source is unbounded");
            if (on_upload) {
                auto callback = std::move(on_upload);
                callback();
            }
        }
        require(total == body.size_bytes, "stream content length differs");
        if (uploading) {
            require(file_started && payload == expected.size_bytes && tail.starts_with("\r\n--crawler-") && tail.ends_with("--\r\n"), "multipart not complete");
            const auto query = url.find("&remote=");
            require(query != std::string_view::npos, "RC upload missing destination directory");
            // 测试中路径只需解码 percent 序列，不读取任意 URL。
            const auto decode = [](std::string_view value) {
                std::string result;
                for (std::size_t i = 0; i < value.size(); ++i) {
                    if (value[i] == '%' && i + 2 < value.size()) {
                        result += static_cast<char>(std::stoul(std::string(value.substr(i + 1, 2)), nullptr, 16));
                        i += 2;
                    } else
                        result += value[i];
                }
                return result;
            };
            const auto begin = small.find("filename=\"") + 10;
            const auto filename = small.substr(begin, small.find('"', begin) - begin);
            const auto configuration = url.find("&_config=");
            require(configuration != std::string_view::npos && body.bytes_per_second == expected_rate, "upload dropped independent rate/retry configuration");
            const auto config_begin = configuration + 9;
            const auto call_config = nlohmann::json::parse(decode(url.substr(config_begin, url.find('&', config_begin) - config_begin)));
            require(call_config.at("Retries") == 1 && call_config.at("LowLevelRetries") == 1, "native upload retries exceeded frozen task policy");
            if (expected_rate) require(call_config.at("BwLimitFile") == std::to_string(expected_rate) + 'B', "upload changed global rate instead of per-call file rate");
            remote = decode(url.substr(query + 8)) + '/' + filename;
            objects[remote] = true;
            ++uploads;
            if (dropped_reply) return {{}, total, true, "timeout"};
            return response(nlohmann::json::object(), total);
        }
        const auto request = nlohmann::json::parse(small);
        require(request.at("_config").at("Retries") == 1 && request.at("_config").at("LowLevelRetries") == 1, "RPC retries were not bounded");
        if (expected_rate) require(request.at("_config").at("BwLimitFile") == std::to_string(expected_rate) + 'B', "RPC/readback dropped the per-file bandwidth limit");
        require(request.at("fs") == expected_fs, "RC target changed");
        remote = request.at("remote").get<std::string>();
        require(remote.starts_with("resources/v1/"), "RC escaped controlled object layout");
        if (url.ends_with("operations/stat")) {
            ++probes;
            if (!objects[remote]) return response({{"item", nullptr}}, total);
            nlohmann::json item {{"Size", expected.size_bytes}, {"IsDir", directory}, {"Hashes", nlohmann::json::object()}};
            if (malformed) item["Size"] = "unknown";
            if (!no_hash) item["Hashes"]["MD5"] = wrong_hash ? std::string(32, '0') : expected.md5;
            if (sha_hash) item["Hashes"]["SHA-256"] = wrong_hash ? std::string(64, '0') : expected.sha256;
            return response({{"item", std::move(item)}}, total);
        }
        require(url.ends_with("operations/hashsumfile") && request.at("download") == true && request.at("hashType") == "SHA-256", "repair did not read back exact object SHA-256");
        ++readbacks;
        return response({{"hash", wrong_hash ? std::string(64, '0') : expected.sha256}, {"hashType", wrong_readback_type ? "md5" : "sha256"}}, total);
    }

private:
    // 只构造有界 JSON 响应。
    static http::HttpUploadResult response(nlohmann::json value, std::uint64_t sent)
    {
        const auto text = value.dump();
        http::HttpResponse response;
        response.status_code = 200;
        response.body = std::vector<char>(text.begin(), text.end());
        return {std::move(response), sent, true, "ok"};
    }
};
} // namespace orchestrator::modules::crawler::tests::cloud_transfer
