#include "crawler/infrastructure/storage/filesystem/ResourceArtifactProducer.h"
#include "crawler/infrastructure/storage/filesystem/ResourceSnapshotSource.h"
#include "crawler/infrastructure/storage/filesystem/ResourceFileDigest.h"
#include "crawler/infrastructure/music/lyrics/TimedLyricsEncoder.h"
#include "crawler/infrastructure/music/migu/parser/MiguSongResourceParser.h"
#include "migu/support/MetadataCoverTestSupport.h"

#include <fstream>
#include <iostream>

namespace c = orchestrator::modules::crawler;
namespace model = c::application::model;
namespace ports = c::application::ports;
namespace files = c::infrastructure::storage::filesystem;
namespace test = c::tests::cover;
template <class T>
using Result = c::application::result::ServiceResult<T>;
namespace {
class Decoder final : public ports::TimedLyricsDecoder {
public:
    mutable int calls = 0;
    Result<c::domain::TimedLyrics> decode(std::string_view content) const override
    {
        ++calls;
        test::require(content == "fixture-mrc", "producer read unexpected input");
        c::domain::TimedLyrics lyrics;
        lyrics.lines.push_back({0, 500, {{"word", 0, 500}}});
        return Result<c::domain::TimedLyrics>::success(lyrics);
    }
    std::string_view version() const override
    {
        return "fixture-v1";
    }
};
class Snapshot final : public model::ResourceContentSnapshot {
public:
    std::string path, relative = "source.mrc";
    files::ResourceFileDigest digest;
    const std::string &filePath() const override
    {
        return path;
    }
    const std::string &relativePath() const override
    {
        return relative;
    }
    const std::string &checksum() const override
    {
        return digest.sha256;
    }
    std::uint64_t sizeBytes() const override
    {
        return digest.size_bytes;
    }
};
class Descriptions final : public ports::ResourceTransferSources {
public:
    model::ResourceArtifactInput input;
    std::optional<model::SongResourceQueueTarget> song;
    Result<model::ResourceTransferSourceInput> describe(const model::ResourceTransferJob &) override
    {
        if (song) return Result<model::ResourceTransferSourceInput>::success(*song);
        return Result<model::ResourceTransferSourceInput>::success(input);
    }
};
class Provider final : public ports::SongResourceProvider {
public:
    int calls = 0;
    std::optional<Response> fetchSongResource(model::SongResourceFetchRequest) override
    {
        ++calls;
        return std::nullopt;
    }
};
class Content final : public ports::SongResourceContentFetcher {
public:
    int calls = 0;
    model::SongResourceContentFetchResult fetchSongResourceContent(model::SongResourceContentFetchRequest) override
    {
        ++calls;
        model::SongResourceContentFetchResult value;
        value.ok = true;
        value.status_code = 200;
        value.content_type = "application/octet-stream";
        const std::string body = "download-only-mrc";
        value.body.assign(body.begin(), body.end());
        return value;
    }
};
} // namespace

