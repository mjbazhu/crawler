#include "crawler/application/use_cases/MetadataCoverDownloadWorkflow.h"
#include "crawler/application/use_cases/MetadataCoverService.h"
#include "migu/support/MetadataCoverTestSupport.h"

#include <future>
#include <iostream>

namespace test = orchestrator::modules::crawler::tests::cover;
namespace model = test::model;
namespace use = orchestrator::modules::crawler::application::use_cases;

// 验证独立封面编排及真实文件补偿，不连接平台或数据库。
int main()
{
    try {
        test::TempRoot root;
        auto files = std::make_shared<test::files::FileStorage>(root.path);
        auto http = std::make_shared<test::Http>();
        auto store = std::make_shared<test::files::MetadataCoverStore>(http, files, std::vector<std::string> {});
        auto queue = std::make_shared<test::Queue>();
        const auto singer = test::target(model::CoverOwner::Singer);
        const auto album = test::target(model::CoverOwner::Album);
        queue->rows[{singer.owner, singer.resource_id}] = singer;
        queue->rows[{album.owner, album.resource_id}] = album;
        auto workflow = std::make_shared<use::MetadataCoverDownloadWorkflow>(queue, store, store);
        model::MetadataCoverOptions options;
        queue->before_success = [&](const auto &receipt) { test::require(files->exists(receipt.local_path), "DB success preceded complete file"); };
        auto downloaded = workflow->process(singer, options);
        test::require(downloaded.success && downloaded.record.file.size_bytes == test::png().size() && downloaded.record.file.checksum.size() == 64, "singer download did not produce a complete receipt");
        test::require(store->read(downloaded.record.file).has_value(), "saved image cannot be read or verified");
        const int calls = http->calls;
        test::require(workflow->process(downloaded.record, options).skipped && http->calls == calls, "completed cover was downloaded twice");
        const auto album_file = workflow->process(album, options);
        test::require(album_file.success && album_file.record.file.local_path != downloaded.record.file.local_path && album_file.record.file.local_path.starts_with("migu/albums/"), "same ID in two owner tables was mixed");

        options.refresh = true;
        http->status = 503;
        const auto old = downloaded.record.file.local_path;
        auto failed = workflow->process(downloaded.record, options);
        test::require(!failed.success && failed.record.available && failed.record.file.local_path == old && files->exists(old), "failed refresh destroyed old image");
        test::require(failed.record.file.result_code == "http_503", "HTTP failure classification was lost");
        http->status = 200;
        queue->before_success = [&](const auto &receipt) { test::require(files->exists(old) && files->exists(receipt.local_path), "old image disappeared before DB acknowledgement"); };
        downloaded = workflow->process(**queue->find(singer.owner, 1).data(), options);
        test::require(downloaded.success && !files->exists(old), "confirmed refresh did not retire old image");
        queue->before_success = {};

        // URL 在请求期间更新，旧快照只能释放认领，不能替换成功图片。
        const auto current_path = downloaded.record.file.local_path;
        http->on_get = [&] { queue->rows[{singer.owner, 1}].remote_url = "https://example.test/new.png"; };
        failed = workflow->process(downloaded.record, options);
        test::require(!failed.success && files->exists(current_path) && failed.record.file.result_code == "cover_claim_changed", "old URL snapshot overwrote new metadata");
        http->on_get = {};

        // 取消发生在内容下载后，不得写成功或破坏旧回执。
        bool cancelled = false;
        http->on_get = [&] { cancelled = true; };
        failed = workflow->process(**queue->find(singer.owner, 1).data(), options, {}, [&] { return cancelled; });
        test::require(!failed.success && failed.record.file.result_code == "cancelled" && files->exists(current_path), "cancellation committed a cover or lost old image");
        http->on_get = {};

        // 成功响应丢失时保留独占文件与日志，启动核对按真实回执结束补偿。
        queue->uncertain = true;
        queue->committed_before_disconnect = true;
        failed = workflow->process(**queue->find(singer.owner, 1).data(), options);
        test::require(!failed.success && failed.record.file.result_code == "writeback_uncertain", "uncertain commit was reported as confirmed");
        const auto committed = **queue->find(singer.owner, 1).data();
        test::require(files->exists(committed.file.local_path), "uncertain committed file was deleted");
        const auto recovered = files->recoverOwnedResourceWrites([&](const std::string &owner, std::uint64_t id, std::uint64_t, const std::string &path) -> std::optional<model::ResourceWriteDisposition> {
            test::require(owner == "singer" && id == 1, "journal lost cover owner or resource ID");
            return path == committed.file.local_path ? model::ResourceWriteDisposition::Committed : model::ResourceWriteDisposition::Discard;
        });
        test::require(recovered == 1 && files->exists(committed.file.local_path) && !files->exists(current_path), "confirmed journal recovery failed");

        // 两个任务读取同一待处理快照，只有一个能认领；恢复后旧代次被拒绝。
        const auto pending = test::target(model::CoverOwner::Singer, 2);
        queue->rows[{pending.owner, 2}] = pending;
        auto first = std::async(std::launch::async, [&] { return queue->claim(pending, false, "one"); });
        auto second = std::async(std::launch::async, [&] { return queue->claim(pending, false, "two"); });
        const auto a = first.get(), b = second.get();
        test::require(static_cast<int>(a.data()->has_value()) + static_cast<int>(b.data()->has_value()) == 1, "concurrent claim was not exclusive");
        const auto claimed = a.data()->has_value() ? **a.data() : **b.data();
        queue->uncertain = false;
        model::MetadataCoverQuery query;
        test::require(*queue->recover(query, 1).data() == 1 && !*queue->succeed(claimed, committed.file).data(), "stale generation overwrote recovered resource");
        files->remove(committed.file.local_path);
        options.refresh = false;
        const auto missing = workflow->process(committed, options);
        test::require(!missing.success && missing.record.file.result_code == "local_file_missing", "missing completed file was reported as available");
        std::cout << "PASS: cover ownership, file acknowledgement, reuse, refresh, URL race, cancellation, uncertain recovery, concurrent claims and missing files\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
