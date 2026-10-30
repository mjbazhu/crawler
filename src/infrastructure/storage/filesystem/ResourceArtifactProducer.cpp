#include "crawler/infrastructure/storage/filesystem/ResourceArtifactProducer.h"

#include <openssl/evp.h>
#include <algorithm>
#include <array>
#include <fstream>
#include <filesystem>
#include <map>
#include <mutex>
#include <stdexcept>
#include <utility>
#include <vector>
#include <string>
#include <string_view>
#include <cstdint>

namespace orchestrator::modules::crawler::infrastructure::storage::filesystem {
namespace model = application::model;
namespace result = application::result;
namespace ports = application::ports;
namespace {
// 只对已经生成的完整内容计算摘要，不把来源/旧本地摘要冒充产物摘要。
std::string sha256(std::string_view bytes)
{
    std::array<unsigned char, 32> hash {};
    unsigned int length = 0;
    if (EVP_Digest(bytes.data(), bytes.size(), hash.data(), &length, EVP_sha256(), nullptr) != 1 || length != hash.size()) throw std::runtime_error("artifact digest failed");
    static constexpr char hex[] = "0123456789abcdef";
    std::string value;
    for (const auto byte : hash) {
        value.push_back(hex[byte >> 4]);
        value.push_back(hex[byte & 15]);
    }
    return value;
}
} // namespace

class ResourceArtifactProducer::State {
public:
    std::shared_ptr<ports::TimedLyricsDecoder> decoder;                    // 线程安全纯解码
    std::shared_ptr<ports::LyricArtifactEncoder> encoder;                  // 统一导出协议
    std::string version;                                                   // 本实例处理版本固定
    bool available = true;                                                 // 启动密钥能力，不存储或返回密钥
    mutable std::mutex mutex;                                              // 只保护缓存与纯计算
    std::map<std::string, std::vector<model::EncodedLyricArtifact>> cache; // 最多 16 份、每份最多 4 MiB
    // 文件读取在锁外；锁只包有界纯解码与缓存，不进行网络/数据库操作。
    result::ServiceResult<std::vector<model::EncodedLyricArtifact>> generate(const model::ResourceContentSnapshot &source, const model::ResourceTransferContext &context)
    {
        using Result = result::ServiceResult<std::vector<model::EncodedLyricArtifact>>;
        if (context.stopped()) return Result::failure(result::ServiceErrorType::ExternalApiError, "cancelled");
        if (source.sizeBytes() == 0 || source.sizeBytes() > 1024 * 1024) return Result::failure(result::ServiceErrorType::InvalidParam, "lyric_source_size_invalid");
        std::ifstream stream(std::filesystem::u8path(source.filePath()), std::ios::binary);
        std::string content(static_cast<std::size_t>(source.sizeBytes()), '\0');
        stream.read(content.data(), static_cast<std::streamsize>(content.size()));
        if (!stream || stream.peek() != std::char_traits<char>::eof() || sha256(content) != source.checksum()) return Result::failure(result::ServiceErrorType::Conflict, "lyric_source_changed");
        std::lock_guard lock(mutex);
        if (const auto found = cache.find(source.checksum()); found != cache.end()) return Result::success(found->second);
        if (context.remaining() == 0) return Result::failure(result::ServiceErrorType::ExternalApiError, "timeout");
        const auto decoded = decoder->decode(content);
        if (!decoded.isSuccess()) return Result::failure(decoded.error(), decoded.message());
        auto encoded = encoder->encode(*decoded.data());
        if (!encoded.isSuccess()) return encoded;
        std::uint64_t total = 0;
        bool json = false, lrc = false;
        for (const auto &file : *encoded.data()) {
            if (file.kind == "word_json" && !json)
                json = true;
            else if (file.kind == "enhanced_lrc" && !lrc)
                lrc = true;
            else
                return Result::failure(result::ServiceErrorType::InvalidParam, "lyric_invalid_artifacts");
            total += file.content.size();
            if (file.content.empty() || total > 4 * 1024 * 1024) return Result::failure(result::ServiceErrorType::InvalidParam, "lyric_artifacts_too_large");
        }
        if (!json || !lrc) return Result::failure(result::ServiceErrorType::InvalidParam, "lyric_invalid_artifacts");
        if (cache.size() == 16) cache.erase(cache.begin());
        cache.emplace(source.checksum(), *encoded.data());
        return encoded;
    }
};

ResourceArtifactProducer::ResourceArtifactProducer(std::shared_ptr<ports::TimedLyricsDecoder> decoder, std::shared_ptr<ports::LyricArtifactEncoder> encoder, bool available) : state_(std::make_shared<State>())
{
    if (!decoder || !encoder) throw std::invalid_argument("artifact producer dependencies required");
    state_->version = std::string(decoder->version()) + "-" + std::string(encoder->version());
    state_->decoder = std::move(decoder);
    state_->encoder = std::move(encoder);
    state_->available = available;
}

bool ResourceArtifactProducer::available() const
{
    return state_->available;
}

result::ServiceResult<model::LyricArtifactBundle> ResourceArtifactProducer::generate(const model::ResourceTransferJob &parent, const model::ResourceContentSnapshot &source, const model::ResourceTransferContext &context)
{
    using Result = result::ServiceResult<model::LyricArtifactBundle>;
    if (!available()) return Result::failure(result::ServiceErrorType::ExternalApiError, "waiting_input");
    const auto generated = state_->generate(source, context);
    if (!generated.isSuccess()) return Result::failure(generated.error(), generated.message());
    if (context.stopped()) return Result::failure(result::ServiceErrorType::ExternalApiError, "cancelled");
    model::LyricArtifactBundle bundle {parent.resource.resource_id, source.checksum(), state_->version, state_->encoder->schemaVersion(), {}};
    for (const auto &file : *generated.data()) bundle.files.push_back({file.kind, {}, file.content.size(), sha256(file.content)});
    return Result::success(std::move(bundle));
}

std::optional<model::EncodedLyricArtifact> ResourceArtifactProducer::cached(const model::ResourceArtifactInput &input) const
{
    if (input.processor_version != state_->version || input.schema_version != state_->encoder->schemaVersion()) return std::nullopt;
    std::lock_guard lock(state_->mutex);
    const auto found = state_->cache.find(input.source_checksum);
    if (found == state_->cache.end()) return std::nullopt;
    const auto file = std::find_if(found->second.begin(), found->second.end(), [&](const auto &value) { return value.kind == input.artifact_type; });
    return file == found->second.end() ? std::nullopt : std::optional(*file);
}

result::ServiceResult<model::EncodedLyricArtifact> ResourceArtifactProducer::regenerate(const model::ResourceArtifactInput &input, const model::ResourceContentSnapshot &source, const model::ResourceTransferContext &context)
{
    using Result = result::ServiceResult<model::EncodedLyricArtifact>;
    if (!available()) return Result::failure(result::ServiceErrorType::ExternalApiError, "waiting_input");
    if (input.source_checksum != source.checksum() || input.processor_version != state_->version || input.schema_version != state_->encoder->schemaVersion()) return Result::failure(result::ServiceErrorType::Conflict, "artifact_input_changed");
    const auto generated = state_->generate(source, context);
    if (!generated.isSuccess()) return Result::failure(generated.error(), generated.message());
    const auto file = cached(input);
    if (!file) return Result::failure(result::ServiceErrorType::InvalidParam, "invalid_artifact_type");
    return Result::success(*file);
}
} // namespace orchestrator::modules::crawler::infrastructure::storage::filesystem
