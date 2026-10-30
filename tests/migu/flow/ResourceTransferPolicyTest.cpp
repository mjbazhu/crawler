#include "crawler/application/use_cases/ResourceTransferPolicy.h"

#include <iostream>
#include <stdexcept>
#include <string>

namespace c = orchestrator::modules::crawler;
namespace model = c::application::model;
namespace use = c::application::use_cases;
namespace {
// 断言业务语义，不访问数据库、文件或远端。
void require(bool value, const char *message)
{
    if (!value) throw std::runtime_error(message);
}
} // namespace

// 核对跳过优先级、目标修订隔离及旧摘要证据不被伪造。
int main()
{
    try {
        use::ResourceTransferPolicyInput input;
        input.version = {7, 9};
        input.skip = {{7, 9}, "reviewed-manifest", "legacy_both_clouds_present", true};
        input.available = false;
        input.waiting_input = true;
        input.active = true;
        require(use::evaluateResourceTransfer(input) == use::ResourceWorkDecision::SkipPolicy, "historical skip depends on I/O readiness");
        input.cancelled = true;
        require(use::evaluateResourceTransfer(input) == use::ResourceWorkDecision::Cancelled, "skip changed an explicit cancellation result");
        input.cancelled = false;
        input.version.target_revision++;
        require(use::evaluateResourceTransfer(input) == use::ResourceWorkDecision::Blocked, "old target skip was reused");
        input.dispatch_ready = true;
        require(use::evaluateResourceTransfer(input) == use::ResourceWorkDecision::Busy, "active target was taken over");
        input.active = false;
        require(use::evaluateResourceTransfer(input) == use::ResourceWorkDecision::WaitingInput, "missing producer input was ignored");
        input.waiting_input = false;
        require(use::evaluateResourceTransfer(input) == use::ResourceWorkDecision::Unavailable, "unavailable target was treated as ready");
        input.available = true;
        require(use::evaluateResourceTransfer(input) == use::ResourceWorkDecision::Transfer, "new target was not eligible");
        input.cancelled = true;
        require(use::evaluateResourceTransfer(input) == use::ResourceWorkDecision::Cancelled, "cancellation requeued transfer");
        input.enabled = false;
        require(use::evaluateResourceTransfer(input) == use::ResourceWorkDecision::Disabled, "disabled target was started");

        const auto sha = model::parseLegacyResourceChecksum("rclone:SHA-256:" + std::string(64, 'A'));
        require(sha.algorithm == "sha256" && sha.sha256 == std::string(64, 'a'), "valid SHA-256 not normalized");
        const auto md5 = model::parseLegacyResourceChecksum("rclone:md5:" + std::string(32, 'b'));
        require(md5.content_digest && md5.sha256.empty(), "MD5 became SHA-256");
        const auto size = model::parseLegacyResourceChecksum("rclone:size:123");
        require(size.algorithm == "size" && !size.content_digest && size.sha256.empty(), "size-only fingerprint became content proof");
        require(model::parseLegacyResourceChecksum("rclone:size:18446744073709551616").algorithm == "unknown", "overflowing size accepted");
        require(model::parseLegacyResourceChecksum("rclone:sha256:" + std::string(64, 'z')).algorithm == "unknown", "malformed hash accepted");
        require(!model::parseCloudProvider("local") && model::parseCloudProvider("gdrive").has_value(), "local became a cloud provider");
        require(model::supportsResourceTransferAdapter(model::ResourceDestination::Baidu, "rclone") && model::supportsResourceTransferAdapter(model::ResourceDestination::Baidu, "baidupcs"), "Baidu was tied to one transport tool");
        require(!model::supportsResourceTransferAdapter(model::ResourceDestination::Gdrive, "baidupcs") && !model::supportsResourceTransferAdapter(model::ResourceDestination::Local, "rclone"), "adapter compatibility crossed platform scope");
        require(model::parseResourceOwner("song_artifact").has_value() && !model::parseResourceOwner("songs;drop"), "owner parser allowed arbitrary table names");
        std::cout << "PASS: persistent skip priority, version/target isolation, cancellation and legacy checksum classification\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
