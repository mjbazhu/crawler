#include "SongResourceFileSession.h"
#include "FileWriteSupport.h"

namespace orchestrator::modules::crawler::infrastructure::storage::filesystem {
namespace file_detail {
// 本地文件流式写入会话，先写临时文件，commit 成功后才替换最终路径。
class FileSongResourceBlobWriteSession : public application::ports::SongResourceBlobWriteSession {
public:
    FileSongResourceBlobWriteSession(std::string relative_path, std::filesystem::path full_path, std::filesystem::path temp_path, bool require_ack, std::filesystem::path journal_path, std::filesystem::path previous_path) : relative_path_(std::move(relative_path)),
                                                                                                                                                                                                                               full_path_(std::move(full_path)),
                                                                                                                                                                                                                               temp_path_(std::move(temp_path)),
                                                                                                                                                                                                                               output_(std::make_unique<std::ofstream>(temp_path_, std::ios::binary | std::ios::out | std::ios::trunc))
    {
        require_ack_ = require_ack;
        journal_path_ = std::move(journal_path);
        previous_path_ = std::move(previous_path);
        SHA256_Init(&sha_context_);
    }

    ~FileSongResourceBlobWriteSession() override
    {
        if (!committed_ || (require_ack_ && !accepted_)) {
            abort();
        }
    }

    bool opened() const
    {
        return output_ && output_->is_open();
    }

    // 路径在会话创建时已独占固定，不需要先 commit 才能登记意图。
    std::string plannedPath() const override
    {
        return relative_path_;
    }

    bool write(std::string_view chunk, std::string &error) override
    {
        if (committed_ || aborted_) {
            error = "blob_write_session_closed";
            return false;
        }
        if (!output_ || !output_->is_open()) {
            error = "temp_file_open_failed";
            return false;
        }
        if (chunk.empty()) {
            return true;
        }
        if (chunk.size() > static_cast<std::size_t>((std::numeric_limits<std::streamsize>::max)())) {
            error = "chunk_too_large";
            return false;
        }

        output_->write(chunk.data(), static_cast<std::streamsize>(chunk.size()));
        if (!*output_) {
            error = "file_write_failed";
            return false;
        }
        SHA256_Update(&sha_context_, reinterpret_cast<const unsigned char *>(chunk.data()), chunk.size());
        size_bytes_ += static_cast<std::uint64_t>(chunk.size());
        return true;
    }

    application::model::SongResourceBlobSaveResult commit() override
    {
        application::model::SongResourceBlobSaveResult result;
        if (committed_ || aborted_) {
            result.error = "blob_write_session_closed";
            return result;
        }
        if (size_bytes_ == 0) {
            result.error = "empty_resource_body";
            abort();
            return result;
        }

        output_->flush();
        if (!*output_) {
            result.error = "file_flush_failed";
            abort();
            return result;
        }
        output_->close();
        if (!*output_) {
            result.error = "file_close_failed";
            abort();
            return result;
        }
        output_.reset();

        if (auto rename_error = renameReplacingWithRetry(temp_path_, full_path_); !rename_error.empty()) {
            result.error = std::move(rename_error);
            abort();
            return result;
        }

        std::error_code size_ec;
        const auto saved_size = std::filesystem::file_size(full_path_, size_ec);
        if (size_ec || saved_size != size_bytes_) {
            result.error = "file_size_verify_failed";
            removeQuietly(full_path_);
            committed_ = true;
            return result;
        }

        std::array<unsigned char, SHA256_DIGEST_LENGTH> digest {};
        SHA256_Final(digest.data(), &sha_context_);
        committed_ = true;

        result.ok = true;
        result.local_path = relative_path_;
        result.size_bytes = size_bytes_;
        result.checksum = sha256DigestHex(digest);
        return result;
    }

    // 数据库已持有文件路径后，析构不再删除已提交对象。
    void accept() override
    {
        accepted_ = true;
        if (!previous_path_.empty() && previous_path_ != full_path_) {
            std::error_code error;
            std::filesystem::remove(previous_path_, error);
            if (error) return;
        }
        if (!journal_path_.empty()) removeQuietly(journal_path_);
    }

