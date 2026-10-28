#pragma once
#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>
#include <string>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/use_future.hpp>
#include <exception>
#include <future>
#include <iostream>
#include <string_view>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {
namespace resource_detail {
// 在同步 port 边界阻塞等待 MySQL 查询完成，避免把 Asio 暴露到 application。
template <typename T>
T waitForMysqlResult(boost::asio::any_io_executor executor, boost::asio::awaitable<T> task, std::string_view component, std::string_view context, T fallback)
{
    std::future<T> future = boost::asio::co_spawn(std::move(executor), std::move(task), boost::asio::use_future);
    try {
        return future.get();
    } catch (const std::exception &e) {
        std::cerr << "[" << component << "] " << context << " failed: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "[" << component << "] " << context << " failed with unknown error" << std::endl;
    }
    return fallback;
}

// 记录资源队列异常。
inline void logResourceQueueError(const std::string &context, const std::exception &error)
{
    std::cerr << "[MySqlSongResourceQueue] " << context << " failed: " << error.what() << std::endl;
}

}
} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql::resource_detail
