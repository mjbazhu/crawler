#pragma once

#include <boost/asio/any_io_executor.hpp>
#include <boost/asio/awaitable.hpp>

#include <string_view>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {

// MySQL 同步 port 桥接器，在基础设施边界等待协程结果并记录上下文。
class MySqlWriteRunner {
public:
    // 阻塞等待 bool 写入协程完成；失败时记录组件和操作名。
    static bool waitForWrite(boost::asio::any_io_executor executor, boost::asio::awaitable<bool> task, std::string_view component, std::string_view context);
};

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
