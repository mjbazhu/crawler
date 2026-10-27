#include "crawler/infrastructure/http/client/HttpContentDecoder.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <limits>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::http::client::detail {

HttpContentDecoder::~HttpContentDecoder()
{
    if (initialized_) inflateEnd(&stream_);
}

bool HttpContentDecoder::configure(std::string encoding, std::size_t max_bytes)
{
    if (configured_) return fail("invalid_content_encoding");
    configured_ = true;
    max_bytes_ = max_bytes;
    std::transform(encoding.begin(), encoding.end(), encoding.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    if (encoding.empty() || encoding == "identity") return true;
    if (encoding != "gzip") return fail("unsupported_content_encoding");
    gzip_ = true;
    initialized_ = inflateInit2(&stream_, MAX_WBITS + 16) == Z_OK;
    return initialized_ || fail("invalid_content_encoding");
}

bool HttpContentDecoder::decode(std::string_view input, std::string &output)
{
    output.clear();
    if (!error_code_.empty()) return false;
    if (!configured_) return fail("invalid_content_encoding");
    if (input.size() > max_bytes_ - wire_bytes_) return fail("body_too_large");
    wire_bytes_ += input.size();
    if (!gzip_) {
        output.assign(input);
        decoded_bytes_ += input.size();
        return true;
    }
    if (input.empty()) return true;
    if (ended_ || input.size() > (std::numeric_limits<uInt>::max)()) return fail("invalid_content_encoding");
    stream_.next_in = reinterpret_cast<Bytef *>(const_cast<char *>(input.data()));
    stream_.avail_in = static_cast<uInt>(input.size());
    std::array<char, 16384> buffer {};
    for (;;) {
        stream_.next_out = reinterpret_cast<Bytef *>(buffer.data());
        stream_.avail_out = static_cast<uInt>(buffer.size());
        const auto code = inflate(&stream_, Z_NO_FLUSH);
        const auto produced = buffer.size() - stream_.avail_out;
        if (produced > max_bytes_ - decoded_bytes_) return fail("body_too_large");
        decoded_bytes_ += produced;
        output.append(buffer.data(), produced);
        if (code == Z_STREAM_END) {
            ended_ = true;
            return stream_.avail_in == 0 || fail("invalid_content_encoding");
        }
        if (code != Z_OK && code != Z_BUF_ERROR) return fail("invalid_content_encoding");
        if (stream_.avail_in == 0 && stream_.avail_out != 0) return true;
        if (code == Z_BUF_ERROR && produced == 0) return fail("invalid_content_encoding");
    }
}

bool HttpContentDecoder::finish()
{
    if (!error_code_.empty()) return false;
    return !gzip_ || ended_ || fail("invalid_content_encoding");
}

std::size_t HttpContentDecoder::wireBytes() const
{
    return wire_bytes_;
}

const std::string &HttpContentDecoder::errorCode() const
{
    return error_code_;
}

bool HttpContentDecoder::fail(std::string code)
{
    if (error_code_.empty()) error_code_ = std::move(code);
    return false;
}

} // namespace orchestrator::modules::crawler::infrastructure::http::client::detail
