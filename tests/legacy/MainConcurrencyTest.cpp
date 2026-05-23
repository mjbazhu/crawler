#include "crawler/infrastructure/music/persistence/sqlite/ConnectionPool.h"
#include "crawler/infrastructure/music/persistence/sqlite/SongDao.h"
#include "crawler/infrastructure/music/migu/parser/MiguParser.h"
#include "crawler/infrastructure/music/migu/provider/MiguProvider.h"
#include "crawler/infrastructure/http/client/AsioHttpClient.h"
#include "crawler/infrastructure/execution/threading/ThreadPool.h"
#include <iostream>
#include <memory>
#include <vector>
#include <cassert>
#include <filesystem>
#include <future>
#include <variant>

namespace crawler = orchestrator::modules::crawler;
using crawler::domain::SongList;
using crawler::infrastructure::music::migu::parser::MiguParser;
using crawler::infrastructure::music::persistence::sqlite::ConnectionPool;
using crawler::infrastructure::music::persistence::sqlite::SongDao;
using crawler::infrastructure::music::migu::provider::MiguProvider;

void test_main_concurrency_stability()
{
    std::cout << "Running Main Concurrency Stability Test..." << std::endl;

    const std::string db_path = "test_main_concurrency.db";
    // 确保测试环境干净
    if (std::filesystem::exists(db_path)) {
        std::filesystem::remove(db_path);
    }

    try {
        // 1. 初始化基础设施 (模拟 main.cpp 的流程)
        // 初始化连接池 (4个连接)
        auto pool = std::make_unique<ConnectionPool>(db_path, 4);

        auto client = std::make_shared<crawler::infrastructure::http::client::AsioHttpClient>();
        auto parser = std::make_shared<MiguParser>();

        // 初始化 Provider (无状态/线程安全)
        auto provider = std::make_shared<MiguProvider>(client);

        // 初始化 DAO (无状态，使用连接池)
        SongDao songDao(*pool);

        // 2. 执行并发负载
        // 使用独立的线程池模拟并发用户请求或服务调用
        int concurrency = 4;
        crawler::infrastructure::execution::threading::ThreadPool request_pool(concurrency);
        std::vector<std::future<void>> futures;

        std::cout << "Launching " << concurrency << " concurrent search & save tasks..." << std::endl;

        for (int i = 0; i < concurrency; ++i) {
            futures.push_back(request_pool.enqueue([&provider, &parser, &songDao, i]() {
                // A. 网络请求 (并发 HTTP)
                // 使用一个大概率有结果的关键词
                std::string query = "周杰伦";

                // 同步调试入口内部阻塞等待协程 HTTP 完成，验证 AsioHttpClient 的并发安全
                auto response = provider->fetchSongSearchPageSync(query, 1, 20);
                if (!response) {
                    std::cout << "Thread " << i << ": No response." << std::endl;
                    return;
                }

                SongList list;
                auto results = parser->parse(response->body);
                for (const auto &result : results) {
                    if (std::holds_alternative<crawler::domain::Song>(result.meta_info)) {
                        list.songs.push_back(std::get<crawler::domain::Song>(result.meta_info));
                    }
                }

                if (list.songs.empty()) {
                    std::cout << "Thread " << i << ": No songs found (Network issue or empty)." << std::endl;
                    return;
                }

                // B. 数据库写入 (并发 SQLite)
                int saved_count = 0;
                for (const auto &song : list.songs) {
                    // songDao.save 从连接池获取连接，验证 ConnectionPool 的线程安全
                    if (songDao.save(song)) {
                        saved_count++;
                    }
                }
                // std::cout << "Thread " << i << ": Saved " << saved_count << " songs." << std::endl;
            }));
        }

        // 3. 等待所有任务完成
        for (auto &f : futures) {
            f.get();
        }

        std::cout << "[Pass] All concurrent tasks completed without crash." << std::endl;

    } catch (const std::exception &e) {
        std::cerr << "[Fail] Uncaught exception: " << e.what() << std::endl;
        assert(false);
    }

    // 清理测试数据库
    if (std::filesystem::exists(db_path)) {
        std::filesystem::remove(db_path);
    }
}

int main()
{
    test_main_concurrency_stability();
    return 0;
}
