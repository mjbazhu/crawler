#include "crawler/infrastructure/storage/rclone/RcloneDiscoveryProbe.h"
#include "crawler/infrastructure/storage/baidupcs/BaiduDiscoveryProbe.h"
#include "infrastructure/storage/support/RcloneTransferTestSupport.h"
#include "infrastructure/storage/support/BaiduTransferTestSupport.h"
#include <iostream>
namespace c = orchestrator::modules::crawler;
namespace test = c::tests::cloud_transfer;
namespace model = c::application::model;
int main()
{
    try {
        model::ResourceDiscoveryDefinition d;
        d.run_id = "fixture";
        d.strip_prefix = "C:\\archive\\";
        d.path_source = "local";
        model::validateResourceDiscoveryDefinition(d);
        model::LegacyResourceSnapshot source;
        source.identity = {model::ResourceOwner::Song, 1};
        source.generation = 1;
        source.local_path = "C:\\archive\\歌手\\歌曲.flac";
        test::require(model::resourceDiscoveryCandidate(source, d) == "歌手/歌曲.flac", "explicit root mapping failed");
        source.local_path = "C:/archive-other/file";
        test::require(model::resourceDiscoveryCandidate(source, d).empty(), "prefix component boundary ignored");
        for (const auto path : {"../file", "/absolute", "a/../file", "a//b", "a/./b", "file?token=private", "https://private.test/file", "C:/file", "a\\b"}) test::require(!model::safeResourceDiscoveryPath(path), "unsafe candidate accepted");
        source.cloud_path = "resources/v1/歌曲.bin";
        source.status = 4;
        d.path_source = "cloud";
        d.strip_prefix.clear();
        test::require(model::resourceDiscoveryCandidate(source, d).empty(), "restricted source was probed");
        source.status = 2;
        source.size_bytes = 3;
        source.checksum = "md5:" + std::string(32, 'a');
        model::ResourceObjectObservation object;
        object.state = "found";
        object.size_bytes = 3;
        object.hashes["md5"] = std::string(32, 'a');
        test::require(model::classifyResourceDiscovery(source, source.cloud_path, object).grade == "hash_matched", "legacy MD5 comparison lost");
        object.hashes["md5"] = std::string(32, 'b');
        test::require(model::classifyResourceDiscovery(source, source.cloud_path, object).grade == "conflict", "hash conflict not retained");
        object.hashes.clear();
        test::require(model::classifyResourceDiscovery(source, source.cloud_path, object).grade == "observed", "existence promoted to content verification");
        object.state = "unknown";
        test::require(model::classifyResourceDiscovery(source, source.cloud_path, object).grade == "unknown", "unknown became missing");
        object.state = "missing";
        test::require(model::classifyResourceDiscovery(source, source.cloud_path, object).grade == "missing", "explicit missing lost");
        object.state = "directory";
        test::require(model::classifyResourceDiscovery(source, source.cloud_path, object).grade == "conflict", "directory became file");
        const auto identity = [](const auto &) { return c::application::result::ServiceResult<std::string>::success(std::string(64, 'b')); };
        auto rc = std::make_shared<test::RcClient>();
        rc->objects[source.cloud_path] = true;
        rc->expected.size_bytes = 3;
        rc->expected.md5 = std::string(32, 'a');
        c::infrastructure::storage::rclone::RcloneConfig config;
        config.target_fs = rc->expected_fs;
        config.rc_url = rc->expected_rc_url;
        auto remote = c::infrastructure::storage::rclone::makeRcloneDiscoveryProbe(config, rc, identity);
        auto result = remote->observe(source.cloud_path, test::context());
        test::require(result.isSuccess() && result.data()->state == "found" && result.data()->hashes.at("md5") == std::string(32, 'a'), "rclone stat evidence missing");
        rc->no_hash = true;
        result = remote->observe(source.cloud_path, test::context());
        test::require(result.isSuccess() && result.data()->hashes.empty() && rc->readbacks == 0, "missing hash triggered content readback");
        rc->malformed = true;
        test::require(remote->observe(source.cloud_path, test::context()).data()->state == "unknown", "malformed stat guessed existence");
        rc->malformed = false;
        rc->directory = true;
        test::require(remote->observe(source.cloud_path, test::context()).data()->state == "directory", "directory lost");
        rc->directory = false;
        test::require(remote->observe("resources/v1/missing", test::context()).data()->state == "missing", "null stat not missing");
        auto cancelled = test::context();
        cancelled.cancelled = [] { return true; };
        const auto calls = rc->probes;
        remote->observe(source.cloud_path, cancelled);
        test::require(calls == rc->probes && rc->uploads == 0 && rc->readbacks == 0, "cancelled metadata used content or RPC");
        test::Root root;
        const auto file = root.path / "native-fixture";
        test::writeFile(file, 3);
        auto runner = std::make_shared<test::BaiduRunner>(root.path / "remote");
        runner->objects["/Archive/file"] = file;
        c::infrastructure::storage::baidupcs::BaiduPcsGoConfig native;
        native.executable_path = "pcs-fixture";
        native.cloud_root = "/Archive";
        native.local_root = root.path;
        auto baidu = c::infrastructure::storage::baidupcs::makeBaiduDiscoveryProbe(native, [runner](const auto &line, auto timeout, const auto &stop) { return (*runner)(line, timeout, stop); }, identity);
        result = baidu->observe("file", test::context());
        test::require(result.isSuccess() && result.data()->state == "found" && result.data()->size_bytes == 3 && result.data()->hashes.empty(), "native meta promoted hash");
        test::require(baidu->observe("missing", test::context()).data()->state == "missing", "native explicit missing lost");
        runner->unknown = true;
        test::require(baidu->observe("file", test::context()).data()->state == "unknown" && runner->downloads == 0 && runner->uploads == 0, "native unknown triggered download or missing");
        std::cout << "PASS: safe mapping, evidence grades and bounded stat/meta without media IO\n";
        return 0;
    } catch (const std::exception &e) {
        std::cerr << "FAIL: " << e.what() << '\n';
        return 1;
    }
}