int main()
{
    try {
        test::TempRoot root;
        Snapshot snapshot;
        const auto source_path = root.path / std::filesystem::path(u8"歌词源.mrc");
        const auto source_utf8 = source_path.generic_u8string();
        snapshot.path.assign(reinterpret_cast<const char *>(source_utf8.data()), source_utf8.size());
        {
            std::ofstream output(source_path, std::ios::binary);
            output << "fixture-mrc";
        }
        model::ResourceTransferContext context {std::chrono::steady_clock::now() + std::chrono::seconds(5), {}};
        snapshot.digest = *files::digestResourceFile(source_path, 1024, context).data();
        auto decoder = std::make_shared<Decoder>();
        auto encoder = std::make_shared<c::infrastructure::music::lyrics::TimedLyricsEncoder>();
        auto producer = std::make_shared<files::ResourceArtifactProducer>(decoder, encoder);
        model::ResourceTransferJob parent;
        parent.resource = {model::ResourceOwner::Song, 19};
        parent.task_id = 7;
        parent.claim_generation = 2;
        parent.operation_id = "rt_fixture";
        parent.adapter = "rclone";
        parent.target_fingerprint = std::string(64, 'a');
        model::LyricArtifactBundle bundle;
        for (const auto destination : {model::ResourceDestination::Baidu, model::ResourceDestination::Gdrive}) {
            parent.destination = destination;
            const auto generated = producer->generate(parent, snapshot, context);
            test::require(generated.isSuccess(), "producer failed cloud-only source");
            bundle = *generated.data();
        }
        test::require(decoder->calls == 1 && bundle.files.size() == 2, "two clouds repeated decryption");
        for (const auto &file : bundle.files) test::require(file.local_path.empty() && file.checksum.size() == 64, "generator invented formal local facts");

        auto descriptions = std::make_shared<Descriptions>();
        descriptions->input.artifact_type = bundle.files[0].kind;
        descriptions->input.source_checksum = snapshot.checksum();
        descriptions->input.processor_version = bundle.processor_version;
        descriptions->input.schema_version = bundle.schema_version;
        descriptions->input.checksum = bundle.files[0].checksum;
        descriptions->input.size_bytes = bundle.files[0].size_bytes;
        files::ResourceSnapshotDependencies dependencies;
        dependencies.descriptions = descriptions;
        dependencies.producer = producer;
        auto shared_content = std::make_shared<Content>();
        dependencies.songs = std::make_shared<Provider>();
        dependencies.content = shared_content;
        dependencies.parser = std::make_shared<c::infrastructure::music::migu::parser::MiguSongResourceParser>();
        dependencies.artifacts = std::make_shared<files::FileStorage>(root.path / "formal", false);
        const std::array<std::filesystem::path, 3> roots {root.path / "local", root.path / "baidu", root.path / "google"};
        files::ResourceSnapshotSource source(dependencies, roots, 2 * 1024 * 1024);
        auto job = parent;
        job.resource = {model::ResourceOwner::SongArtifact, 31};
        job.policy.max_bytes = 1024 * 1024;
        job.destination = model::ResourceDestination::Baidu;
        auto left = source.acquire(job, context);
        job.destination = model::ResourceDestination::Gdrive;
        auto right = source.acquire(job, context);
        test::require(left.isSuccess() && right.isSuccess() && (*left.data())->checksum() == (*right.data())->checksum() && (*left.data())->filePath() != (*right.data())->filePath(), "cloud-only generated snapshots were not independent");
        test::require(!std::filesystem::exists(root.path / "formal") && !std::filesystem::exists(roots[0]) && decoder->calls == 1, "producer created formal/local root or repeated decoder");
        const auto content = producer->regenerate(descriptions->input, snapshot, context);
        test::require(content.isSuccess() && decoder->calls == 1, "regeneration ignored shared cache");
        auto restarted = std::make_shared<files::ResourceArtifactProducer>(decoder, encoder);
        test::require(restarted->regenerate(descriptions->input, snapshot, context).isSuccess() && decoder->calls == 2, "restart could not regenerate controlled source");
        descriptions->input.source_checksum = std::string(64, 'f');
        test::require(!restarted->regenerate(descriptions->input, snapshot, context).isSuccess(), "changed original source was accepted");
        auto cancelled = context;
        cancelled.cancelled = [] { return true; };
        test::require(!producer->generate(parent, snapshot, cancelled).isSuccess(), "cancelled producer registered new children");
        auto no_key = std::make_shared<files::ResourceArtifactProducer>(decoder, encoder, false);
        auto provider = std::make_shared<Provider>();
        dependencies.producer = no_key;
        dependencies.songs = provider;
        files::ResourceSnapshotSource missing_key(dependencies, roots, 2 * 1024 * 1024);
        const auto unavailable = missing_key.acquire(job, context);
        test::require(!unavailable.isSuccess() && unavailable.message() == "waiting_input" && provider->calls == 0, "missing key requested regeneration source");
        dependencies.producer = producer;
        descriptions->song = model::SongResourceQueueTarget {};
        descriptions->song->resource_id = 19;
        descriptions->song->song_source_ref_id = 11;
        descriptions->song->source_content_id = "fixture19";
        descriptions->song->resource_kind = "mrc";
        descriptions->song->remote_url = "https://example.test/source.mrc";
        const auto decoded_before_download = decoder->calls;
        parent.policy.max_bytes = 1024 * 1024;
        const auto downloaded = source.acquire(parent, context);
        test::require(downloaded.isSuccess() && decoder->calls == decoded_before_download, "MRC download still synchronously generated lyrics");
        const auto calls_before_quota = shared_content->calls;
        const auto full = source.acquire(parent, context);
        test::require(!full.isSuccess() && full.message() == "snapshot_quota_exhausted" && shared_content->calls == calls_before_quota, "MRC and artifact leases bypassed shared target staging quota");
        std::cout << "artifact producer: cloud-only generation, shared decoding, private snapshots, restart regeneration and cancellation passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "artifact producer test failed: " << error.what() << '\n';
        return 1;
    }
}
