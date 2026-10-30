#pragma once

#include "crawler/application/model/SongResourceFile.h"

#include <memory>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace orchestrator::modules::crawler::application::ports {

// 歌曲资源 blob 写入会话；实现必须保证 commit 前不会暴露半文件或半对象。
class SongResourceBlobWriteSession {
public:
    virtual ~SongResourceBlobWriteSession() = default;

    // 新编排在正式发布前登记实际独占路径；旧替身返回空表示不支持该能力。
    virtual std::string plannedPath() const
    {
        return {};
    }

    // 写入一个二进制分片；失败时返回 false 并填充脱敏错误。
    virtual bool write(std::string_view chunk, std::string &error) = 0;

    // 提交完整对象，成功时返回可回写数据库的路径、大小和校验值。
    virtual application::model::SongResourceBlobSaveResult commit() = 0;

    // 数据库已确认本次文件，解除补偿；普通独立保存可使用默认实现。
    virtual void accept()
    {}

    // 数据库暂时不可用时保留文件待核对，禁止误删可能已提交的对象。
    virtual void defer()
    {
        accept();
    }

    // 放弃写入并清理临时对象；需要数据库确认的会话还须清理未确认文件。
    virtual void abort() = 0;
};

// 歌曲资源二进制存储 port，隔离本地文件和后续 API/对象存储实现。
class SongResourceBlobStore {
public:
    virtual ~SongResourceBlobStore() = default;

    // 保存资源字节，成功时返回 worker 可回写的路径、大小和校验值。
    virtual application::model::SongResourceBlobSaveResult saveSongResourceBlob(application::model::SongResourceBlobSaveRequest request) = 0;

    // 打开流式写入会话；默认实现会在内存中聚合分片以兼容旧存储替身。
    virtual std::unique_ptr<SongResourceBlobWriteSession> beginSongResourceBlobWrite(application::model::SongResourceBlobStreamSaveRequest request);
};

namespace detail {

// 兼容旧 BlobStore 实现的内存聚合写入会话，只用于未覆盖流式能力的测试替身。
class BufferedSongResourceBlobWriteSession : public SongResourceBlobWriteSession {
public:
    BufferedSongResourceBlobWriteSession(SongResourceBlobStore &owner, application::model::SongResourceBlobStreamSaveRequest request) : owner_(owner),
                                                                                                                                        request_(std::move(request))
    {}

    bool write(std::string_view chunk, std::string &error) override
    {
        if (aborted_ || committed_) {
            error = "blob_write_session_closed";
            return false;
        }
        data_.insert(data_.end(), chunk.begin(), chunk.end());
        return true;
    }

    application::model::SongResourceBlobSaveResult commit() override
    {
        application::model::SongResourceBlobSaveResult result;
        if (aborted_ || committed_) {
            result.error = "blob_write_session_closed";
            return result;
        }
        committed_ = true;

        application::model::SongResourceBlobSaveRequest save_request;
        save_request.relative_path_hint = std::move(request_.relative_path_hint);
        save_request.content_type = std::move(request_.content_type);
        save_request.data = std::move(data_);
        return owner_.saveSongResourceBlob(std::move(save_request));
    }

    void abort() override
    {
        aborted_ = true;
        data_.clear();
    }

private:
    SongResourceBlobStore &owner_;                                  // 旧保存 port
    application::model::SongResourceBlobStreamSaveRequest request_; // 保存目标
    std::vector<char> data_;                                        // 兼容模式下的临时聚合字节
    bool committed_ = false;                                        // 是否已提交
    bool aborted_ = false;                                          // 是否已放弃
};

} // namespace detail

inline std::unique_ptr<SongResourceBlobWriteSession> SongResourceBlobStore::beginSongResourceBlobWrite(application::model::SongResourceBlobStreamSaveRequest request)
{
    return std::make_unique<detail::BufferedSongResourceBlobWriteSession>(*this, std::move(request));
}

} // namespace orchestrator::modules::crawler::application::ports
