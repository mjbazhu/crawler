#pragma once

#include <zlib.h>

#include <cstddef>
#include <string>
#include <string_view>

namespace orchestrator::modules::crawler::infrastructure::http::client::detail {

// 请求独占的内容解码器；线程不安全。编码前后分别限长，禁止压缩内容绕过响应上限。
class HttpContentDecoder {
public:
    // 每个 HTTP 响应使用独立解码状态，不共享 zlib 流。
    HttpContentDecoder() = default;
    // 释放已经初始化的 zlib 状态。
    ~HttpContentDecoder();
    HttpContentDecoder(const HttpContentDecoder &) = delete;
    HttpContentDecoder &operator=(const HttpContentDecoder &) = delete;

    // 仅接受 identity 或 gzip；未知编码保留明确失败语义。
    bool configure(std::string encoding, std::size_t max_bytes);
    // 解码一个完整传输分片；输出仅在当前调用期间由调用方持有。
    bool decode(std::string_view input, std::string &output);
    // 响应结束时必须验证 gzip 校验和及结束标志，防止截断内容被提交。
    bool finish();
    // Content-Length 对应传输字节数，与下游接收的解压字节数分开。
    std::size_t wireBytes() const;
    // 返回稳定错误码，调用方负责映射和日志。
    const std::string &errorCode() const;

private:
    // 统一记录解码或限长失败。
    bool fail(std::string code);

    z_stream stream_ {};            // 本响应独占的 zlib 状态
    std::size_t max_bytes_ = 0;     // 编码前后共同上限
    std::size_t wire_bytes_ = 0;    // 已消费传输字节
    std::size_t decoded_bytes_ = 0; // 已生成内容字节
    std::string error_code_;        // 首次失败诊断
    bool configured_ = false;       // 是否已经解析成功响应的编码
    bool gzip_ = false;             // 是否需要 gzip 解码
    bool initialized_ = false;      // zlib 是否初始化成功
    bool ended_ = false;            // 是否读到完整 gzip 结束标志
};

} // namespace orchestrator::modules::crawler::infrastructure::http::client::detail
