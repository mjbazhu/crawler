#pragma once

#include <boost/mysql/ssl_mode.hpp>

#include <chrono>
#include <cstddef>
#include <string>

namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql {

// MySQL 连接池配置，只描述基础设施连接参数，不承载业务规则。
struct MySqlConfig {
    std::string host = "127.0.0.1";                                        // MySQL 主机名或 IP
    unsigned short port = 3306;                                            // MySQL TCP 端口
    std::string username = "root";                                         // 登录用户名
    std::string password;                                                  // 登录密码
    std::string database = "crawler";                                      // 默认数据库名，需要提前创建
    int io_threads = 4;                                                    // 内部 Asio I/O 线程数
    std::size_t initial_pool_size = 1;                                     // 启动时预建连接数
    std::size_t max_pool_size = 16;                                        // 最大连接数
    std::chrono::seconds connect_timeout {20};                             // 单次建连超时
    std::chrono::seconds retry_interval {5};                               // 建连失败后的重试间隔
    std::chrono::seconds ping_interval {60};                               // 空闲连接健康检查间隔
    std::chrono::seconds ping_timeout {5};                                 // 健康检查超时
    boost::mysql::ssl_mode ssl_mode = boost::mysql::ssl_mode::disable;     // TLS 策略，默认本地开发关闭
    bool thread_safe = true;                                               // 允许多线程同时从池中获取连接
    bool initialize_schema = true;                                         // 启动时执行 MySQL migration
    std::string migrations_path = "sql/mysql/migrations";                  // MySQL 版本化 migration 目录
    std::string song_upsert_path = "sql/mysql/songs/upsert.sql";           // 歌曲写入 SQL
    std::string song_delete_path = "sql/mysql/songs/delete_by_id.sql";     // 歌曲删除 SQL
    std::string song_find_path = "sql/mysql/songs/find_by_id.sql";         // 歌曲按 ID 查询 SQL
    std::string song_find_all_path = "sql/mysql/songs/find_all.sql";       // 歌曲分页查询 SQL
    std::string album_upsert_path = "sql/mysql/albums/upsert.sql";         // 专辑写入 SQL
    std::string album_delete_path = "sql/mysql/albums/delete_by_id.sql";   // 专辑删除 SQL
    std::string album_find_path = "sql/mysql/albums/find_by_id.sql";       // 专辑按 ID 查询 SQL
    std::string album_find_all_path = "sql/mysql/albums/find_all.sql";     // 专辑分页查询 SQL
    std::string singer_upsert_path = "sql/mysql/singers/upsert.sql";       // 歌手写入 SQL
    std::string singer_delete_path = "sql/mysql/singers/delete_by_id.sql"; // 歌手删除 SQL
    std::string singer_find_path = "sql/mysql/singers/find_by_id.sql";     // 歌手按 ID 查询 SQL
    std::string singer_find_all_path = "sql/mysql/singers/find_all.sql";   // 歌手分页查询 SQL
};

} // namespace orchestrator::modules::crawler::infrastructure::music::persistence::mysql
