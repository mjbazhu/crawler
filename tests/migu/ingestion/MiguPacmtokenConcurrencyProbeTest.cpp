#include "crawler/application/model/ProviderResponse.h"
#include "crawler/application/model/SongResourceFetch.h"
#include "crawler/infrastructure/http/client/AsioHttpClient.h"
#include "crawler/infrastructure/music/migu/parser/MiguSongResourceParser.h"
#include "crawler/infrastructure/music/migu/provider/MiguPacmtokenClient.h"
#include "crawler/infrastructure/music/migu/provider/MiguRequestFactory.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlConnectionPool.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlValueReader.h"
#include "crawler/infrastructure/platform/Environment.h"
#include "crawler/infrastructure/platform/PlatformUtils.h"
#include "migu/support/MySqlTestSupport.h"

#include <boost/asio/awaitable.hpp>
#include <boost/asio/use_awaitable.hpp>
#include <boost/mysql/results.hpp>
#include <boost/mysql/statement.hpp>

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <exception>
#include <iostream>
#include <map>
#include <memory>
#include <mutex>
#include <set>
#include <stdexcept>
#include <string>
#include <string_view>
#include <thread>
#include <utility>
#include <vector>

namespace crawler = orchestrator::modules::crawler;
namespace model = crawler::application::model;
namespace http_client = crawler::infrastructure::http::client;
namespace migu_parser = crawler::infrastructure::music::migu::parser;
namespace migu_provider = crawler::infrastructure::music::migu::provider;
namespace mysql = crawler::infrastructure::music::persistence::mysql;
namespace platform = crawler::infrastructure::platform;
namespace mysql_support = crawler::tests::migu::support;

namespace {

// listen 探测候选资源，只包含换取 URL 所需稳定参数。
struct ListenCandidate {
    std::uint64_t resource_id = 0; // song_resources.id
    std::string content_id;        // Migu contentId
    std::string resource_type;     // Migu resourceType
    std::string copyright_id;      // Migu copyrightId
    std::string tone_flag;         // Migu toneFlag
};

// 运行期 token 槽位，只输出长度和刷新状态，不输出 token 明文。
struct TokenSlot {
    int index = 0;                  // token 序号
    std::string token;              // 仅内存使用，禁止输出
    std::size_t length = 0;         // token 长度
    bool refreshed = false;         // 是否由 checkPacMtoken 返回
    bool changed_from_previous = false; // 是否不同于上一个 token
    bool duplicate = false;         // 是否和前面 token 重复
    std::string refresh_error;      // 刷新失败时的脱敏错误
};

// listen 并发探测结果。
struct ProbeStats {
    int requested = 0;                  // 实际请求数
    int ok = 0;                         // 成功拿到 URL 数
    std::map<std::string, int> codes;   // 按平台或网络结果码统计
    std::chrono::milliseconds elapsed {0}; // 耗时
};

// 从 Cookie 字符串中提取 pacmtoken；环境变量本身是 token 时原样返回。
std::string normalizePacmtoken(std::string value)
{
    constexpr std::string_view marker = "pacmtoken=";
    const std::size_t marker_pos = value.find(marker);
    if (marker_pos == std::string::npos) {
        return value;
    }

    const std::size_t value_begin = marker_pos + marker.size();
    const std::size_t value_end = value.find(';', value_begin);
    return value.substr(value_begin, value_end == std::string::npos ? std::string::npos : value_end - value_begin);
}

// 读取当前 Unix 秒，用于测试输出中的脱敏时间标记。
std::uint64_t currentUnixSeconds()
{
    return static_cast<std::uint64_t>(std::chrono::duration_cast<std::chrono::seconds>(std::chrono::system_clock::now().time_since_epoch()).count());
}

// 从环境变量读取真实 HTTP 配置。
http_client::AsioHttpClient::Options makeHttpOptionsFromEnv()
{
    http_client::AsioHttpClient::Options options;
    options.io_threads = platform::Environment::getIntOr("CRAWLER_OUTBOUND_HTTP_IO_THREADS", 4);
    options.verify_tls = platform::Environment::getBoolOr("CRAWLER_OUTBOUND_VERIFY_TLS", false);
    options.request_timeout = std::chrono::milliseconds(platform::Environment::getIntOr("CRAWLER_OUTBOUND_HTTP_TIMEOUT_MS", 30000));
    options.max_response_bytes = static_cast<std::size_t>(platform::Environment::getIntOr("CRAWLER_RESOURCE_LISTEN_MAX_RESPONSE_BYTES", 2 * 1024 * 1024));
    return options;
}

// 查询一组音频资源参数，不修改 song_resources 状态。
boost::asio::awaitable<std::vector<ListenCandidate>> loadListenCandidates(
    const std::shared_ptr<mysql::MySqlConnectionPool> &pool,
    std::string format_type,
    int limit
)
{
    std::vector<ListenCandidate> candidates;
    auto connection = co_await pool->coAcquire();
    boost::mysql::statement statement = co_await connection->async_prepare_statement(
        R"SQL(
SELECT
    sr.id,
    COALESCE(r.source_content_id, ''),
    COALESCE(NULLIF(f.resource_type, ''), NULLIF(r.source_resource_type, ''), ''),
    COALESCE(r.copyright_id, ''),
    sr.format_type
FROM song_resources sr
JOIN song_source_refs r ON r.id = sr.song_source_ref_id
LEFT JOIN song_source_audio_formats f ON f.song_source_ref_id = sr.song_source_ref_id
    AND f.format_type = sr.format_type
WHERE r.source = 'migu'
  AND sr.resource_kind = 'audio'
  AND sr.format_type = CONVERT(? USING utf8mb4) COLLATE utf8mb4_unicode_ci
  AND sr.status <> 4
  AND COALESCE(r.source_content_id, '') <> ''
  AND COALESCE(r.copyright_id, '') <> ''
ORDER BY sr.updated_at ASC, sr.id ASC
LIMIT ?
)SQL",
        boost::asio::use_awaitable
    );

