#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceTransferJobs.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMetadataBatchWriter.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlLegacyResourceImport.h"
#include "migu/support/MetadataCoverMySqlTestSupport.h"

#include <iostream>
#include <algorithm>
#include <stdexcept>

namespace c = orchestrator::modules::crawler;
namespace mysql = c::infrastructure::music::persistence::mysql;
namespace model = c::application::model;
namespace support = c::tests::migu::support;
namespace {
// 固定诊断不回显连接、URL或文件内容。
void require(bool value, const char *message)
{
    if (!value) throw std::runtime_error(message);
}
// 测试只访问显式隔离库。
boost::mysql::results execute(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, std::string sql)
{
    return support::runOnExecutor(pool->executor(), pool->coExecute(std::move(sql)), "related fixture");
}
// 受控计数用于核对事务副作用。
std::uint64_t scalar(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, std::string sql)
{
    auto rows = execute(pool, std::move(sql));
    return mysql::MySqlValueReader::uint64At(rows.rows().front(), 0);
}
// 验证显式业务结果，不依赖assert构建模式。
template <class T>
T get(c::application::result::ServiceResult<T> value)
{
    if (!value.isSuccess()) throw std::runtime_error(value.message());
    return *value.data();
}
// 按稳定输入创建音频资源，不调用音乐平台。
std::uint64_t audio(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, int sequence)
{
    model::SongSourceMetadata song;
    song.source = "migu";
    song.source_song_id = "related-song-" + std::to_string(sequence);
    song.source_content_id = "related-content-" + std::to_string(sequence);
    song.title = "关联事务测试";
    song.audio_formats.push_back({"PQ", "1", "020010", "020010", 64, 100});
    if (sequence == 3) song.audio_formats.push_back({"HQ", "2", "020020", "020020", 128, 200});
    model::MetadataWriteBatch batch;
    batch.songs = {song};
    require(mysql::MySqlMetadataBatchWriter(pool).saveBatch(batch).isSuccess(), "audio fixture failed");
    return scalar(pool, "SELECT r.id FROM song_resources r JOIN song_source_refs s ON s.id=r.song_source_ref_id WHERE s.source_content_id='" + song.source_content_id + "' AND r.resource_kind='audio' AND r.format_type='PQ'");
}
// 只构造已有持久确认端口需要的测试事实，不声称真实文件或上传。
model::ResourceTransferReceipt receipt(const model::ResourceTransferAttempt &attempt)
{
    model::ResourceTransferReceipt value;
    value.path = attempt.path;
    value.checksum = attempt.checksum;
    value.size_bytes = attempt.size_bytes;
    value.adapter = "filesystem";
    value.verification = model::ResourceVerification::Sha256Verified;
    value.verification_method = "local_sha256";
    return value;
}
} // namespace

