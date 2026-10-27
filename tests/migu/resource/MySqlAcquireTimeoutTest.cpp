#include "crawler/infrastructure/music/persistence/mysql/MySqlConnectionPool.h"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/use_future.hpp>
#include <chrono>
#include <future>
#include <iostream>
#include <stdexcept>
#include <vector>

namespace mysql = orchestrator::modules::crawler::infrastructure::music::persistence::mysql;

int main()
{
    try {
        // 保留本地监听端口但不发送 MySQL 握手，测试不连接任何真实数据库。
        boost::asio::io_context local;
        boost::asio::ip::tcp::acceptor stalled(local, {boost::asio::ip::address_v4::loopback(), 0});
        mysql::MySqlConfig config;
        config.port = stalled.local_endpoint().port();
        config.username = "fixture";
        config.database = "fixture";
        config.connect_timeout = std::chrono::seconds(1);
        config.io_threads = 3;
        config.initialize_schema = false;
        auto pool = std::make_shared<mysql::MySqlConnectionPool>(config);
        std::vector<std::future<boost::mysql::pooled_connection>> attempts;
        for (int i = 0; i < 12; ++i) attempts.push_back(boost::asio::co_spawn(pool->executor(), pool->coAcquire(), boost::asio::use_future));
        for (auto &attempt : attempts) {
            if (attempt.wait_for(std::chrono::seconds(5)) != std::future_status::ready) throw std::runtime_error("connection acquisition did not honor deadline");
            bool failed = false;
            try {
                (void)attempt.get();
            } catch (const std::exception &) {
                failed = true;
            }
            if (!failed) throw std::runtime_error("stalled fixture unexpectedly produced a MySQL connection");
        }
        pool->cancel();
        std::cout << "PASS: concurrent pool acquisition respects bounded deadline\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