    // 不确定提交时保留日志与完整对象，启动核对后再决定归属。
    void defer() override
    {
        accepted_ = true;
    }

    void abort() override
    {
        if (aborted_ || accepted_ || (committed_ && !require_ack_)) {
            return;
        }
        aborted_ = true;
        if (output_) {
            if (output_->is_open()) {
                output_->close();
            }
            output_.reset();
        }
        removeQuietly(temp_path_);
        if (committed_ && require_ack_) removeQuietly(full_path_);
        if (!journal_path_.empty()) removeQuietly(journal_path_);
    }

private:
    std::string relative_path_;             // 存储相对路径
    std::filesystem::path full_path_;       // 最终文件路径
    std::filesystem::path temp_path_;       // 临时文件路径
    std::unique_ptr<std::ofstream> output_; // 临时文件输出流，commit 前销毁以释放 Windows 文件句柄
    SHA256_CTX sha_context_ {};             // 流式 SHA-256 上下文
    std::uint64_t size_bytes_ = 0;          // 已写入字节数
    bool committed_ = false;                // 是否已提交到最终路径
    std::filesystem::path journal_path_;    // 数据库确认前保留的文件日志
    std::filesystem::path previous_path_;   // 成功替换后清理的旧文件
    bool require_ack_ = false;              // 是否在数据库确认前保留补偿
    bool accepted_ = false;                 // 数据库是否已确认本次文件
    bool aborted_ = false;                  // 是否已放弃写入
};

std::unique_ptr<application::ports::SongResourceBlobWriteSession> openSongResourceWrite(const std::filesystem::path &root_dir, application::model::SongResourceBlobStreamSaveRequest request)
{
    try {
        if (request.relative_path_hint.empty() || !isSafeRelativePath(request.relative_path_hint)) {
            return nullptr;
        }

        auto full_path = (root_dir / request.relative_path_hint);
        if (request.require_database_ack) {
            const auto unique = makeUniqueTempPath(full_path).filename().string().substr(full_path.filename().string().size() + 5);
            const auto extension = full_path.extension();
            full_path = full_path.parent_path() / (full_path.stem().string() + "." + unique + extension.string());
            request.relative_path_hint = full_path.lexically_relative(root_dir).generic_string();
        }
        if (auto parent = full_path.parent_path(); !std::filesystem::exists(parent)) {
            std::error_code ec;
            std::filesystem::create_directories(parent, ec);
            if (ec) {
                std::cerr << "[FileStorage] Failed to create parent dir: " << parent << " Error: " << ec.message() << std::endl;
                return nullptr;
            }
        }

        const auto temporary = makeUniqueTempPath(full_path);
        std::filesystem::path journal;
        if (request.require_database_ack) {
            const auto directory = root_dir / ".resource-writes";
            std::filesystem::create_directories(directory);
            journal = directory / (full_path.filename().string() + ".pending");
            std::ofstream record(journal, std::ios::binary | std::ios::trunc);
            record << request.resource_id << '\n'
                   << request.claim_generation << '\n'
                   << request.relative_path_hint << '\n'
                   << temporary.lexically_relative(root_dir).generic_string() << '\n'
                   << request.previous_local_path << '\n'
                   << request.owner_kind << '\n';
            record.flush();
            if (!record) {
                removeQuietly(journal);
                return nullptr;
            }
        }
        const auto previous = isSafeRelativePath(request.previous_local_path) ? (root_dir / request.previous_local_path) : std::filesystem::path {};
        auto session = std::make_unique<FileSongResourceBlobWriteSession>(request.relative_path_hint, full_path, temporary, request.require_database_ack, journal, previous);
        if (!session->opened()) {
            return nullptr;
        }
        return session;
    } catch (const std::exception &e) {
        std::cerr << "[FileStorage] Begin stream save exception: " << e.what() << std::endl;
        return nullptr;
    }
}
}
} // namespace orchestrator::modules::crawler::infrastructure::storage::filesystem::file_detail
