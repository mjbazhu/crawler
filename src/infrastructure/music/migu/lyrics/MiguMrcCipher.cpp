#include "crawler/infrastructure/music/migu/lyrics/MiguMrcCipher.h"

#include <charconv>
#include <cstddef>
#include <vector>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::music::migu::lyrics {
namespace {

using Result = application::result::ServiceResult<std::string>;
using Error = application::result::ServiceErrorType;

// 显式模拟 Java long 算术右移；全部运算采用无符号回绕，避免有符号溢出。
std::uint64_t arithmeticRight(std::uint64_t value, unsigned shift)
{
    return (value >> shift) | ((value >> 63) ? (~std::uint64_t {0} << (64 - shift)) : 0);
}

// 64 位 MRC 混合步骤，不可替换为普通的 32 位 XXTEA。
std::uint64_t mix(std::uint64_t z, std::uint64_t y, std::uint64_t sum, std::size_t position, const std::array<std::uint64_t, 4> &key)
{
    const auto index = (position & 3U) ^ ((sum >> 2) & 3U);
    return ((y ^ sum) + (z ^ key[index])) ^ ((arithmeticRight(z, 5) ^ (y << 2)) + (arithmeticRight(y, 3) ^ (z << 4)));
}

// 将一个已验证的 Unicode 标量编码为 UTF-8，不依赖平台 locale。
void appendUtf8(std::string &output, std::uint32_t value)
{
    if (value <= 0x7f) {
        output.push_back(static_cast<char>(value));
    } else if (value <= 0x7ff) {
        output.push_back(static_cast<char>(0xc0 | (value >> 6)));
        output.push_back(static_cast<char>(0x80 | (value & 0x3f)));
    } else if (value <= 0xffff) {
        output.push_back(static_cast<char>(0xe0 | (value >> 12)));
        output.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (value & 0x3f)));
    } else {
        output.push_back(static_cast<char>(0xf0 | (value >> 18)));
        output.push_back(static_cast<char>(0x80 | ((value >> 12) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | ((value >> 6) & 0x3f)));
        output.push_back(static_cast<char>(0x80 | (value & 0x3f)));
    }
}

// 严格解码 UTF-16LE，错误密钥造成的非法代理项不得静默替换为乱码。
Result decodeText(const std::vector<std::uint64_t> &blocks)
{
    std::vector<unsigned char> bytes;
    bytes.reserve(blocks.size() * 8);
    for (const auto block : blocks) {
        for (unsigned shift = 0; shift < 64; shift += 8) bytes.push_back(static_cast<unsigned char>(block >> shift));
    }
    while (!bytes.empty() && bytes.back() == 0x30) bytes.pop_back();
    if (bytes.size() % 2 != 0) return Result::failure(Error::InvalidParam, "mrc_invalid_utf16");
    // 某些来源以 UTF-16 NUL 补齐；只移除结尾，正文中的 NUL 必须报错。
    while (bytes.size() >= 2 && bytes[bytes.size() - 1] == 0 && bytes[bytes.size() - 2] == 0) bytes.resize(bytes.size() - 2);
    const auto unit = [&](std::size_t offset) { return static_cast<std::uint32_t>(bytes[offset]) | (static_cast<std::uint32_t>(bytes[offset + 1]) << 8); };
    std::string output;
    for (std::size_t offset = 0; offset < bytes.size(); offset += 2) {
        auto value = unit(offset);
        if (value >= 0xd800 && value <= 0xdbff) {
            if (offset + 3 >= bytes.size()) return Result::failure(Error::InvalidParam, "mrc_invalid_utf16");
            const auto next = unit(offset + 2);
            if (next < 0xdc00 || next > 0xdfff) return Result::failure(Error::InvalidParam, "mrc_invalid_utf16");
            value = 0x10000 + ((value - 0xd800) << 10) + next - 0xdc00;
            offset += 2;
        } else if ((value >= 0xdc00 && value <= 0xdfff) || value == 0) {
            return Result::failure(Error::InvalidParam, "mrc_invalid_utf16");
        }
        if (offset == 0 && value == 0xfeff) continue;
        appendUtf8(output, value);
    }
    if (output.empty()) return Result::failure(Error::InvalidParam, "mrc_empty_plaintext");
    return Result::success(std::move(output));
}

} // namespace

MiguMrcCipher::MiguMrcCipher(std::string_view key)
{
    valid_key_ = !key.empty() && key.size() <= 256;
    for (const unsigned char value : key) valid_key_ = valid_key_ && value > 0 && value < 128;
    for (std::size_t index = 0; index < 16; ++index) {
        const auto value = index < key.size() ? static_cast<unsigned char>(key[index]) : static_cast<unsigned char>('0');
        key_[index / 4] |= static_cast<std::uint64_t>(value) << ((index % 4) * 16);
    }
}

Result MiguMrcCipher::decrypt(std::string_view content) const
{
    if (!valid_key_) return Result::failure(Error::InvalidParam, "mrc_key_not_configured");
    if (content.size() > 1024 * 1024) return Result::failure(Error::InvalidParam, "mrc_input_too_large");
    const auto first = content.find_first_not_of(" \t\r\n");
    if (first == std::string_view::npos) return Result::failure(Error::InvalidParam, "mrc_empty_input");
    content = content.substr(first, content.find_last_not_of(" \t\r\n") - first + 1);
    if (content.starts_with("\xef\xbb\xbf")) content.remove_prefix(3);
    if (content.size() < 32 || content.size() % 16 != 0) return Result::failure(Error::InvalidParam, "mrc_invalid_hex_length");
    std::vector<std::uint64_t> blocks(content.size() / 16);
    for (std::size_t index = 0; index < blocks.size(); ++index) {
        const auto token = content.substr(index * 16, 16);
        const auto parsed = std::from_chars(token.data(), token.data() + token.size(), blocks[index], 16);
        if (parsed.ec != std::errc {} || parsed.ptr != token.data() + token.size()) return Result::failure(Error::InvalidParam, "mrc_invalid_hex");
    }
    constexpr std::uint64_t delta = 0x9e3779b9;
    auto sum = (6 + 52 / blocks.size()) * delta;
    auto y = blocks.front();
    while (sum != 0) {
        for (std::size_t position = blocks.size() - 1; position > 0; --position) {
            y = blocks[position] -= mix(blocks[position - 1], y, sum, position, key_);
        }
        y = blocks[0] -= mix(blocks.back(), y, sum, 0, key_);
        sum -= delta;
    }
    return decodeText(blocks);
}

} // namespace orchestrator::modules::crawler::infrastructure::music::migu::lyrics
