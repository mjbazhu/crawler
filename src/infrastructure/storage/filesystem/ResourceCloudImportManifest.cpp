#include "crawler/infrastructure/storage/filesystem/ResourceCloudImportManifest.h"

#include <nlohmann/json.hpp>
#include <openssl/evp.h>

#include <array>
#include <fstream>
#include <memory>
#include <stdexcept>
#include <utility>

namespace orchestrator::modules::crawler::infrastructure::storage::filesystem {
namespace {
namespace model = application::model;
using json = nlohmann::json;
constexpr std::size_t max_line_bytes = 16 * 1024 * 1024; // 限制单页序列化内存

// 行读取有硬上限，不能先用无界 getline 分配任意大正文。
std::optional<std::string> readLine(std::istream &input)
{
    std::string line;
    char ch = 0;
    while (input.get(ch)) {
        if (ch == '\n') return line;
        if (line.size() >= max_line_bytes) throw std::invalid_argument("manifest line exceeds the bounded page size");
        line.push_back(ch);
    }
    if (input.bad()) throw std::runtime_error("manifest read failed");
    if (line.empty()) return std::nullopt;
    return line;
}

// 禁止 JSON 负数或浮点数静默转成无符号资源身份。
std::uint64_t number(const json &value, const char *key)
{
    const auto &field = value.at(key);
    if (field.is_number_unsigned()) return field.get<std::uint64_t>();
    if (field.is_number_integer() && field.get<std::int64_t>() >= 0) return static_cast<std::uint64_t>(field.get<std::int64_t>());
    throw std::invalid_argument("manifest numeric field is invalid");
}

// 只序列化白名单元信息，不包含认证或临时来源 URL。
json targetJson(const model::LegacyResourceTarget &target)
{
    return {{"destination", model::resourceDestinationName(target.destination)}, {"revision", target.revision}, {"fingerprint", target.fingerprint}, {"dispatch_state", target.dispatch_state}};
}

// 输出完整 JSON 行前检查大小和流错误。
void writeLine(std::ostream &output, const json &value)
{
    const auto line = value.dump();
    if (line.size() > max_line_bytes) throw std::invalid_argument("manifest page is too large; reduce page size");
    output << line << '\n';
    if (!output) throw std::runtime_error("manifest write failed");
}
} // namespace

void writeResourceCloudManifestHeader(std::ostream &output, const ResourceCloudManifestHeader &header)
{
    json targets = json::array();
    for (const auto &target : header.scope.targets) targets.push_back(targetJson(target));
    writeLine(output, {{"type", "crawler_resource_cloud_manifest"}, {"version", 1}, {"run_id", header.plan.run_id}, {"operator_attested_both", header.plan.operator_attested_both}, {"operator_excluded_all", header.plan.operator_excluded_all}, {"legacy_rclone_provider", header.plan.legacy_rclone_provider ? json(model::resourceDestinationName(*header.plan.legacy_rclone_provider)) : json(nullptr)}, {"owner_type", model::resourceOwnerName(header.scope.owner)}, {"database_identity", header.scope.database_identity}, {"high_water_id", header.scope.high_water_id}, {"targets", std::move(targets)}});
}

void writeResourceCloudManifestPage(std::ostream &output, const model::LegacyResourceImportPage &page)
{
    if (page.rows.size() > 5000) throw std::invalid_argument("manifest page has too many rows");
    json rows = json::array();
    for (const auto &row : page.rows) {
        const auto legacy = json::parse(row.legacy_values, nullptr, false);
        if (!legacy.is_object()) throw std::invalid_argument("manifest legacy evidence is invalid");
        rows.push_back({{"resource_id", row.identity.resource_id}, {"generation", row.generation}, {"source_identity", row.source_identity}, {"values_digest", row.values_digest}, {"status", row.status}, {"cloud_backend", row.cloud_backend}, {"cloud_path", row.cloud_path}, {"local_path", row.local_path}, {"size_bytes", row.size_bytes}, {"checksum", row.checksum}, {"legacy_values", legacy}});
    }
    writeLine(output, {{"type", "page"}, {"after_id", page.after_id}, {"next_id", page.next_id}, {"rows", std::move(rows)}});
}

ResourceCloudManifestHeader readResourceCloudManifestHeader(std::istream &input, std::string verified_sha256)
{
    try {
        const auto line = readLine(input);
        if (!line || line->starts_with("\xef\xbb\xbf")) throw std::invalid_argument("manifest header missing or has BOM");
        const auto value = json::parse(*line, nullptr, false);
        if (!value.is_object() || value.at("type") != "crawler_resource_cloud_manifest" || number(value, "version") != 1)
            throw std::invalid_argument("manifest format is unsupported");
        ResourceCloudManifestHeader header;
        header.plan.manifest_hash = std::move(verified_sha256);
        header.plan.run_id = value.at("run_id").get<std::string>();
        header.plan.operator_attested_both = value.at("operator_attested_both").get<bool>();
        header.plan.operator_excluded_all = value.value("operator_excluded_all", false);
        if (!value.at("legacy_rclone_provider").is_null()) {
            header.plan.legacy_rclone_provider = model::parseCloudProvider(value.at("legacy_rclone_provider").get<std::string>());
            if (!header.plan.legacy_rclone_provider) throw std::invalid_argument("manifest rclone mapping is invalid");
        }
        const auto owner = model::parseResourceOwner(value.at("owner_type").get<std::string>());
        if (!owner) throw std::invalid_argument("manifest owner is invalid");
        header.scope.owner = *owner;
        header.scope.database_identity = value.at("database_identity").get<std::string>();
        header.scope.high_water_id = number(value, "high_water_id");
        const auto &targets = value.at("targets");
        if (!targets.is_array() || targets.size() != 3) throw std::invalid_argument("manifest target scope is incomplete");
        std::array<bool, 3> seen {};
        for (const auto &target : targets) {
            const auto destination = model::parseResourceDestination(target.at("destination").get<std::string>());
            if (!destination) throw std::invalid_argument("manifest destination is invalid");
            const auto index = static_cast<std::size_t>(*destination);
            if (seen[index]) throw std::invalid_argument("manifest destination is duplicated");
            header.scope.targets[index] = {*destination, number(target, "revision"), target.at("fingerprint").get<std::string>(), target.at("dispatch_state").get<std::string>()};
            seen[index] = true;
        }
        return header;
    } catch (const json::exception &) {
        throw std::invalid_argument("manifest header fields are invalid");
    }
}

std::optional<model::LegacyResourceImportPage> readResourceCloudManifestPage(std::istream &input, const ResourceCloudManifestHeader &header)
{
    try {
        const auto line = readLine(input);
        if (!line) return std::nullopt;
        const auto value = json::parse(*line, nullptr, false);
        if (!value.is_object() || value.at("type") != "page" || !value.at("rows").is_array() || value.at("rows").size() > 5000)
            throw std::invalid_argument("manifest page format is invalid");
        auto page = header.scope;
        page.after_id = number(value, "after_id");
        page.next_id = number(value, "next_id");
        for (const auto &row : value.at("rows")) {
            model::LegacyResourceSnapshot snapshot;
            snapshot.identity = {page.owner, number(row, "resource_id")};
            snapshot.generation = number(row, "generation");
            snapshot.source_identity = row.at("source_identity").get<std::string>();
            snapshot.values_digest = row.at("values_digest").get<std::string>();
            const auto status = number(row, "status");
            if (status > 4 || !row.at("legacy_values").is_object()) throw std::invalid_argument("manifest resource state is invalid");
            snapshot.status = static_cast<int>(status);
            snapshot.cloud_backend = row.at("cloud_backend").get<std::string>();
            snapshot.cloud_path = row.at("cloud_path").get<std::string>();
            snapshot.local_path = row.at("local_path").get<std::string>();
            snapshot.size_bytes = number(row, "size_bytes");
            snapshot.checksum = row.at("checksum").get<std::string>();
            snapshot.legacy_values = row.at("legacy_values").dump();
            page.rows.push_back(std::move(snapshot));
        }
        return page;
    } catch (const json::exception &) {
        throw std::invalid_argument("manifest page fields are invalid");
    }
}

std::string resourceCloudManifestSha256(const std::filesystem::path &path)
{
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("manifest cannot be opened");
    std::unique_ptr<EVP_MD_CTX, decltype(&EVP_MD_CTX_free)> context(EVP_MD_CTX_new(), EVP_MD_CTX_free);
    if (!context || EVP_DigestInit_ex(context.get(), EVP_sha256(), nullptr) != 1) throw std::runtime_error("manifest digest initialization failed");
    std::array<char, 65536> buffer {};
    while (input) {
        input.read(buffer.data(), static_cast<std::streamsize>(buffer.size()));
        if (input.gcount() && EVP_DigestUpdate(context.get(), buffer.data(), static_cast<std::size_t>(input.gcount())) != 1)
            throw std::runtime_error("manifest digest update failed");
    }
    if (input.bad()) throw std::runtime_error("manifest digest read failed");
    std::array<unsigned char, EVP_MAX_MD_SIZE> digest {};
    unsigned int length = 0;
    if (EVP_DigestFinal_ex(context.get(), digest.data(), &length) != 1 || length != 32) throw std::runtime_error("manifest digest finalization failed");
    constexpr char hex[] = "0123456789abcdef";
    std::string result;
    for (unsigned int i = 0; i < length; ++i) {
        result.push_back(hex[digest[i] >> 4]);
        result.push_back(hex[digest[i] & 15]);
    }
    return result;
}

} // namespace orchestrator::modules::crawler::infrastructure::storage::filesystem
