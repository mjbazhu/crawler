#include "crawler/application/model/ResourceCloudCleanup.h"
#include <iostream>
#include <stdexcept>
namespace model = orchestrator::modules::crawler::application::model;
namespace {
// 无真实云端或文件内容。
void require(bool value, const char *message)
{
    if (!value) throw std::runtime_error(message);
}
} // namespace
int main()
{
    try {
        for (const auto path : {"https://private.test/object?token=secret", "archive:root/../file", "archive:root//file", "a\\b", "a@b", "/root/file#fragment"}) require(!model::safeResourceCloudCleanupPath(path), "unsafe cloud path accepted");
        require(model::resourceCloudCleanupObjectKey("archive:root/file").empty(), "legacy basename guessed object identity");
        const auto key = std::string("resources/v1/song/1/歌曲.bin");
        require(model::resourceCloudCleanupObjectKey("old:root/" + key) == key && model::resourceCloudCleanupObjectKey("renamed:root/" + key) == key, "remote alias broke object identity");
        model::ResourceCloudCleanupCandidate c;
        c.cloud_path = "archive:root/" + key;
        c.object_key = key;
        c.content_generation = 1;
        c.target_revision = 2;
        c.confirmed_content_generation = 1;
        c.confirmed_target_revision = 2;
        c.verification = "sha256_verified";
        c.checksum = std::string(64, 'a');
        c.receipt_state = "succeeded";
        model::ResourceCloudCleanupFacts f;
        f.target_matches = true;
        f.maintenance = true;
        require(model::classifyResourceCloudCleanup(c, f) == "candidate_only", "valid evidence cannot reach review candidate");
        f.source_exists = true;
        require(model::classifyResourceCloudCleanup(c, f) == "source_retained", "live resource became orphan");
        f.source_exists = false;
        f.target_matches = false;
        require(model::classifyResourceCloudCleanup(c, f) == "target_changed", "changed target candidate allowed");
        f.target_matches = true;
        f.active_intent = true;
        require(model::classifyResourceCloudCleanup(c, f) == "active_or_uncertain_intent", "uncertain attempt ignored");
        f.active_intent = false;
        c.receipt_state = "uncertain";
        require(model::classifyResourceCloudCleanup(c, f) == "active_or_uncertain_intent", "captured native intent lost after receipt deletion");
        c.receipt_state = "succeeded";
        f.shared_reference = true;
        require(model::classifyResourceCloudCleanup(c, f) == "shared_reference", "shared object reference ignored");
        f.shared_reference = false;
        f.alias_reference = true;
        require(model::classifyResourceCloudCleanup(c, f) == "alias_evidence_requires_review", "alias archive lost protection");
        f.alias_reference = false;
        f.maintenance = false;
        require(model::classifyResourceCloudCleanup(c, f) == "maintenance_required", "active writers ignored");
        f.maintenance = true;
        c.verification = "operator_attested";
        require(model::classifyResourceCloudCleanup(c, f) == "unverified_evidence", "declaration became deletion proof");
        c.verification = "sha256_verified";
        c.confirmed_target_revision = 1;
        require(model::classifyResourceCloudCleanup(c, f) == "confirmation_version_mismatch", "old target confirmation accepted");
        std::cout << "PASS: cleanup candidate policy guards references, targets, aliases, intent and evidence\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
