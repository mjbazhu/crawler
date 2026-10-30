#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceProducerTasks.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMetadataBatchWriter.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceTransferJobs.h"
#include "migu/support/MetadataCoverMySqlTestSupport.h"

#include <algorithm>
#include <cstdint>
#include <iostream>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace c = orchestrator::modules::crawler;
namespace mysql = c::infrastructure::music::persistence::mysql;
namespace model = c::application::model;
namespace support = c::tests::migu::support;
using Destination = model::ResourceDestination;
namespace {
// 固定诊断不泄露连接、临时地址或对象正文。
void require(bool value, const char *message)
{
    if (!value) throw std::runtime_error(message);
}
// 仅操作显式隔离测试库的元信息。
boost::mysql::results execute(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, std::string sql)
{
    return support::runOnExecutor(pool->executor(), pool->coExecute(std::move(sql)), "producer fixture");
}
// 固定计数验证事务边界。
std::uint64_t scalar(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, std::string sql)
{
    const auto rows = execute(pool, std::move(sql));
    return mysql::MySqlValueReader::uint64At(rows.rows().front(), 0);
}
// 保留服务固定错误消息以定位失败阶段。
template <class T>
T get(c::application::result::ServiceResult<T> value)
{
    if (!value.isSuccess()) throw std::runtime_error(value.message());
    return *value.data();
}
// 登记一个没有正式本地文件的 MRC 来源。
std::uint64_t source(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, const std::string &name)
{
    model::SongSourceMetadata song;
    song.source = "migu";
    song.source_song_id = name;
    song.source_content_id = name + "-content";
    song.title = name;
    model::MetadataWriteBatch batch;
    batch.songs = {song};
    get(mysql::MySqlMetadataBatchWriter(pool).saveBatch(batch));
    execute(pool, "INSERT INTO song_resources(song_source_ref_id,resource_kind,format_type) SELECT id,'mrc','' FROM song_source_refs WHERE source_song_id='" + name + "' ON DUPLICATE KEY UPDATE id=song_resources.id");
    return scalar(pool, "SELECT r.id FROM song_resources r JOIN song_source_refs s ON s.id=r.song_source_ref_id WHERE s.source_song_id='" + name + "' AND r.resource_kind='mrc'");
}
// 目标预算和生成预算故意不同，验证不会用 MRC 上限替换产物保存预算。
std::vector<model::ResourceTransferTargetProfile> profiles(bool baidu_key = true)
{
    std::vector<model::ResourceTransferTargetProfile> result;
    for (const auto destination : {Destination::Baidu, Destination::Gdrive}) {
        model::ResourceTransferTargetProfile value;
        value.destination = destination;
        value.fingerprint = std::string(64, destination == Destination::Baidu ? 'b' : 'c');
        value.adapter = "rclone";
        value.produces_artifacts = true;
        value.producer_version = "fixture-v1";
        value.producer_key_available = destination != Destination::Baidu || baidu_key;
        value.producer_policy = {5000, 1, 1024 * 1024, 0};
        result.push_back(value);
    }
    return result;
}
// 新请求明确选择目标，生成计划在同一事务内产生。
model::ResourceTransferOperation start(mysql::MySqlResourceTransferJobs &jobs, std::uint64_t id, std::vector<Destination> destinations = {Destination::Baidu}, bool key = true)
{
    model::ResourceTransferRequest request;
    request.resources = {{model::ResourceOwner::Song, id}};
    request.destinations = std::move(destinations);
    return get(jobs.enqueue(request, profiles(key)));
}
// 每次只能取得当前独立 producer 认领。
model::ResourceProducerTask claim(mysql::MySqlResourceProducerTasks &jobs, Destination destination = Destination::Baidu)
{
    auto value = get(jobs.claim(destination, std::string(64, destination == Destination::Baidu ? 'b' : 'c'), "rclone", "fixture-v1", 1, true));
    require(value.has_value(), "producer not independently claimable");
    return *value;
}
// 测试摘要只验证持久化逻辑，不声称执行真实文件生成。
model::LyricArtifactBundle bundle(std::uint64_t resource)
{
    return {resource, std::string(64, 'f'), "fixture-v1", 1, {{"word_json", {}, 3, std::string(64, 'a')}, {"enhanced_lrc", {}, 4, std::string(64, 'b')}}};
}
// 将已验证假对象通过正式事务确认，父传输终态仍不能提前结束生成计划。
void completeParent(mysql::MySqlResourceTransferJobs &jobs)
{
    auto value = get(jobs.claim(Destination::Gdrive, std::string(64, 'c'), "rclone"));
    require(value.has_value(), "parent transfer missing");
    require(get(jobs.beginAttempt(*value, {"fixture/parent-mrc", std::string(64, 'f'), 9})), "parent attempt rejected");
    model::ResourceTransferReceipt receipt;
    receipt.path = "fixture/parent-mrc";
    receipt.checksum = std::string(64, 'f');
    receipt.size_bytes = 9;
    receipt.adapter = "rclone";
    receipt.verification = model::ResourceVerification::Sha256Verified;
    receipt.verification_method = "remote_sha256";
    receipt.remote_hash_algorithm = "sha256";
    receipt.remote_hash_value = receipt.checksum;
    require(get(jobs.complete(*value, receipt)), "parent completion rejected");
}
} // namespace

