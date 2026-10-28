#pragma once

#include "crawler/application/async/Task.h"
#include "crawler/application/model/SongResourceFile.h"

#include <memory>
#include <span>
#include <string>

namespace orchestrator::modules::crawler::application::ports {

// 异步 blob 写入结果，表达单个分片写入是否成功。
struct AsyncBlobWriteResult {
    bool ok = false;   // 分片是否写入成功
    std::string error; // 脱敏错误说明
};

// 异步歌曲资源 blob 写入会话；实现必须保证 commit 前不暴露半文件或半对象。
class AsyncBlobWriteSession {
public:
    virtual ~AsyncBlobWriteSession() = default;

    // 异步写入一个分片；慢存储必须在实现中隔离到 blocking/storage executor。
    virtual application::async::Task<AsyncBlobWriteResult> write(std::span<const char> chunk) = 0;

    // 提交完整对象，成功后返回可回写数据库的路径、大小和校验值。
    virtual application::async::Task<application::model::SongResourceBlobSaveResult> commit() = 0;

    // 数据库确认后解除文件补偿，兼容非事务存储替身。
    virtual application::async::Task<void> accept()
    {
        co_return;
    }

    // 无法确认数据库提交时保留文件待核对。
    virtual application::async::Task<void> defer()
    {
        co_await accept();
    }

    // 放弃写入并清理临时对象，以及本会话已提交但未被数据库确认的文件。
    virtual application::async::Task<void> abort() = 0;
};

// 异步歌曲资源二进制存储 port，固定 staging/commit/abort 语义以便后续替换 rclone/API。
class AsyncSongResourceBlobStore {
public:
    virtual ~AsyncSongResourceBlobStore() = default;

    // 打开异步写入会话。
    virtual application::async::Task<std::unique_ptr<AsyncBlobWriteSession>> open(application::model::SongResourceBlobStreamSaveRequest request) = 0;
};

} // namespace orchestrator::modules::crawler::application::ports