    boost::mysql::results result;
    co_await connection->async_execute(statement.bind(std::move(format_type), static_cast<std::uint64_t>((std::max)(1, limit))), result, boost::asio::use_awaitable);
    for (const auto &row : result.rows()) {
        ListenCandidate candidate;
        candidate.resource_id = mysql::MySqlValueReader::uint64At(row, 0);
        candidate.content_id = mysql::MySqlValueReader::stringAt(row, 1);
        candidate.resource_type = mysql::MySqlValueReader::stringAt(row, 2);
        candidate.copyright_id = mysql::MySqlValueReader::stringAt(row, 3);
        candidate.tone_flag = mysql::MySqlValueReader::stringAt(row, 4);
        if (!candidate.content_id.empty() && !candidate.resource_type.empty() && !candidate.copyright_id.empty() && !candidate.tone_flag.empty()) {
            candidates.push_back(std::move(candidate));
        }
    }
    co_return candidates;
}

// 生成多个 token 槽位；失败时保留上一个 token 继续用于对照测试。
std::vector<TokenSlot> collectTokens(const std::shared_ptr<http_client::AsioHttpClient> &client, std::string initial_token, int desired_count)
{
    desired_count = (std::max)(1, desired_count);
    migu_provider::MiguPacmtokenClient refresher(client);
    std::vector<TokenSlot> tokens;
    std::set<std::string> seen;

    TokenSlot first;
    first.index = 1;
    first.token = std::move(initial_token);
    first.length = first.token.size();
    first.duplicate = !seen.insert(first.token).second;
    tokens.push_back(first);

    const int pause_ms = platform::Environment::getIntOr("CRAWLER_TEST_MIGU_TOKEN_REFRESH_PAUSE_MS", 300);
    for (int index = 2; index <= desired_count; ++index) {
        TokenSlot slot;
        slot.index = index;
        const std::string previous = tokens.back().token;
        auto refreshed = refresher.refreshPacmtoken(previous);
        if (refreshed.success && !refreshed.pacmtoken.empty()) {
            slot.token = std::move(refreshed.pacmtoken);
            slot.refreshed = refreshed.refreshed;
        } else {
            slot.token = previous;
            slot.refresh_error = refreshed.error_code.empty() ? refreshed.message : refreshed.error_code;
        }
        slot.length = slot.token.size();
        slot.changed_from_previous = slot.token != previous;
        slot.duplicate = !seen.insert(slot.token).second;
        tokens.push_back(std::move(slot));
        if (pause_ms > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(pause_ms));
        }
    }
    return tokens;
}