// 隔离库验证独立预算、原子子排队、输入等待、有限恢复及取消/版本围栏。
int main()
{
    try {
        require(support::makeMySqlConfigFromEnv().database.starts_with("crawler_ownership_test_producer_"), "refusing non-isolated producer database");
        auto pool = support::makeCoverTestPool();
        execute(pool, "UPDATE resource_destination_states SET dispatch_state='active',baseline_ready=1,baseline_manifest=REPEAT('f',64),target_fingerprint=CASE destination WHEN 'local' THEN REPEAT('a',64) WHEN 'baidu' THEN REPEAT('b',64) ELSE REPEAT('c',64) END");
        mysql::MySqlResourceTransferJobs transfers(pool);
        mysql::MySqlResourceProducerTasks producers(pool);
        const auto first_source = source(pool, "producer-atomic");
        const auto parent = start(transfers, first_source, {Destination::Baidu, Destination::Gdrive}, false);
        auto listed = get(producers.list(parent.operation_id));
        require(listed.size() == 2 && listed.front().state == "waiting_input" && listed.front().attempt_count == 0, "missing key did not persist waiting_input");
        completeParent(transfers);
        require(get(transfers.find(parent.operation_id)).state == "running", "parent completed before producer");
        const auto generated = claim(producers, Destination::Gdrive);
        require(generated.source_job.state == "succeeded", "producer still depended on parent running");
        execute(pool, "CREATE TRIGGER reject_producer_child BEFORE INSERT ON resource_transfer_operations FOR EACH ROW BEGIN IF JSON_UNQUOTE(JSON_EXTRACT(NEW.request_snapshot,'$.child_kind'))='artifact' THEN SIGNAL SQLSTATE '45000' SET MESSAGE_TEXT='injected child failure'; END IF; END");
        require(!producers.complete(generated, bundle(first_source), 9).isSuccess(), "injected child failure unexpectedly committed");
        require(scalar(pool, "SELECT COUNT(*) FROM song_resource_artifacts WHERE source_resource_id=" + std::to_string(first_source)) == 0, "child enqueue failure leaked artifact registration");
        execute(pool, "DROP TRIGGER reject_producer_child");
        auto changed_description = generated;
        changed_description.source_job.policy.max_bytes = 1;
        const auto child_id = get(producers.complete(changed_description, bundle(first_source), 9));
        require(get(producers.complete(generated, bundle(first_source), 9)) == child_id, "producer replay duplicated child operation");
        const auto child = get(transfers.find(child_id));
        require(child.tasks.size() == 2 && child.tasks.front().state == "pending" && child.tasks.front().policy.max_bytes == profiles().front().policy.max_bytes, "artifact tasks did not inherit transfer budget");
        require(scalar(pool, "SELECT COUNT(*) FROM song_resource_artifacts WHERE source_resource_id=" + std::to_string(first_source) + " AND COALESCE(local_path,'')='' AND status=0 AND size_bytes=0 AND checksum='' AND LENGTH(reference_sha256)=64") == 2, "producer fabricated local completion");
        get(transfers.cancel(parent.operation_id, {Destination::Baidu}));
        listed = get(producers.list(parent.operation_id));
        require(listed.front().state == "cancelled" && listed.back().state == "succeeded", "target cancellation affected sibling producer");
        require(get(transfers.find(child_id)).tasks.front().state == "pending", "target cancellation affected sibling child transfer");
        get(transfers.cancel(parent.operation_id, {Destination::Gdrive}));
        require(get(transfers.find(child_id)).state == "cancelled", "parent cancellation missed queued artifact operation");

        const auto retries_source = source(pool, "producer-retries");
        const auto retries_parent = start(transfers, retries_source);
        const auto attempt1 = claim(producers);
        require(get(producers.fail(attempt1, {"source_temporarily_unavailable", "network", false, true})), "temporary producer failure rejected");
        execute(pool, "UPDATE resource_producer_tasks SET available_at=0 WHERE producer_id=" + std::to_string(attempt1.producer_id));
        const auto attempt2 = claim(producers);
        get(producers.fail(attempt2, {"source_temporarily_unavailable", "network", false, true}));
        require(get(producers.list(retries_parent.operation_id)).front().state == "failed", "producer retries were unbounded");
        require(get(producers.retry(attempt1.producer_id)), "explicit producer retry failed");
        const auto recovery = claim(producers);
        require(get(producers.recover(Destination::Baidu, 1)) == 0, "small stale window stole unexpired producer lease");
        execute(pool, "UPDATE resource_producer_tasks SET claimed_at=1 WHERE producer_id=" + std::to_string(recovery.producer_id));
        require(get(producers.recover(Destination::Baidu, 1)) == 1 && get(producers.shouldStop(recovery)), "expired producer was not fenced");
        require(!producers.complete(recovery, bundle(retries_source), 9).isSuccess(), "recovered producer allowed late commit");
        get(transfers.cancel(retries_parent.operation_id, {}));

        const auto key_source = source(pool, "producer-key");
        const auto key_parent = start(transfers, key_source, {Destination::Baidu}, false);
        require(!get(producers.claim(Destination::Baidu, std::string(64, 'b'), "rclone", "fixture-v1", 1, false)), "missing key acquired work");
        listed = get(producers.list(key_parent.operation_id));
        require(listed.front().attempt_count == 0, "missing key consumed retry budget");
        execute(pool, "UPDATE resource_producer_tasks SET available_at=0 WHERE producer_id=" + std::to_string(listed.front().producer_id));
        const auto key_ready = claim(producers);
        execute(pool, "UPDATE song_resources SET source_revision=source_revision+1 WHERE id=" + std::to_string(key_source));
        require(get(producers.shouldStop(key_ready)) && !producers.complete(key_ready, bundle(key_source), 9).isSuccess(), "changed source allowed late artifacts");
        get(producers.fail(key_ready, {"source_changed", "source"}));
        get(transfers.cancel(key_parent.operation_id, {}));

        const auto cancelled_source = source(pool, "producer-cancelled");
        const auto cancelled_parent = start(transfers, cancelled_source);
        const auto cancelled = claim(producers);
        get(transfers.cancel(cancelled_parent.operation_id, {}));
        require(get(producers.shouldStop(cancelled)) && !producers.complete(cancelled, bundle(cancelled_source), 9).isSuccess(), "parent cancellation allowed late producer commit");
        get(producers.fail(cancelled, {"cancelled", "cancelled"}));
        require(get(producers.list(cancelled_parent.operation_id)).front().state == "cancelled", "running producer cancellation lost state");

        const auto changed_target_source = source(pool, "producer-target");
        const auto changed_target_parent = start(transfers, changed_target_source);
        const auto changed_target = claim(producers);
        execute(pool, "UPDATE resource_destination_states SET target_revision=target_revision+1 WHERE destination='baidu'");
        require(get(producers.shouldStop(changed_target)) && !producers.complete(changed_target, bundle(changed_target_source), 9).isSuccess(), "changed target allowed old producer commit");
        get(producers.fail(changed_target, {"target_changed", "target"}));
        get(transfers.cancel(changed_target_parent.operation_id, {}));

        const auto processor_source = source(pool, "producer-processor");
        const auto processor_parent = start(transfers, processor_source);
        require(!get(producers.claim(Destination::Baidu, std::string(64, 'b'), "rclone", "fixture-v2", 1, true)), "changed processor acquired old plan");
        listed = get(producers.list(processor_parent.operation_id));
        require(listed.front().state == "blocked" && listed.front().attempt_count == 0, "processor mismatch consumed attempt or lost block");
        require(get(producers.retry(listed.front().producer_id)), "same frozen plan could not be explicitly retried");
        const auto old_processor = claim(producers);
        require(old_processor.processor_version == "fixture-v1", "retry silently changed processor version");
        get(transfers.cancel(processor_parent.operation_id, {}));
        get(producers.fail(old_processor, {"cancelled", "cancelled"}));

        const auto conflict_source = source(pool, "producer-content-conflict");
        const auto conflict_parent = start(transfers, conflict_source, {Destination::Baidu, Destination::Gdrive});
        const auto consistent = claim(producers);
        const auto inconsistent = claim(producers, Destination::Gdrive);
        get(producers.complete(consistent, bundle(conflict_source), 9));
        auto different_bundle = bundle(conflict_source);
        different_bundle.source_checksum = std::string(64, 'e');
        require(!producers.complete(inconsistent, different_bundle, 9).isSuccess(), "conflicting cloud source overwrote producer artifacts");
        require(scalar(pool, "SELECT COUNT(*) FROM song_resource_artifacts WHERE source_resource_id=" + std::to_string(conflict_source) + " AND source_checksum=REPEAT('f',64)") == 2, "conflicting producer changed frozen source checksum");
        get(producers.fail(inconsistent, {"source_checksum_conflict", "source"}));
        get(transfers.cancel(conflict_parent.operation_id, {}));

        const auto historical_source = source(pool, "producer-history");
        const auto historical_id = std::to_string(historical_source);
        execute(pool, "UPDATE song_resources SET status=2,local_skip_generation=content_generation,local_skip_target_revision=(SELECT target_revision FROM resource_destination_states WHERE destination='local'),local_skip_baseline='history',local_skip_reason='operator_attested' WHERE id=" + historical_id);
        execute(pool, "INSERT INTO resource_cloud_receipts(owner_type,resource_id,song_resource_id,cloud_provider,transfer_policy,policy_content_generation,policy_target_revision,baseline_id,skip_reason) SELECT 'song',r.id,r.id,d.destination,'skip',r.content_generation,d.target_revision,'history','operator_attested' FROM song_resources r JOIN resource_destination_states d ON d.destination IN ('baidu','gdrive') WHERE r.id=" + historical_id);
        auto history_profiles = profiles(false);
        auto local_profile = history_profiles.front();
        local_profile.destination = Destination::Local;
        local_profile.fingerprint = std::string(64, 'a');
        local_profile.adapter = "filesystem";
        history_profiles.push_back(local_profile);
        model::ResourceTransferRequest history_request;
        history_request.resources = {{model::ResourceOwner::Song, historical_source}};
        history_request.destinations = {Destination::Local, Destination::Baidu, Destination::Gdrive};
        const auto history_skip = get(transfers.enqueue(history_request, history_profiles));
        require(get(producers.list(history_skip.operation_id)).empty() && std::all_of(history_skip.tasks.begin(), history_skip.tasks.end(), [](const auto &task) { return task.result_code == "skip_existing"; }), "normal history skip created producer work");
        history_request.action = model::ResourceTransferAction::ProduceArtifacts;
        history_request.idempotency_key = "explicit-history-producer";
        const auto history_parent = get(transfers.enqueue(history_request, history_profiles));
        require(std::all_of(history_parent.tasks.begin(), history_parent.tasks.end(), [](const auto &task) { return task.state == "skipped" && task.result_code == "producer_only"; }), "explicit producer registered an MRC transfer");
        listed = get(producers.list(history_parent.operation_id));
        require(listed.size() == 3 && std::count_if(listed.begin(), listed.end(), [](const auto &task) { return task.state == "waiting_input"; }) == 2, "explicit history producer lost missing key state");
        require(get(transfers.enqueue(history_request, history_profiles)).operation_id == history_parent.operation_id, "explicit history replay duplicated operation");
        const auto history_cloud = claim(producers);
        get(producers.complete(history_cloud, bundle(historical_source), 9));
        const auto history_local = get(producers.claim(Destination::Local, std::string(64, 'a'), "filesystem", "fixture-v1", 1, true));
        require(history_local.has_value(), "local history producer remained blocked by MRC skip");
        get(producers.complete(*history_local, bundle(historical_source), 9));
        require(scalar(pool, "SELECT COUNT(*) FROM resource_transfer_slots WHERE owner_type='song' AND resource_id=" + historical_id) == 0, "producer-only action acquired an MRC transfer slot");
        require(scalar(pool, "SELECT COUNT(*) FROM song_resources WHERE id=" + historical_id + " AND status=2 AND local_skip_generation=content_generation AND local_skip_baseline='history' AND local_skip_reason='operator_attested' AND COALESCE(local_path,'')=''") == 1, "producer-only action changed local MRC history");
        require(scalar(pool, "SELECT COUNT(*) FROM resource_cloud_receipts WHERE owner_type='song' AND resource_id=" + historical_id + " AND transfer_policy='skip' AND baseline_id='history' AND skip_reason='operator_attested' AND task_state='pending' AND attempt_count=0 AND cloud_path IS NULL") == 2, "producer-only action changed cloud MRC history");
        get(transfers.cancel(history_parent.operation_id, {}));
        history_request.idempotency_key.clear();
        const auto invalid_source = source(pool, "producer-not-mrc");
        execute(pool, "UPDATE song_resources SET resource_kind='lyric' WHERE id=" + std::to_string(invalid_source));
        history_request.resources = {{model::ResourceOwner::Song, invalid_source}};
        require(transfers.enqueue(history_request, history_profiles).error() == c::application::result::ServiceErrorType::InvalidParam, "non-MRC explicit producer was accepted");
        execute(pool, "UPDATE song_resources SET status=4 WHERE id=" + historical_id);
        history_request.resources = {{model::ResourceOwner::Song, historical_source}};
        const auto restricted_parent = get(transfers.enqueue(history_request, history_profiles));
        require(get(producers.list(restricted_parent.operation_id)).empty() && restricted_parent.tasks.front().result_code == "restricted", "explicit generation bypassed source restriction");

        const auto new_restricted_source = source(pool, "producer-discovered-restriction");
        history_request.resources = {{model::ResourceOwner::Song, new_restricted_source}};
        history_request.destinations = {Destination::Baidu};
        const auto new_restricted_parent = get(transfers.enqueue(history_request, history_profiles));
        const auto new_restricted = claim(producers);
        get(producers.fail(new_restricted, {"210001", "resource", false, false, true}));
        require(scalar(pool, "SELECT status FROM song_resources WHERE id=" + std::to_string(new_restricted_source)) == 4 && !get(producers.retry(new_restricted.producer_id)), "discovered source restriction was not durable");
        require(get(producers.list(new_restricted_parent.operation_id)).front().state == "failed", "restricted producer lost diagnostic result");

        const auto deleted_source = source(pool, "producer-deleted");
        const auto deleted_parent = start(transfers, deleted_source);
        const auto deleted = claim(producers);
        execute(pool, "DELETE FROM song_resources WHERE id=" + std::to_string(deleted_source));
        require(get(producers.shouldStop(deleted)) && !producers.complete(deleted, bundle(deleted_source), 9).isSuccess(), "deleted parent resource accepted artifacts");
        get(producers.fail(deleted, {"resource_not_found", "source"}));
        require(!get(producers.list(deleted_parent.operation_id)).empty(), "deleted resource erased producer failure history");
        std::cout << "PASS: durable producer, atomic child enqueue, independent budget, bounded recovery and cancellation fences\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "producer test failed: " << error.what() << '\n';
        return 1;
    }
}
