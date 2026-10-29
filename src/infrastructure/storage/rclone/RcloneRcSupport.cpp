#include "crawler/infrastructure/storage/rclone/RcloneRcSupport.h"

#include <openssl/evp.h>

#include <cctype>
#include <stdexcept>

namespace orchestrator::modules::crawler::infrastructure::storage::rclone::detail {

std::vector<std::string> rcHeaders(const RcloneConfig &config, std::string_view content_type)
{
    std::vector<std::string> headers {"Accept: application/json", "Content-Type: " + std::string(content_type)};
    if (!config.rc_user.empty() || !config.rc_password.empty()) {
        const auto credentials = config.rc_user + ':' + config.rc_password;
        if (credentials.size() > 8192) throw std::invalid_argument("rclone credentials exceed configured boundary");
        std::string encoded(4 * ((credentials.size() + 2) / 3) + 1, '\0');
        const int size = EVP_EncodeBlock(reinterpret_cast<unsigned char *>(encoded.data()), reinterpret_cast<const unsigned char *>(credentials.data()), static_cast<int>(credentials.size()));
        encoded.resize(static_cast<std::size_t>(size));
        headers.push_back("Authorization: Basic " + encoded);
    }
    return headers;
}

std::string queryEncode(std::string_view value)
{
    constexpr char digits[] = "0123456789ABCDEF";
    std::string encoded;
    for (unsigned char ch : value) {
        if ((ch >= 'a' && ch <= 'z') || (ch >= 'A' && ch <= 'Z') || (ch >= '0' && ch <= '9') || ch == '-' || ch == '_' || ch == '.' || ch == '~')
            encoded += static_cast<char>(ch);
        else {
            encoded += '%';
            encoded += digits[ch >> 4];
            encoded += digits[ch & 15];
        }
    }
    return encoded;
}

} // namespace orchestrator::modules::crawler::infrastructure::storage::rclone::detail
