#include "crawler/application/use_cases/LyricProcessingWorkflow.h"

#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>

namespace {

namespace crawler = orchestrator::modules::crawler;
namespace model = crawler::application::model;
namespace ports = crawler::application::ports;
namespace result = crawler::application::result;

// 测试断言不受构建模式影响。
void require(bool condition, const std::string &message)
{
    if (!condition) throw std::runtime_error(message);
}

// 顺序测试专用 decoder，计数用于验证错误时没有继续调用下游。
struct Decoder final : ports::TimedLyricsDecoder {
    bool fail = false;     // 模拟解析失败
    mutable int calls = 0; // 解码调用次数，测试中只顺序访问
    // 模拟一个已验证的逐字模型。
    result::ServiceResult<crawler::domain::TimedLyrics> decode(std::string_view) const override
    {
        ++calls;
        if (fail) return result::ServiceResult<crawler::domain::TimedLyrics>::failure(result::ServiceErrorType::InvalidParam, "mrc_invalid_timing");
        return result::ServiceResult<crawler::domain::TimedLyrics>::success({{}, {{0, 10, {{"甲", 0, 10}}}}});
    }
    // 测试版本固定，便于验证版本进入持久化输入。
    std::string_view version() const override
    {
        return "test-v1";
    }
};

// 顺序测试专用导出器。
struct Encoder final : ports::LyricArtifactEncoder {
    // 非默认版本用于验证用例只转交版本，不写死存储格式。
    int schemaVersion() const override
    {
        return 7;
    }
    // 独立导出版本进入产物身份，不由 application 写死。
    std::string_view version() const override
    {
        return "exports-v1";
    }
    // 模拟从统一模型导出的两个格式。
    result::ServiceResult<std::vector<model::EncodedLyricArtifact>> encode(const crawler::domain::TimedLyrics &) const override
    {
        return result::ServiceResult<std::vector<model::EncodedLyricArtifact>>::success({{"word_json", "words.json", "application/json", "{}"}, {"enhanced_lrc", "words.enhanced.lrc", "text/plain", "[00:00.000]甲"}});
    }
};

// 顺序测试专用存储，校验处理器不会直接访问文件系统。
struct Store final : ports::LyricArtifactStore {
    mutable int published = 0; // 已请求发布的次数
    bool fail_publish = false; // 模拟文件替换失败
    // 返回来源和校验值，供用例检验代次。
    result::ServiceResult<model::LyricSourceContent> readSource(std::string_view, std::size_t) const override
    {
        return result::ServiceResult<model::LyricSourceContent>::success({"encrypted", "source-hash"});
    }
    // 返回完整集合；正文由此边界终止，不交给数据库。
    result::ServiceResult<model::LyricArtifactBundle> publish(const model::LyricProcessingRequest &request, std::string_view hash, std::string_view version, int schema_version, const std::vector<model::EncodedLyricArtifact> &) const override
    {
        ++published;
        if (fail_publish) return result::ServiceResult<model::LyricArtifactBundle>::failure(result::ServiceErrorType::InternalError, "lyric_artifact_publish_failed");
        return result::ServiceResult<model::LyricArtifactBundle>::success({request.source_resource_id, std::string(hash), std::string(version), schema_version, {{"word_json", "words.json", 2, "json-hash"}, {"enhanced_lrc", "words.lrc", 10, "lrc-hash"}}});
    }
};

// 顺序测试专用索引，可模拟提交后的数据库不可用。
struct Repository final : ports::LyricArtifactRepository {
    bool fail = false;               // 模拟数据库故障
    int recorded = 0;                // 成功登记调用次数
    int failures = 0;                // 独立失败登记次数
    model::LyricArtifactBundle last; // 最近登记内容
    // 索引只能收到描述，不能收到歌词正文。
    result::ServiceResult<bool> record(const model::LyricArtifactBundle &bundle) override
    {
        ++recorded;
        last = bundle;
        if (fail) return result::ServiceResult<bool>::failure(result::ServiceErrorType::InternalError, "lyric_index_write_failed");
        return result::ServiceResult<bool>::success(true);
    }
    // 失败不回写原始资源队列。
    result::ServiceResult<bool> recordFailure(const model::LyricProcessingRequest &, std::string_view, int, std::string_view) override
    {
        ++failures;
        return result::ServiceResult<bool>::success(true);
    }
};

// 覆盖完整编排、源文件变化、解析失败、索引故障及离线模式。
void testWorkflowBoundaries()
{
    auto decoder = std::make_shared<Decoder>();
    auto store = std::make_shared<Store>();
    auto repository = std::make_shared<Repository>();
    crawler::application::use_cases::LyricProcessingWorkflow workflow({decoder, std::make_shared<Encoder>(), store, repository}, "migu");
    const model::LyricProcessingRequest request {11, "song/lyrics/mrc.mrc", "source-hash"};
    const auto success = workflow.processFile(request);
    require(success.isSuccess() && repository->recorded == 1, "complete artifacts should be indexed");
    require(repository->last.processor_version == "test-v1-exports-v1", "decoder and export versions must enter the index");
    require(repository->last.schema_version == 7, "schema version must come from encoder instead of application");
    auto changed = request;
    changed.expected_checksum = "newer-source";
    const auto stale = workflow.processFile(changed);
    require(!stale.isSuccess() && stale.message() == "lyric_source_changed", "changed raw source must fail");
    require(decoder->calls == 1 && store->published == 1, "changed source must not be decoded or published");
    decoder->fail = true;
    require(!workflow.processFile(request).isSuccess(), "parse error must be returned");
    require(store->published == 1 && repository->failures == 2, "parse failure must not create output files");
    decoder->fail = false;
    store->fail_publish = true;
    const auto file_failure = workflow.processFile(request);
    require(!file_failure.isSuccess() && file_failure.message() == "lyric_artifact_publish_failed", "file publication failure must be returned");
    require(repository->recorded == 1 && repository->failures == 3, "incomplete files must not be indexed as completed artifacts");
    store->fail_publish = false;
    repository->fail = true;
    const auto unavailable = workflow.processFile(request);
    require(!unavailable.isSuccess() && unavailable.message() == "lyric_index_write_failed", "database failure cannot report success");
    model::SongResourceProcessItem other;
    other.ok = true;
    other.resource_kind = "audio";
    const auto skipped = workflow.process(other);
    require(skipped.isSuccess() && !*skipped.data(), "unrelated resource types must skip");
    crawler::application::use_cases::LyricProcessingWorkflow offline({decoder, std::make_shared<Encoder>(), store, nullptr}, "migu");
    require(offline.processFile({0, request.local_path, {}}).isSuccess(), "offline export should not require database access");
}

} // namespace

// 执行 application 边界回归，所有外部依赖由窄 port 替身提供。
int main()
{
    try {
        testWorkflowBoundaries();
        std::cout << "LyricProcessingWorkflowTest passed.\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "LyricProcessingWorkflowTest failed: " << error.what() << '\n';
        return 1;
    }
}