// 执行单次 listen 请求并返回稳定结果码。
std::string fetchListenCode(
    const std::shared_ptr<http_client::AsioHttpClient> &client,
    const ListenCandidate &candidate,
    const std::string &token
)
{
    model::SongResourceFetchRequest request;
    request.content_id = candidate.content_id;
    request.resource_type = candidate.resource_type;
    request.copyright_id = candidate.copyright_id;
    request.tone_flag = candidate.tone_flag;

    auto headers = migu_provider::MiguRequestFactory::songResourceListenHeaders("pacmtoken=" + token);
    auto response = client->get(migu_provider::MiguRequestFactory::songResourceListenUrl(request), headers);
    if (!response) {
        return "network_failed";
    }

    migu_parser::MiguSongResourceParser parser;
    auto parsed = parser.parseSongResource(response->body);
    if (parsed.ok && !parsed.url.empty()) {
        return "ok";
    }
    if (!parsed.code.empty()) {
        return parsed.code;
    }
    return response->status_code >= 200 && response->status_code < 300 ? "parse_failed" : "http_status_" + std::to_string(response->status_code);
}

// 并发执行 listen 探测；token_selector 返回每条请求使用的 token。
template <typename TokenSelector>
ProbeStats runProbe(
    const std::shared_ptr<http_client::AsioHttpClient> &client,
    const std::vector<ListenCandidate> &candidates,
    int request_limit,
    int concurrency,
    TokenSelector token_selector
)
{
    ProbeStats stats;
    const int total = (std::min)(static_cast<int>(candidates.size()), (std::max)(1, request_limit));
    concurrency = (std::min)((std::max)(1, concurrency), total);
    stats.requested = total;
    std::atomic<int> next {0};
    std::mutex mutex;
    const auto started = std::chrono::steady_clock::now();

    std::vector<std::thread> workers;
    workers.reserve(static_cast<std::size_t>(concurrency));
    for (int worker = 0; worker < concurrency; ++worker) {
        workers.emplace_back([&, worker] {
            (void)worker;
            for (;;) {
                const int index = next.fetch_add(1);
                if (index >= total) {
                    return;
                }
                const std::string code = fetchListenCode(client, candidates[static_cast<std::size_t>(index)], token_selector(index));
                std::lock_guard<std::mutex> lock(mutex);
                ++stats.codes[code];
                if (code == "ok") {
                    ++stats.ok;
                }
            }
        });
    }
    for (auto &worker : workers) {
        if (worker.joinable()) {
            worker.join();
        }
    }

    stats.elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - started);
    return stats;
}

// 输出统计，不包含 token 或临时 URL。
void printStats(const std::string &label, const ProbeStats &stats)
{
    std::cout << "[Probe] " << label << " requested=" << stats.requested << ", ok=" << stats.ok << ", elapsed_ms=" << stats.elapsed.count();
    for (const auto &[code, count] : stats.codes) {
        std::cout << ", " << code << '=' << count;
    }
    std::cout << std::endl;
}