int main()
{
    std::string stage = "startup";
    try {
        require(support::makeMySqlConfigFromEnv().database.starts_with("crawler_ownership_test_related_"), "refusing non-isolated related database");
        auto pool = support::makeCoverTestPool();
        mysql::MySqlLegacyResourceImport importer(pool);
        for (auto destination : {model::ResourceDestination::Local, model::ResourceDestination::Baidu, model::ResourceDestination::Gdrive}) get(importer.configureTarget(destination, 1, std::string(64, char('a' + static_cast<int>(destination)))));
        execute(pool, "UPDATE resource_destination_states SET dispatch_state='active',baseline_ready=1,baseline_manifest=REPEAT('f',64)");
        model::ResourceTransferTargetProfile profile;
        profile.destination = model::ResourceDestination::Local;
        profile.fingerprint = std::string(64, 'a');
        profile.adapter = "filesystem";
        mysql::MySqlResourceTransferJobs jobs(pool);
        for (int scenario = 0; scenario < 4; ++scenario) {
            stage = "fixture " + std::to_string(scenario);
            const auto id = audio(pool, scenario);
            model::ResourceTransferRequest request;
            request.resources = {{model::ResourceOwner::Song, id}};
            request.destinations = {model::ResourceDestination::Local};
            request.process_related_resources = scenario != 0;
            stage = "enqueue " + std::to_string(scenario);
            const auto accepted = get(jobs.enqueue(request, {profile}));
            stage = "claim " + std::to_string(scenario);
            auto claimed = get(jobs.claim(profile.destination, profile.fingerprint, profile.adapter));
            require(claimed.has_value() && claimed->task_id == accepted.tasks.front().task_id, "related parent was not independently claimed");
            const auto job = *claimed;
            require(job.process_related_resources == (scenario != 0), "related policy did not survive persistence");
            if (scenario == 3) {
                stage = "restricted fail";
                model::ResourceTransferJobFailure failure {"440016", "resource", false, false, true};
                failure.related_mask = 3;
                require(get(jobs.fail(job, failure)), "permanent audio result failed");
                const auto parent = get(jobs.find(job.operation_id));
                require(parent.child_operation_ids.size() == 1, "allowed related resources disappeared with restricted audio");
                require(scalar(pool, "SELECT status FROM song_resources WHERE id=" + std::to_string(id)) == 4, "related discovery cleared audio restriction");
                require(scalar(pool, "SELECT COUNT(*) FROM song_resources WHERE song_source_ref_id=(SELECT song_source_ref_id FROM (SELECT song_source_ref_id FROM song_resources WHERE id=" + std::to_string(id) + ") original) AND resource_kind='audio' AND status<>4") == 0, "digital album did not restrict sibling audio formats");
                require(scalar(pool, "SELECT COUNT(*) FROM song_resources WHERE song_source_ref_id=(SELECT song_source_ref_id FROM song_resources WHERE id=" + std::to_string(id) + ") AND resource_kind<>'audio' AND status=4") == 0, "digital album restricted allowed related files");
            } else {
                stage = "begin " + std::to_string(scenario);
                model::ResourceTransferAttempt attempt {"fixture/audio-" + std::to_string(scenario), std::string(64, 'a'), 3, 15};
                require(get(jobs.beginAttempt(job, attempt)), "related parent intent failed");
                if (scenario == 2) {
                    stage = "uncertain fail";
                    require(get(jobs.fail(job, {"reply_unknown", "network", true, false})), "uncertain parent fixture failed");
                    request.action = model::ResourceTransferAction::Repair;
                    stage = "repair enqueue";
                    const auto repair = get(jobs.enqueue(request, {profile}));
                    const auto next = get(jobs.claim(profile.destination, profile.fingerprint, profile.adapter));
                    require(next.has_value() && next->task_id == repair.tasks.front().task_id && next->attempt_path == attempt.path, "repair lost exact audio intent");
                    stage = "repair complete";
                    require(get(jobs.complete(*next, receipt(attempt))), "repair did not use persisted discovery");
                    const auto parent = get(jobs.find(next->operation_id));
                    require(parent.child_operation_ids.size() == 1, "repair omitted related child");
                } else {
                    stage = "complete " + std::to_string(scenario);
                    require(get(jobs.complete(job, receipt(attempt))), "audio confirmation failed");
                    require(!get(jobs.complete(job, receipt(attempt))), "late confirmation repeated related work");
                    const auto parent = get(jobs.find(job.operation_id));
                    require(parent.child_operation_ids.size() == (scenario ? 1 : 0), "related flag did not control child scope");
                    if (scenario) {
                        const auto child = get(jobs.find(parent.child_operation_ids.front()));
                        require(child.tasks.size() == 4 && child.parent_task_id == job.task_id, "related kinds did not become a controlled child");
                        require(get(jobs.cancel(job.operation_id, {})), "related parent cancel failed");
                        const auto cancelled = get(jobs.find(child.operation_id));
                        require(std::all_of(cancelled.tasks.begin(), cancelled.tasks.end(), [](const auto &task) { return task.state == "cancelled"; }), "parent cancellation left related children active");
                    }
                }
            }
            stage = "cleanup " + std::to_string(scenario);
            // 不运行内容worker；显式取消当前fixture所有已受理子任务，避免影响下一个认领。
            const auto rows = execute(pool, "SELECT operation_id FROM resource_transfer_operations ORDER BY operation_id");
            for (const auto row : rows.rows()) require(get(jobs.cancel(mysql::MySqlValueReader::stringAt(row, 0), {})), "fixture cleanup cancellation failed");
        }
        require(scalar(pool, "SELECT COUNT(*) FROM resource_transfer_related WHERE discovered_mask=15") >= 2, "discovery was not durable");
        std::cout << "PASS: frozen related scope, atomic audio children, repair, cancellation and permanent restrictions\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "related test failed at " << stage << ": " << error.what() << '\n';
        return 1;
    }
}
