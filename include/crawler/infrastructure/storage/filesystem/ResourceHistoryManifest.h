#pragma once
#include "crawler/application/model/ResourceHistoryAdoption.h"
#include <filesystem>
#include <istream>
#include <optional>
#include <ostream>
#include <string>
namespace orchestrator::modules::crawler::infrastructure::storage::filesystem {
// 线程不安全、单次调用独占的已核对文件副本，避免审阅后源文件被替换。
class ResourceHistoryFileSnapshot {
public:
    // 只接受普通文件并复制到独占路径，核对完整SHA256后才能读取。
    ResourceHistoryFileSnapshot(const std::filesystem::path &source, std::string expected_sha256);
    // 仅删除本次成功创建的副本，不触碰原清单。
    ~ResourceHistoryFileSnapshot();
    ResourceHistoryFileSnapshot(const ResourceHistoryFileSnapshot &) = delete;
    ResourceHistoryFileSnapshot &operator=(const ResourceHistoryFileSnapshot &) = delete;
    // 只读快照路径。
    const std::filesystem::path &path() const;
    // 已核对的完整文件摘要。
    const std::string &sha256() const;

private:
    std::filesystem::path path_; // 独占副本
    std::string hash_;           // 核对后的摘要
    bool owned_ = false;         // 限定清理范围
};
// 将原报告流转为独立采信清单；默认只为正证据建议adopt，其余保留review。
application::model::ResourceHistoryAdoptionPlan prepareResourceHistoryManifest(std::istream &report, std::string report_hash, std::string run_id, std::ostream &manifest);
// 读取独立采信清单头并绑定完整清单SHA256。
application::model::ResourceHistoryAdoptionPlan readResourceHistoryManifest(std::istream &manifest, std::string manifest_hash);
// 每次最多读取200条；EOF并已覆盖高水位返回空。
std::optional<application::model::ResourceHistoryAdoptionPage> readResourceHistoryPage(std::istream &manifest, const application::model::ResourceHistoryAdoptionPlan &plan, std::uint64_t after);
// 写入之前完整流式预检，防止格式截断在后页才被发现。
void validateResourceHistoryManifest(std::istream &manifest, const application::model::ResourceHistoryAdoptionPlan &plan);
} // namespace orchestrator::modules::crawler::infrastructure::storage::filesystem