// 执行 pacmtoken 与 listen 并发限制诊断。
void testMiguPacmtokenConcurrencyProbe()
{
    platform::Environment::loadDotEnvFile(".env.local");
    const mysql::MySqlConfig mysql_config = mysql_support::makeMySqlConfigFromEnv();
    mysql_support::assertDirectMySqlAvailable(mysql_config);
    auto mysql_pool = std::make_shared<mysql::MySqlConnectionPool>(mysql_config);
    mysql_support::assertMySqlAvailable(mysql_pool);

    auto raw_token = platform::Environment::getString("MIGU_PACMTOKEN");
    if (!raw_token || raw_token->empty()) {
        raw_token = platform::Environment::getString("CRAWLER_MIGU_PACMTOKEN");
    }
    if (!raw_token || raw_token->empty()) {
        raw_token = platform::Environment::getString("MIGU_COOKIE");
    }
    if (!raw_token || raw_token->empty()) {
        mysql_pool->cancel();
        throw std::runtime_error("MIGU_PACMTOKEN, CRAWLER_MIGU_PACMTOKEN or MIGU_COOKIE must be configured");
    }

    auto http = std::make_shared<http_client::AsioHttpClient>(makeHttpOptionsFromEnv());
    const int token_count = platform::Environment::getIntOr("CRAWLER_TEST_MIGU_TOKEN_PROBE_TOKENS", 3);
    auto tokens = collectTokens(http, normalizePacmtoken(std::move(*raw_token)), token_count);
    std::cout << "[Check] token_slots=" << tokens.size() << ", now=" << currentUnixSeconds() << std::endl;
    for (const auto &slot : tokens) {
        std::cout << "[Token] index=" << slot.index << ", length=" << slot.length
                  << ", refreshed=" << (slot.refreshed ? "true" : "false")
                  << ", changed_from_previous=" << (slot.changed_from_previous ? "true" : "false")
                  << ", duplicate=" << (slot.duplicate ? "true" : "false")
                  << ", refresh_error=" << slot.refresh_error << std::endl;
    }

    const std::string format = platform::Environment::getStringOr("CRAWLER_TEST_MIGU_TOKEN_PROBE_FORMAT", "PQ");
    const int per_token_limit = platform::Environment::getIntOr("CRAWLER_TEST_MIGU_TOKEN_PROBE_PER_TOKEN_LIMIT", 10);
    const int per_token_concurrency = platform::Environment::getIntOr("CRAWLER_TEST_MIGU_TOKEN_PROBE_PER_TOKEN_CONCURRENCY", 5);
    const int mixed_limit = platform::Environment::getIntOr("CRAWLER_TEST_MIGU_TOKEN_PROBE_MIXED_LIMIT", per_token_limit * static_cast<int>(tokens.size()));
    const int mixed_concurrency = platform::Environment::getIntOr("CRAWLER_TEST_MIGU_TOKEN_PROBE_MIXED_CONCURRENCY", 5);
    const int candidate_limit = (std::max)(mixed_limit, per_token_limit);
    auto candidates = mysql_support::runOnExecutor(mysql_pool->executor(), loadListenCandidates(mysql_pool, format, candidate_limit), "load listen probe candidates");
    if (candidates.empty()) {
        mysql_pool->cancel();
        throw std::runtime_error("no listen probe candidate was loaded from MySQL");
    }

    std::cout << "[Check] candidates=" << candidates.size() << ", format=" << format
              << ", per_token_limit=" << per_token_limit << ", per_token_concurrency=" << per_token_concurrency
              << ", mixed_limit=" << mixed_limit << ", mixed_concurrency=" << mixed_concurrency << std::endl;

    for (const auto &slot : tokens) {
        const auto stats = runProbe(http, candidates, per_token_limit, per_token_concurrency, [&slot](int) -> const std::string & {
            return slot.token;
        });
        printStats("single_token_" + std::to_string(slot.index), stats);
    }

    const auto mixed = runProbe(http, candidates, mixed_limit, mixed_concurrency, [&tokens](int index) -> const std::string & {
        return tokens[static_cast<std::size_t>(index) % tokens.size()].token;
    });
    printStats("round_robin_tokens", mixed);

    mysql_pool->cancel();
}

} // namespace

int main()
{
    try {
        platform::setupConsole();
        testMiguPacmtokenConcurrencyProbe();
        std::cout << "MiguPacmtokenConcurrencyProbeTest passed." << std::endl;
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "MiguPacmtokenConcurrencyProbeTest failed: " << e.what() << std::endl;
        return 1;
    }
}
