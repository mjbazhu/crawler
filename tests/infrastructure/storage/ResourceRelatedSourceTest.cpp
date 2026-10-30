#include "crawler/infrastructure/storage/filesystem/ResourceSnapshotSource.h"
#include "infrastructure/storage/support/CloudTransferTestSupport.h"

#include <iostream>
#include <stdexcept>

namespace c = orchestrator::modules::crawler;
namespace model = c::application::model;
namespace ports = c::application::ports;
namespace result = c::application::result;
namespace test = c::tests::cloud_transfer;
namespace files = c::infrastructure::storage::filesystem;
namespace {
// 固定歌曲元数据，不使用真实来源或URL。
class Description final : public ports::ResourceTransferSources {
public:
    result::ServiceResult<model::ResourceTransferSourceInput> describe(const model::ResourceTransferJob &) override
    {
        model::SongResourceQueueTarget value;
        value.resource_id = 1;
        value.song_source_ref_id = 1;
        value.source = "migu";
        value.source_content_id = "fixture-content";
        value.copyright_id = "fixture-copyright";
        value.resource_kind = "audio";
        value.format_type = "PQ";
        value.source_resource_type = "2";
        return result::ServiceResult<model::ResourceTransferSourceInput>::success(value);
    }
};
// 只返回无敏感正文的协议成功壳。
class Provider final : public ports::SongResourceProvider {
public:
    int calls = 0; // 实际listen次数
    std::optional<Response> fetchSongResource(model::SongResourceFetchRequest) override
    {
        ++calls;
        Response value;
        value.status_code = 200;
        return value;
    }
};
// 模拟已有合法parser的有限结果，验证来源不会保存这些URL。
class Parser final : public ports::SongResourceParser {
public:
    bool allowed = true; // 永久拒绝与完整音频区分
    model::SongResourceFetchResult parseSongResource(const std::vector<char> &) const override
    {
        model::SongResourceFetchResult value;
        value.ok = allowed;
        value.code = allowed ? "000000" : "210001";
        value.url = allowed ? "https://fixture.invalid/audio.mp3" : "";
        value.cover_url = "https://fixture.invalid/cover.jpg";
        value.mrc_url = "https://fixture.invalid/words.mrc";
        return value;
    }
};
// 真实有界分片接口，不回退完整内存下载。
class Content final : public ports::SongResourceContentFetcher {
public:
    int calls = 0; // 永久拒绝不得请求音频正文
    model::SongResourceContentFetchResult fetchSongResourceContent(model::SongResourceContentFetchRequest) override
    {
        throw std::runtime_error("unexpected memory download");
    }
    model::SongResourceContentStreamResult streamSongResourceContent(model::SongResourceContentFetchRequest, model::SongResourceContentStreamCallbacks callbacks) override
    {
        ++calls;
        test::require(!callbacks.on_response || callbacks.on_response({200, "audio/mpeg"}), "fixture header rejected");
        test::require(!callbacks.on_chunk || callbacks.on_chunk("fixture-audio"), "fixture chunk rejected");
        model::SongResourceContentStreamResult value;
        value.ok = true;
        value.status_code = 200;
        value.content_type = "audio/mpeg";
        value.bytes_received = 13;
        return value;
    }
};
} // namespace

int main()
{
    try {
        test::Root root;
        auto provider = std::make_shared<Provider>();
        auto parser = std::make_shared<Parser>();
        auto content = std::make_shared<Content>();
        files::ResourceSnapshotDependencies dependencies;
        dependencies.descriptions = std::make_shared<Description>();
        dependencies.songs = provider;
        dependencies.parser = parser;
        dependencies.content = content;
        const std::array<std::filesystem::path, 3> roots {root.path / "local", root.path / "baidu", root.path / "gdrive"};
        files::ResourceSnapshotSource source(dependencies, roots, 1024 * 1024);
        auto job = test::job(model::ResourceDestination::Local);
        job.policy.max_bytes = 1024 * 1024;
        job.process_related_resources = true;
        {
            const auto first = source.acquire(job, test::context());
            test::require(first.isSuccess() && (*first.data())->relatedMask() == 5 && content->calls == 1, "legal listen kinds were lost or content failed");
        }
        job.process_related_resources = false;
        {
            const auto plain = source.acquire(job, test::context());
            test::require(plain.isSuccess() && (*plain.data())->relatedMask() == 0, "explicitly disabled related scope was widened");
        }
        job.process_related_resources = true;
        parser->allowed = false;
        auto context = test::context();
        std::optional<std::uint8_t> mask;
        context.related_discovered = [&](std::uint8_t value) { mask = value; };
        const auto restricted = source.acquire(job, context);
        test::require(!restricted.isSuccess() && restricted.error() == result::ServiceErrorType::ResourceRestricted && mask && *mask == 5 && content->calls == 2, "permanent restriction bypassed audio or lost allowed related scope");
        const auto calls = provider->calls;
        context.cancelled = [] { return true; };
        test::require(!source.acquire(job, context).isSuccess() && provider->calls == calls, "cancelled source performed listen");
        std::cout << "PASS: related kind capture, explicit scope, permanent audio restriction and cancellation\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "related source test failed: " << error.what() << '\n';
        return 1;
    }
}
