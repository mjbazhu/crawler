#include "RcloneTransferProtocol.h"

#include <openssl/evp.h>

#include <algorithm>
#include <array>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::storage::rclone::transfer {
namespace {
// 任务内独占 reader，HTTP 连接结束后释放；不共享文件偏移或摘要状态。
class MultipartReader {
public:
    // 输入是应用已经取得租约的不可变快照，不接受 API 文件路径。
    MultipartReader(const model::ResourceContentSnapshot &snapshot, std::string filename, std::string boundary) : file_(std::filesystem::u8path(snapshot.filePath())), input_(file_, std::ios::binary), expected_(snapshot.checksum()), size_(snapshot.sizeBytes()), digest_(EVP_MD_CTX_new(), EVP_MD_CTX_free)
    {
        prefix_ = "--" + boundary + "\r\nContent-Disposition: form-data; name=\"file\"; filename=\"" + filename + "\"\r\nContent-Type: application/octet-stream\r\n\r\n";
        suffix_ = "\r\n--" + boundary + "--\r\n";
        stamp_ = std::filesystem::last_write_time(file_);
        if (!input_ || !digest_ || EVP_DigestInit_ex(digest_.get(), EVP_sha256(), nullptr) != 1) throw std::runtime_error("snapshot stream unavailable");
    }
    // 已知长度使 RC 无需在本机内存中缓冲 multipart。
    std::uint64_t size() const
    {
        return prefix_.size() + size_ + suffix_.size();
    }
    // 返回固定缓冲中的下一段；变化或短读让整个上传保持未决。
    std::optional<std::size_t> read(std::span<char> chunk)
    {
        if (prefix_offset_ < prefix_.size()) return copy(prefix_, prefix_offset_, chunk);
        if (read_ < size_) {
            const auto count = static_cast<std::size_t>((std::min<std::uint64_t>)(chunk.size(), size_ - read_));
            input_.read(chunk.data(), static_cast<std::streamsize>(count));
            if (input_.gcount() != static_cast<std::streamsize>(count) || EVP_DigestUpdate(digest_.get(), chunk.data(), count) != 1) return std::nullopt;
            read_ += count;
            return count;
        }
        if (!verified_) {
            if (input_.peek() != std::char_traits<char>::eof() || std::filesystem::file_size(file_) != size_ || std::filesystem::last_write_time(file_) != stamp_) return std::nullopt;
            std::array<unsigned char, EVP_MAX_MD_SIZE> bytes {};
            unsigned int size = 0;
            if (EVP_DigestFinal_ex(digest_.get(), bytes.data(), &size) != 1 || size != 32) return std::nullopt;
            constexpr char hex[] = "0123456789abcdef";
            std::string value;
            for (unsigned int i = 0; i < size; ++i) {
                value += hex[bytes[i] >> 4];
                value += hex[bytes[i] & 15];
            }
            if (value != expected_) return std::nullopt;
            verified_ = true;
        }
        return copy(suffix_, suffix_offset_, chunk);
    }

private:
    // 每次只复制调用者可容纳的固定片段。
    static std::size_t copy(const std::string &text, std::size_t &offset, std::span<char> chunk)
    {
        const auto count = (std::min)(chunk.size(), text.size() - offset);
        std::memcpy(chunk.data(), text.data() + offset, count);
        offset += count;
        return count;
    }
    std::filesystem::path file_;                                     // 不可变快照文件
    std::ifstream input_;                                            // 独占顺序读句柄
    std::string expected_;                                           // 读取期间重新核对 SHA-256
    std::uint64_t size_ = 0;                                         // 预期完整大小
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> digest_; // 本次流摘要
    std::filesystem::file_time_type stamp_;                          // 读取中变更检测
    std::string prefix_, suffix_;                                    // 有界 multipart 元数据
    std::size_t prefix_offset_ = 0, suffix_offset_ = 0;              // 各段游标
    std::uint64_t read_ = 0;                                         // 已读取正文量
    bool verified_ = false;                                          // 发 multipart 结束符前完成校验
};
} // namespace

http::client::HttpUploadBody multipart(const model::ResourceContentSnapshot &snapshot, std::string filename, std::string boundary, const model::ResourceTransferContext &context, std::uint64_t bytes_per_second)
{
    auto reader = std::make_shared<MultipartReader>(snapshot, std::move(filename), std::move(boundary));
    http::client::HttpUploadBody body;
    body.size_bytes = reader->size();
    body.cancelled = [context] { return context.stopped(); };
    body.bytes_per_second = bytes_per_second;
    body.read = [reader](std::span<char> chunk) { return reader->read(chunk); };
    return body;
}

} // namespace orchestrator::modules::crawler::infrastructure::storage::rclone::transfer
