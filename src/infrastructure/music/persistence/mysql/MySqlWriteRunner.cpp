#include "crawler/infrastructure/music/persistence/mysql/MySqlWriteRunner.h"

#include <boost/asio/co_spawn.hpp>
#include <boost/asio/use_future.hpp>

#include <future>
#include <iostream>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {

bool MySqlWriteRunner::waitForWrite(boost::asio::any_io_executor executor, boost::asio::awaitable<bool> task, std::string_view component, std::string_view context)
{
    auto future = boost::asio::co_spawn(std::move(executor), std::move(task), boost::asio::use_future);
    try {
        return future.get();
    } catch (const std::exception &e) {
        std::cerr << "[" << component << "] " << context << " failed: " << e.what() << std::endl;
    } catch (...) {
        std::cerr << "[" << component << "] " << context << " failed with unknown error" << std::endl;
    }
    return false;
}

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
