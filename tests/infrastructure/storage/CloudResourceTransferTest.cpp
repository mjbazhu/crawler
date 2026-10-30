#include "crawler/infrastructure/app/ResourceTransferFactory.h"
#include "crawler/infrastructure/storage/baidupcs/BaiduResourceTransfer.h"
#include "crawler/infrastructure/storage/rclone/RcloneResourceTransfer.h"
#include "crawler/infrastructure/storage/rclone/RcloneRcSupport.h"
#include "infrastructure/storage/support/BaiduTransferTestSupport.h"
#include "infrastructure/storage/support/RcloneTransferTestSupport.h"

#include <atomic>
#include <future>
#include <iostream>
#include <thread>

namespace c = orchestrator::modules::crawler;
namespace test = c::tests::cloud_transfer;
namespace model = c::application::model;
namespace app = c::infrastructure::app;
using Destination = model::ResourceDestination;
using Observation = model::ResourceAttemptObservation;

namespace {
// 每个 fixture 都有独立文件、后端状态和不创建的正式根。
struct Fixture {
    test::Root root;                                                         // 拥有全部测试文件
    std::shared_ptr<test::Snapshot> snapshot;                                // 大文件快照
    std::shared_ptr<test::RcClient> rc = std::make_shared<test::RcClient>(); // 远端协议替身
    std::shared_ptr<test::BaiduRunner> baidu;                                // 进程替身
    app::ServerApplication::Config config;                                   // 不使用任何 env/登录态
    app::ResourceTransferAssembly assembly;                                  // 真正工厂及具体策略
    Fixture()
    {
        const auto source = root.path / std::filesystem::u8path("source/歌曲.bin");
        test::writeFile(source, 9 * 1024 * 1024 + 7);
        snapshot = std::make_shared<test::Snapshot>(source);
        rc->expected = snapshot->digest;
        baidu = std::make_shared<test::BaiduRunner>(root.path / "remote-fixture");
        config.storage_path = test::utf8(root.path / "formal-local");
        config.baidupcs.executable_path = "pcs-fixture";
        config.baidupcs.cloud_root = "/Archive";
        config.rclone.rc_url = "http://127.0.0.1:5572";
        config.rclone.target_fs = "archive:Media";
        assembly = app::ResourceTransferFactory(config).build(false, {{Destination::Baidu, root.path / "baidu-stage", 8ULL * 1024 * 1024 * 1024, "baidupcs"}, {Destination::Gdrive, {}, 8ULL * 1024 * 1024 * 1024, "rclone"}}, rc, [runner = baidu](const auto &line, auto timeout, const auto &cancelled) { return (*runner)(line, timeout, cancelled); });
        test::require(!assembly.local && !assembly.destination(Destination::Local) && assembly.destination(Destination::Baidu) && assembly.destination(Destination::Gdrive), "factory did not supply independent file strategies");
        test::require(baidu->probes == 0 && rc->probes == 0 && !std::filesystem::exists(root.path / "baidu-stage") && !std::filesystem::exists(root.path / "formal-local"), "construction performed content or target IO");
    }
    // 调用具体工厂策略，prepare 不得上传。
    std::shared_ptr<c::application::ports::ResourceTransferWrite> prepare(Destination destination)
    {
        const auto prepared = assembly.destination(destination)->prepare(test::job(destination), snapshot, test::context());
        test::require(prepared.isSuccess(), "cloud prepare failed");
        return *prepared.data();
    }
};
// 将完整远端对象登记到 RC 替身，仅用于协议测试。
void putRc(Fixture &fixture, const model::ResourceTransferAttempt &attempt)
{
    fixture.rc->objects[attempt.path.substr(fixture.config.rclone.target_fs.size() + 1)] = true;
}
// 检查配置沿用与逐端覆盖，认证只比较 fixture 值，不输出到日志。
void expectRcConfig(test::RcClient &client, const c::infrastructure::storage::rclone::RcloneConfig &config)
{
    client.expected_fs = config.target_fs;
    client.expected_rc_url = config.rc_url;
    for (const auto &header : c::infrastructure::storage::rclone::detail::rcHeaders(config))
        if (header.starts_with("Authorization:")) client.required_authorization = header;
}
} // namespace

