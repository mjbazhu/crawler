#include "crawler/infrastructure/storage/rclone/RcloneRcSupport.h"

#include "crawler/infrastructure/http/client/AsioHttpUrl.h"
#include "crawler/infrastructure/storage/cloud/CloudFileSupport.h"

#include <openssl/evp.h>

#include <cctype>
#include <cstddef>
#include <algorithm>
#include <stdexcept>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::storage::rclone::detail {

RcloneConfig normalizeCloudConfig(RcloneConfig config)
{
    while (!config.rc_url.empty() && config.rc_url.back() == '/') config.rc_url.pop_back();
    while (!config.target_fs.empty() && config.target_fs.back() == '/') config.target_fs.pop_back();
    const auto url = http::client::detail::parseUrl(config.rc_url);
    const auto colon = config.target_fs.find(':');
    const bool name = colon != 0 && colon != std::string::npos && std::all_of(config.target_fs.begin(), config.target_fs.begin() + static_cast<std::ptrdiff_t>(colon), [](unsigned char c) { return std::isalnum(c) || c == '_' || c == '-' || c == '.'; });
    const bool root = name && (colon + 1 == config.target_fs.size() || cloud::safeRelativePath(std::string_view(config.target_fs).substr(colon + 1)));
    if (!url || url->host.find('@') != std::string::npos || config.rc_url.find_first_of("\r\n?#") != std::string::npos || !root) throw std::invalid_argument("invalid rclone cloud configuration");
    return config;
}

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
