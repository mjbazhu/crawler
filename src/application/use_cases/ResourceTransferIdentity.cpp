#include "crawler/application/model/ResourceTransfer.h"

#include <algorithm>
#include <charconv>
#include <cctype>

namespace orchestrator::modules::crawler::application::model {

std::string_view resourceOwnerName(ResourceOwner owner)
{
    switch (owner) {
    case ResourceOwner::Song: return "song";
    case ResourceOwner::Singer: return "singer";
    case ResourceOwner::Album: return "album";
    case ResourceOwner::SongArtifact: return "song_artifact";
    }
    return {};
}

std::optional<ResourceOwner> parseResourceOwner(std::string_view name)
{
    for (const auto owner : {ResourceOwner::Song, ResourceOwner::Singer, ResourceOwner::Album, ResourceOwner::SongArtifact})
        if (name == resourceOwnerName(owner)) return owner;
    return std::nullopt;
}

std::string_view resourceDestinationName(ResourceDestination destination)
{
    switch (destination) {
    case ResourceDestination::Local: return "local";
    case ResourceDestination::Baidu: return "baidu";
    case ResourceDestination::Gdrive: return "gdrive";
    }
    return {};
}

std::optional<ResourceDestination> parseResourceDestination(std::string_view name)
{
    for (const auto destination : {ResourceDestination::Local, ResourceDestination::Baidu, ResourceDestination::Gdrive})
        if (name == resourceDestinationName(destination)) return destination;
    return std::nullopt;
}

std::optional<ResourceDestination> parseCloudProvider(std::string_view name)
{
    const auto destination = parseResourceDestination(name);
    return destination && *destination != ResourceDestination::Local ? destination : std::nullopt;
}

bool supportsResourceTransferAdapter(ResourceDestination destination, std::string_view adapter)
{
    if (destination == ResourceDestination::Local) return adapter == "filesystem";
    if (destination == ResourceDestination::Baidu) return adapter == "baidupcs" || adapter == "rclone";
    return destination == ResourceDestination::Gdrive && adapter == "rclone";
}

LegacyResourceChecksum parseLegacyResourceChecksum(std::string_view value)
{
    LegacyResourceChecksum result;
    result.original = std::string(value);
    std::string normalized(value);
    std::transform(normalized.begin(), normalized.end(), normalized.begin(), [](unsigned char ch) { return static_cast<char>(std::tolower(ch)); });
    std::string_view digest(normalized);
    if (digest.starts_with("rclone:")) digest.remove_prefix(7);
    const auto separator = digest.find(':');
    auto algorithm = separator == std::string_view::npos ? std::string_view("sha256") : digest.substr(0, separator);
    if (separator != std::string_view::npos) digest.remove_prefix(separator + 1);
    if (algorithm == "sha-256") algorithm = "sha256";
    if (algorithm == "sha-1") algorithm = "sha1";
    if (algorithm == "size") {
        std::uint64_t size = 0;
        const auto parsed = std::from_chars(digest.data(), digest.data() + digest.size(), size);
        if (!digest.empty() && parsed.ec == std::errc {} && parsed.ptr == digest.data() + digest.size()) {
            result.algorithm = "size";
            result.value = std::string(digest);
            return result;
        }
    } else {
        const std::size_t expected = algorithm == "sha256" ? 64 : algorithm == "sha1" ? 40 :
                                                              algorithm == "md5"      ? 32 :
                                                                                        0;
        if (expected && digest.size() == expected && std::all_of(digest.begin(), digest.end(), [](unsigned char ch) { return std::isxdigit(ch); })) {
            result.algorithm = std::string(algorithm);
            result.value = std::string(digest);
            result.content_digest = true;
            if (algorithm == "sha256") result.sha256 = result.value;
            return result;
        }
    }
    result.algorithm = "unknown";
    return result;
}

} // namespace orchestrator::modules::crawler::application::model