int main()
{
    try {
        {
            Fixture f;
            auto task = test::job(Destination::Gdrive);
            task.policy.strict_sha256 = true;
            const auto prepared = f.assembly.destination(Destination::Gdrive)->prepare(task, f.snapshot, test::context());
            test::require(prepared.isSuccess(), "strict cloud prepare failed");
            const auto receipt = (*prepared.data())->publish(test::context());
            test::require(receipt.isSuccess() && receipt.data()->verification == model::ResourceVerification::Sha256Verified && f.rc->readbacks == 1 && f.rc->uploads == 1, "strict policy uploaded but stopped at weak MD5 evidence");
        }
        {
            Fixture f;
            auto write = f.prepare(Destination::Gdrive);
            test::require(f.rc->probes == 0 && f.rc->uploads == 0, "prepare contacted RC");
            const auto receipt = write->publish(test::context());
            test::require(receipt.isSuccess() && receipt.data()->uploaded && receipt.data()->verification == model::ResourceVerification::HashMatched && receipt.data()->remote_hash_algorithm == "md5" && receipt.data()->checksum == f.snapshot->checksum(), "Google MD5 promoted or large upload failed");
            test::require(f.rc->uploads == 1 && f.rc->largest_chunk <= 64 * 1024 && !std::filesystem::exists(f.root.path / "formal-local"), "Google aggregated bytes or opened formal local root");
            write->settle(model::ResourceTransferSettlement::Accept);
            test::require(!write->publish(test::context()).isSuccess(), "published session was reused");
            std::filesystem::remove(std::filesystem::u8path(f.snapshot->filePath()));
            const auto inspected = f.assembly.destination(Destination::Gdrive)->inspect(test::repair(test::job(Destination::Gdrive), write->attempt()), test::context());
            test::require(inspected.isSuccess() && inspected.data()->observation == Observation::Matched && inspected.data()->receipt.verification == model::ResourceVerification::Sha256Verified && !inspected.data()->receipt.uploaded && f.rc->readbacks == 1 && f.rc->uploads == 1, "repair required source or manufactured upload time");
        }
        {
            Fixture f;
            auto write = f.prepare(Destination::Gdrive);
            putRc(f, write->attempt());
            const auto receipt = write->publish(test::context());
            test::require(receipt.isSuccess() && !receipt.data()->uploaded && f.rc->uploads == 0, "matched object was uploaded again");
            auto another = f.prepare(Destination::Gdrive);
            test::require(another->attempt().path != write->attempt().path, "new attempt reused old mutable object path");
        }
        {
            Fixture f;
            const auto invalid = app::ResourceTransferFactory(f.config).build(false, {{Destination::Baidu, f.root.path / "bad-stage", 32, "baidupcs"}, {Destination::Gdrive, {}, 8ULL * 1024 * 1024 * 1024, "rclone"}}, f.rc, [runner = f.baidu](const auto &line, auto timeout, const auto &stop) { return (*runner)(line, timeout, stop); });
            test::require(invalid.cloud(Destination::Baidu) && !invalid.destination(Destination::Baidu) && invalid.destination(Destination::Gdrive) && !invalid.clouds.front().transfer_result_code.empty(), "new strategy failure broke legacy port or other cloud");
        }
        {
            Fixture f;
            auto write = f.prepare(Destination::Gdrive);
            const auto repair = test::repair(test::job(Destination::Gdrive), write->attempt());
            const auto missing = f.assembly.destination(Destination::Gdrive)->inspect(repair, test::context());
            test::require(missing.isSuccess() && missing.data()->observation == Observation::Unknown && f.rc->uploads == 0, "in-flight absence enabled duplicate upload");
            f.rc->unknown = true;
            test::require(!write->publish(test::context()).isSuccess() && f.rc->uploads == 0, "network failure treated as absence");
        }
        {
            Fixture f;
            auto write = f.prepare(Destination::Gdrive);
            putRc(f, write->attempt());
            f.rc->wrong_hash = true;
            test::require(!write->publish(test::context()).isSuccess() && f.rc->uploads == 0, "different remote content was overwritten");
            f.rc->wrong_hash = false;
            f.rc->malformed = true;
            const auto inspect = f.assembly.destination(Destination::Gdrive)->inspect(test::repair(test::job(Destination::Gdrive), write->attempt()), test::context());
            test::require(inspect.isSuccess() && inspect.data()->observation == Observation::Unknown, "malformed metadata was accepted");
            f.rc->malformed = false;
            f.rc->wrong_readback_type = true;
            const auto wrong_type = f.assembly.destination(Destination::Gdrive)->inspect(test::repair(test::job(Destination::Gdrive), write->attempt()), test::context());
            test::require(wrong_type.isSuccess() && wrong_type.data()->observation == Observation::Unknown, "wrong readback algorithm was promoted");
        }
        {
            Fixture f;
            auto write = f.prepare(Destination::Gdrive);
            f.rc->dropped_reply = true;
            test::require(write->publish(test::context()).error() == c::application::result::ServiceErrorType::CommitUncertain, "lost upload reply was not uncertain");
            const auto inspected = f.assembly.destination(Destination::Gdrive)->inspect(test::repair(test::job(Destination::Gdrive), write->attempt()), test::context());
            test::require(inspected.isSuccess() && inspected.data()->observation == Observation::Matched && f.rc->uploads == 1, "lost reply caused another upload");
        }
        {
            Fixture f;
            auto write = f.prepare(Destination::Baidu);
            test::require(f.baidu->uploads == 0 && f.baidu->probes == 0, "Baidu prepare performed remote IO");
            const auto receipt = write->publish(test::context());
            test::require(receipt.isSuccess() && receipt.data()->uploaded && receipt.data()->verification == model::ResourceVerification::Sha256Verified && f.baidu->uploads == 1 && f.baidu->downloads == 1, "Baidu upload did not verify full file");
            const auto inspected = f.assembly.destination(Destination::Baidu)->inspect(test::repair(test::job(Destination::Baidu), write->attempt()), test::context());
            test::require(inspected.isSuccess() && inspected.data()->observation == Observation::Matched && !inspected.data()->receipt.uploaded && f.baidu->uploads == 1, "Baidu inspection uploaded again");
            test::require(std::filesystem::is_empty(f.root.path / "baidu-stage/.resource-cloud-readback") && !std::filesystem::exists(f.root.path / "formal-local"), "Baidu readback lease leaked or opened formal local");
            f.baidu->wrong_bytes = true;
            const auto bad = f.assembly.destination(Destination::Baidu)->inspect(test::repair(test::job(Destination::Baidu), write->attempt()), test::context());
            test::require(bad.isSuccess() && bad.data()->observation == Observation::Conflict, "same-size wrong bytes passed Baidu verification");
        }
        {
            Fixture f;
            auto write = f.prepare(Destination::Baidu);
            const auto old = test::repair(test::job(Destination::Baidu), write->attempt());
            test::require(f.assembly.destination(Destination::Baidu)->inspect(old, test::context()).data()->observation == Observation::Unknown, "missing unacknowledged Baidu object enabled retry");
            f.baidu->unknown = true;
            test::require(!write->publish(test::context()).isSuccess() && f.baidu->uploads == 0, "diagnostic number was treated as missing");
            auto limited = test::job(Destination::Baidu);
            limited.policy.bytes_per_second = 1024;
            const auto rejected = f.assembly.destination(Destination::Baidu)->prepare(limited, f.snapshot, test::context());
            test::require(!rejected.isSuccess() && rejected.message() == "baidu_task_rate_unsupported", "per-task limit silently changed shared config or was ignored");
        }
        {
            Fixture f;
            auto baidu_write = f.prepare(Destination::Baidu), google_write = f.prepare(Destination::Gdrive);
            std::promise<void> entered;
            auto reached = entered.get_future();
            std::atomic_bool cancel {false};
            f.baidu->on_upload = [&](const auto &stopped) { entered.set_value(); while (!stopped()) std::this_thread::sleep_for(std::chrono::milliseconds(5)); };
            auto budget = test::context();
            budget.cancelled = [&] { return cancel.load(); };
            auto slow = std::async(std::launch::async, [&] { return baidu_write->publish(budget); });
            test::require(reached.wait_for(std::chrono::seconds(2)) == std::future_status::ready, "Baidu did not reach independent barrier");
            const auto google = google_write->publish(test::context());
            const bool independent = google.isSuccess() && slow.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout;
            cancel.store(true);
            const auto cancelled = slow.get();
            test::require(independent && !cancelled.isSuccess() && f.rc->uploads == 1 && f.baidu->uploads == 0, "blocked/cancelled Baidu affected Google file upload");
        }
        {
            Fixture f;
            auto task = test::job(Destination::Gdrive);
            task.policy.bytes_per_second = 1024 * 1024;
            f.rc->expected_rate = task.policy.bytes_per_second;
            f.rc->sha_hash = true;
            const auto prepared = f.assembly.destination(Destination::Gdrive)->prepare(task, f.snapshot, test::context());
            test::require(prepared.isSuccess(), "rate-limited prepare failed");
            const auto receipt = (*prepared.data())->publish(test::context());
            test::require(receipt.isSuccess() && receipt.data()->verification == model::ResourceVerification::Sha256Verified && f.rc->readbacks == 0, "remote SHA-256 or per-call rate was not preserved");
            const auto repaired = test::repair(task, (*prepared.data())->attempt());
            auto foreign = repaired;
            foreign.resource.owner = model::ResourceOwner::Album;
            const auto before = f.rc->probes;
            test::require(!f.assembly.destination(Destination::Gdrive)->inspect(foreign, test::context()).isSuccess() && f.rc->probes == before, "foreign owner path was probed");
        }
        {
            Fixture f;
            auto write = f.prepare(Destination::Gdrive);
            test::writeFile(std::filesystem::u8path(f.snapshot->filePath()), f.snapshot->sizeBytes(), 'q');
            f.rc->expected_byte = 'q';
            test::require(!write->publish(test::context()).isSuccess() && f.rc->uploads == 0, "changed source emitted a complete multipart object");
        }
        {
            Fixture f;
            auto write = f.prepare(Destination::Baidu);
            test::require(write->publish(test::context()).isSuccess(), "Baidu size-bound fixture failed");
            auto repaired = test::repair(test::job(Destination::Baidu), write->attempt());
            const auto downloads = f.baidu->downloads;
            f.baidu->wrong_size = true;
            test::require(f.assembly.destination(Destination::Baidu)->inspect(repaired, test::context()).data()->observation == Observation::Conflict && f.baidu->downloads == downloads, "wrong size reached readback");
            f.baidu->wrong_size = false;
            f.baidu->wrong_path = true;
            test::require(f.assembly.destination(Destination::Baidu)->inspect(repaired, test::context()).data()->observation == Observation::Unknown && f.baidu->downloads == downloads, "different path was trusted");
            f.baidu->wrong_path = false;
            f.baidu->oversize_readback = true;
            test::require(!f.assembly.destination(Destination::Baidu)->inspect(repaired, test::context()).isSuccess() && std::filesystem::is_empty(f.root.path / "baidu-stage/.resource-cloud-readback"), "oversize readback bypassed cancellation or leaked files");
            auto config = f.config.baidupcs;
            config.local_root = f.root.path / "small-quota";
            c::infrastructure::storage::baidupcs::BaiduResourceTransfer tiny(config, [runner = f.baidu](const auto &line, auto timeout, const auto &stop) { return (*runner)(line, timeout, stop); }, 65536);
            const auto limited = tiny.prepare(test::job(Destination::Baidu), f.snapshot, test::context());
            test::require(!limited.isSuccess() && limited.message() == "baidu_readback_quota_exceeded" && f.baidu->uploads == 1 && !std::filesystem::exists(config.local_root), "quota exhaustion was not rejected before upload");
        }
        {
            Fixture f;
            auto config = f.config;
            const auto unicode_root = f.root.path / std::filesystem::path(u8"中文本地根");
            config.storage_path = test::utf8(unicode_root);
            const auto local = app::ResourceTransferFactory(config).build(true, {});
            test::require(local.local && local.local->getFullPath("probe.mrc") == unicode_root / "probe.mrc" && !std::filesystem::exists(unicode_root), "factory changed UTF-8 local root or created it during assembly");
            config.resource_cloud_upload_enabled = true;
            config.resource_cloud_upload_backend = "rclone";
            config.resource_cloud_provider = "baidu";
            config.rclone.rc_user = "fixture-user";
            config.rclone.rc_password = "fixture-only";
            expectRcConfig(*f.rc, config.rclone);
            app::ResourceTransferFactory factory(config);
            const auto legacy = factory.legacyCloud(f.rc);
            test::require(factory.legacyProvider() == Destination::Baidu && legacy && legacy->prefix() == config.rclone.target_fs + '/', "existing rclone configuration was not reusable for Baidu");
            auto selected = factory.build(false, {{Destination::Baidu, {}}}, f.rc);
            auto task = test::job(Destination::Baidu);
            task.adapter = "rclone";
            task.policy.bytes_per_second = 1024 * 1024;
            f.rc->expected_rate = task.policy.bytes_per_second;
            const auto prepared = selected.destination(Destination::Baidu)->prepare(task, f.snapshot, test::context());
            test::require(prepared.isSuccess(), "rclone Baidu prepare was still tied to Google");
            const auto receipt = (*prepared.data())->publish(test::context());
            test::require(receipt.isSuccess() && receipt.data()->adapter == "rclone" && f.rc->uploads == 1 && f.baidu->uploads == 0 && !std::filesystem::exists(f.root.path / "formal-local"), "Baidu ignored selected rclone adapter");
            config.resource_cloud_provider.clear();
            app::ResourceTransferFactory undeclared(config);
            test::require(!undeclared.legacyProvider() && undeclared.legacyCloud(f.rc), "unclassified rclone was guessed as Google or its legacy port was discarded");
            const auto ambiguous = undeclared.build(false, {{Destination::Baidu, {}}, {Destination::Gdrive, {}}}, f.rc);
            test::require(!ambiguous.destination(Destination::Baidu) && !ambiguous.destination(Destination::Gdrive) && ambiguous.clouds[0].result_code == "cloud_target_ambiguous" && ambiguous.clouds[1].result_code == "cloud_target_ambiguous", "one rclone remote was mislabeled as two platforms");
        }
        {
            Fixture f;
            auto baidu_rc = std::make_shared<test::RcClient>(), google_rc = std::make_shared<test::RcClient>();
            baidu_rc->expected = google_rc->expected = f.snapshot->digest;
            expectRcConfig(*baidu_rc, f.config.rclone);
            auto other = f.config.rclone;
            other.rc_url = "http://127.0.0.1:5573";
            other.target_fs = "second:Other";
            other.rc_user = "other-fixture";
            other.rc_password = "other-fixture-only";
            expectRcConfig(*google_rc, other);
            app::ResourceCloudTarget baidu {Destination::Baidu, {}}, google {Destination::Gdrive, {}};
            baidu.adapter = google.adapter = "rclone";
            baidu.rclone_client = baidu_rc;
            google.rclone = other;
            google.rclone_client = google_rc;
            const auto assembly = app::ResourceTransferFactory(f.config).build(false, {baidu, google});
            auto baidu_job = test::job(Destination::Baidu);
            baidu_job.adapter = "rclone";
            const auto first = assembly.destination(Destination::Baidu)->prepare(baidu_job, f.snapshot, test::context());
            const auto second = assembly.destination(Destination::Gdrive)->prepare(test::job(Destination::Gdrive), f.snapshot, test::context());
            test::require(first.isSuccess() && second.isSuccess() && (*first.data())->attempt().path.starts_with("archive:Media/") && (*second.data())->attempt().path.starts_with("second:Other/"), "two rclone targets shared one mutable root/configuration");
            std::promise<void> entered, release;
            auto ready = entered.get_future();
            auto released = release.get_future().share();
            std::atomic_bool cancelled {false};
            baidu_rc->on_upload = [&] { entered.set_value(); released.wait(); };
            auto context = test::context();
            context.cancelled = [&] { return cancelled.load(); };
            auto stalled = std::async(std::launch::async, [&] { return (*first.data())->publish(context); });
            // 失败同样释放屏障，不能让 future 析构永久等待。
            struct Release {
                std::atomic_bool &cancelled;
                std::promise<void> &release;
                ~Release()
                {
                    cancelled.store(true);
                    try {
                        release.set_value();
                    } catch (...) {}
                }
            } cleanup {cancelled, release};
            test::require(ready.wait_for(std::chrono::seconds(2)) == std::future_status::ready, "rclone Baidu did not reach its own upload barrier");
            const auto healthy = (*second.data())->publish(test::context());
            const bool independent = healthy.isSuccess() && stalled.wait_for(std::chrono::milliseconds(0)) == std::future_status::timeout;
            cancelled.store(true);
            release.set_value();
            test::require(!stalled.get().isSuccess() && independent && google_rc->uploads == 1 && baidu_rc->uploads == 0 && f.baidu->uploads == 0, "two rclone targets shared cancellation or transfer state");
        }
        std::cout << "CloudResourceTransferTest passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "CloudResourceTransferTest failed: " << error.what() << '\n';
        return 1;
    }
}
