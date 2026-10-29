#include "crawler/application/use_cases/MetadataCoverCloudWorkflow.h"
#include "crawler/application/use_cases/MetadataCoverService.h"
#include "migu/support/MetadataCoverCloudTestSupport.h"

#include <chrono>
#include <iostream>
#include <memory>
#include <stdexcept>

namespace c = orchestrator::modules::crawler;
namespace test = c::tests::cover;
namespace model = c::application::model;
namespace use = c::application::use_cases;

// 每个用例使用独立根和记录，同时验证自动串联与历史文件补传。
int main()
{
    try {
        test::TempRoot root;
        auto rows = std::make_shared<test::Queue>();
        rows->rows[{model::CoverOwner::Singer, 1}] = test::target(model::CoverOwner::Singer);
        rows->rows[{model::CoverOwner::Album, 1}] = test::target(model::CoverOwner::Album);
        auto http = std::make_shared<test::Http>();
        auto files = std::make_shared<test::files::FileStorage>(root.path);
        auto local = std::make_shared<test::files::MetadataCoverStore>(http, files, std::vector<std::string> {});
        auto download = std::make_shared<use::MetadataCoverDownloadWorkflow>(rows, local, local);
        auto queue = std::make_shared<test::CloudQueue>(rows);
        auto remote = std::make_shared<test::CloudStore>();
        auto cloud = std::make_shared<use::MetadataCoverCloudWorkflow>(queue, local, remote, 1000);
        use::MetadataCoverService service(rows, local, download, nullptr, nullptr, nullptr, std::chrono::seconds(20), cloud);
        model::MetadataCoverOptions options;
        auto result = service.process(options);
        test::require(result.isSuccess() && result.data()->size() == 1 && result.data()->front().success, "enabled flow did not upload after local confirmation");
        test::require(remote->calls == 1 && http->calls == 1, "enabled cover flow called wrong phases");
        const auto singer = **rows->find(model::CoverOwner::Singer, 1).data();
        test::require(singer.file.status == 2 && singer.cloud.status == 2 && singer.cloud.receipt.path == "/Media/" + singer.file.local_path, "cloud receipt was not independently persisted");
        auto repeated = service.process(options);
        test::require(repeated.isSuccess() && remote->calls == 1 && http->calls == 1, "completed cover repeated local or remote download");

        // 后端切换复用同一图片；同数字 ID 的专辑仍有独立归属。
        remote->provider = "rclone";
        remote->root = "gcloud:Media/";
        auto switched = service.process(options);
        test::require(switched.isSuccess() && switched.data()->front().success && remote->calls == 2 && http->calls == 1, "backend switch did not upload existing bytes only");
        options.query.owner = model::CoverOwner::Album;
        test::require(service.process(options).data()->front().success, "album cloud path did not share the workflow");
        test::require((**rows->find(model::CoverOwner::Album, 1).data()).cloud.receipt.path.find("gcloud:Media/migu/albums/") == 0, "album used singer namespace");

        // 上传失败保留本地成功；重试不再次获取 Migu 图片。
        remote->root = "gcloud:Other/";
        remote->fail = true;
        options.retries = 0;
        const int before = http->calls;
        auto failed = service.process(options);
        test::require(!failed.data()->front().success && http->calls == before, "cloud failure repeated local download");
        auto stored = **rows->find(model::CoverOwner::Album, 1).data();
        test::require(stored.file.status == 2 && local->read(stored.file).has_value() && stored.cloud.status == 3 && stored.cloud.receipt.path.starts_with("gcloud:Media/"), "cloud failure destroyed local completion or last confirmed receipt");
        remote->fail = false;
        test::require(service.process(options).data()->front().success && http->calls == before, "cloud retry did not reuse saved image");

        // 错误回执不能让同大小但不同摘要对象被标记为成功。
        remote->root = "gcloud:Mismatch/";
        remote->bad_receipt = true;
        test::require(!service.process(options).data()->front().success, "bad cloud receipt was accepted");
        remote->bad_receipt = false;
        remote->during_upload = [&] {
            auto &row = rows->rows.at({model::CoverOwner::Album, 1});
            ++row.cloud.claim_generation;
            row.cloud.status = 0;
        };
        test::require(!service.process(options).data()->front().success, "old cloud generation overwrote replacement");
        remote->during_upload = {};

        // 云端未知保持可恢复状态；关闭云端的门面不调用存储后端。
        remote->unknown = true;
        test::require(!service.process(options).data()->front().success, "uncertain cloud operation was reported as confirmed");
        test::require((**rows->find(model::CoverOwner::Album, 1).data()).cloud.status == 4, "uncertain upload did not remain pending verification");
        const int calls = remote->calls;
        use::MetadataCoverService disabled(rows, local, download, nullptr, nullptr, nullptr);
        disabled.process({});
        test::require(remote->calls == calls, "disabled cloud mode invoked uploader");
        std::cout << "PASS: shared cover cloud flow, both owners/backends, reuse, retry, mismatch and generation safety\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
