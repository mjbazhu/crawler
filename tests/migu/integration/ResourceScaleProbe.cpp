#include "migu/support/ResourceScaleSupport.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlMetadataBatchWriter.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceQueueCapacity.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceTransferJobs.h"
#include "crawler/infrastructure/music/persistence/mysql/MySqlResourceTransferQueries.h"
#include "crawler/infrastructure/storage/filesystem/FileStorage.h"

#include <atomic>
#include <barrier>
#include <future>
#include <mutex>
#include <map>
#include <set>
#include <thread>

namespace c = orchestrator::modules::crawler;
namespace scale = c::tests::scale;
namespace mysql = scale::mysql;
namespace model = scale::model;
namespace files = c::infrastructure::storage::filesystem;
using Json = scale::Json;
using Clock = scale::Clock;
using D = model::ResourceDestination;

namespace {
// 负载仅允许预先固定的首轮工程范围，拒绝把工具用作任意批量写入口。
void validatePlan(const Json &plan)
{
    scale::require(plan.at("format") == "crawler.resource-scale-plan.v1" && plan.at("scope").at("scope_type") == "controlled_fixture", "controlled_scale_plan_required");
    const auto &work = plan.at("workload");
    scale::require(work.at("samples_per_worker") == 30 && work.at("resources_per_request") == 10 && work.at("concurrency") == Json({1, 2, 4}) && work.at("staging_bytes_per_target") == 1048576, "fixed_first_round_workload_required");
    scale::require(std::regex_match(plan.at("scope").at("run_id").get<std::string>(), std::regex("[a-z0-9]{8,24}")), "scale_run_identity_invalid");
    scale::require(plan.at("scope").at("schema_version") == 2026102912 && plan.at("scope").at("production_authorized") == false, "scale_schema_and_nonproduction_scope_required");
}

// 目标身份从已封存计划取得；只用于测试库受理，没有云端装配或网络I/O。
std::vector<model::ResourceTransferTargetProfile> profiles(const Json &plan)
{
    std::vector<model::ResourceTransferTargetProfile> values;
    for (const auto destination : {D::Local, D::Baidu, D::Gdrive}) {
        model::ResourceTransferTargetProfile value;
        value.destination = destination;
        value.adapter = destination == D::Local ? "filesystem" : "rclone";
        value.fingerprint = plan.at("scope").at("synthetic_target_fingerprints").at(std::string(model::resourceDestinationName(destination)));
        value.policy.max_bytes = 1048576;
        values.push_back(std::move(value));
    }
    return values;
}

// 只构造清单内的新Singer ID，强制复核写准入且明确选择目标。
model::ResourceTransferRequest request(const std::vector<std::uint64_t> &ids, std::size_t begin, std::size_t count, std::string key, std::vector<D> destinations = {D::Local, D::Baidu, D::Gdrive})
{
    scale::require(begin + count <= ids.size(), "scale_fixture_range_invalid");
    model::ResourceTransferRequest value;
    for (auto index = begin; index < begin + count; ++index) value.resources.push_back({model::ResourceOwner::Singer, ids[index]});
    value.destinations = std::move(destinations);
    value.idempotency_key = std::move(key);
    value.require_write_admission = true;
    return value;
}

// 快照记录所有受理副作用；容量拒绝必须保持整组计数原样。
Json taskCounts(const std::shared_ptr<mysql::MySqlConnectionPool> &pool)
{
    Json value = Json::object();
    for (const auto *table : {"resource_transfer_operations", "resource_transfer_tasks", "resource_transfer_slots", "resource_cloud_receipts", "resource_producer_tasks", "resource_legacy_batches", "resource_legacy_operations", "resource_legacy_profiles", "resource_transfer_related"}) value[table] = scale::scalar(pool, "SELECT COUNT(*) FROM `" + std::string(table) + "`");
    return value;
}

// 真实COUNT只在测量区间前后执行，避免把背景全表扫描混入每条延迟。
void verifyBackground(const Json &counts, const Json &plan)
{
    std::uint64_t rows = 0;
    for (const auto &[name, count] : counts.items()) rows += count.get<std::uint64_t>();
    scale::require(rows >= plan.at("scope").at("minimum_background_rows").get<std::uint64_t>() && counts.at("song_resources") >= plan.at("scope").at("minimum_song_resources"), "scale_background_below_sealed_size");
}

// prepare只追加专属fixture，绝不初始化schema、删除记录或改写原业务行。
void prepare(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, const Json &plan, const Json &observed)
{
    const auto empty = scale::requireEmptyNewTables(pool);
    const auto counts = scale::backgroundCounts(pool);
    verifyBackground(counts, plan);
    // 固定百万行歌曲表的首/中/尾已有ID，只查数据库，不读取历史文件。
    const auto song_count = counts.at("song_resources").get<std::uint64_t>();
    const std::vector<std::uint64_t> song_ids {
        scale::scalar(pool, "SELECT id FROM song_resources ORDER BY id LIMIT 1"),
        scale::scalar(pool, "SELECT id FROM song_resources ORDER BY id LIMIT 1 OFFSET " + std::to_string(song_count / 2)),
        scale::scalar(pool, "SELECT id FROM song_resources ORDER BY id DESC LIMIT 1")
    };
    const auto run = plan.at("scope").at("run_id").get<std::string>();
    const auto prefix = "scale-" + run + "-";
    scale::require(scale::scalar(pool, "SELECT COUNT(*) FROM singer_source_refs WHERE source='migu' AND source_singer_id LIKE '" + prefix + "%' ") == 0 && scale::scalar(pool, "SELECT COUNT(*) FROM singers WHERE normalized_name LIKE '" + prefix + "%' ") == 0, "scale_fixture_origin_already_exists");
    const auto maximum = scale::scalar(pool, "SELECT COALESCE(MAX(id),0) FROM singer_resources");
    const std::size_t fixtures = 2108;
    mysql::MySqlMetadataBatchWriter writer(pool);
    for (std::size_t begin = 0; begin < fixtures; begin += 100) {
        model::MetadataWriteBatch batch;
        for (auto index = begin; index < std::min(begin + 100, fixtures); ++index) {
            model::SingerSourceMetadata singer;
            singer.source = "migu";
            singer.source_singer_id = prefix + std::to_string(index);
            singer.name = singer.source_singer_id;
            singer.avatar_url = "https://example.test/controlled-scale-fixture.png";
            batch.singers.push_back(std::move(singer));
        }
        scale::get(writer.saveBatch(std::move(batch)));
    }
    const auto rows = scale::execute(pool, "SELECT r.id FROM singer_resources r JOIN singer_source_refs s ON s.id=r.singer_source_ref_id WHERE s.source='migu' AND s.source_singer_id LIKE '" + prefix + "%' ORDER BY r.id");
    scale::require(rows.rows().size() == fixtures, "scale_fixture_count_changed");
    Json ids = Json::array();
    for (const auto row : rows.rows()) {
        const auto id = mysql::MySqlValueReader::uint64At(row, 0);
        scale::require(id > maximum, "scale_fixture_overlaps_original_resource");
        ids.push_back(id);
    }
    for (const auto &profile : profiles(plan)) {
        const auto name = std::string(model::resourceDestinationName(profile.destination));
        scale::execute(pool, "UPDATE resource_destination_states SET dispatch_state='active',baseline_ready=1,baseline_manifest='" + scale::sha256("controlled_fixture:" + run) + "',target_fingerprint='" + profile.fingerprint + "',target_revision=target_revision+1 WHERE destination='" + name + "'");
    }
    mysql::MySqlResourceQueueCapacity capacity(pool);
    const auto states = scale::get(capacity.status());
    for (const auto &state : states) scale::get(capacity.configure(state.destination, state.version, 10000, 100));
    scale::require(taskCounts(pool).at("resource_transfer_tasks") == 0, "scale_prepare_created_tasks");
    scale::output({{"event", "prepared"}, {"format", "crawler.resource-scale-fixtures.v1"}, {"scope", plan.at("scope")}, {"identity", observed}, {"background_counts", counts}, {"new_table_counts_before", empty}, {"maximum_original_singer_resource_id", maximum}, {"resource_ids", ids}, {"original_song_ids_first_middle_last", song_ids}, {"original_song_selection", "existing_primary_key_order_first_middle_rank_last"}, {"fixture_resources", fixtures}, {"source_requests", 0}, {"cloud_requests", 0}});
}

// 临时文件只由真实FileStorage会话产生，扫描与创建/abort同步以保留可靠文件size。
class Staging final {
public:
    // 根必须是父进程为本轮单独创建的空目录。
    explicit Staging(std::filesystem::path root) : root_(std::move(root))
    {
        scale::require(std::filesystem::is_directory(root_) && std::filesystem::is_empty(root_) && !std::filesystem::is_symlink(root_), "new_empty_scale_staging_required");
    }
    // 保持三端各1MiB未提交文件；没有正式local发布和数据库回执。
    std::vector<std::unique_ptr<c::application::ports::SongResourceBlobWriteSession>> hold(std::size_t index)
    {
        std::lock_guard guard(mutex_);
        std::vector<std::unique_ptr<c::application::ports::SongResourceBlobWriteSession>> sessions;
        const std::string chunk(65536, static_cast<char>('a' + index % 26));
        for (const auto *target : {"local", "baidu", "gdrive"}) {
            files::FileStorage store(root_ / target);
            model::SongResourceBlobStreamSaveRequest request;
            request.relative_path_hint = "generated-" + std::to_string(index) + ".bin";
            auto session = store.beginSongResourceBlobWrite(request);
            scale::require(session != nullptr, "scale_staging_session_missing");
            std::string error;
            for (int part = 0; part < 16; ++part) scale::require(session->write(chunk, error), "scale_staging_write_failed");
            sessions.push_back(std::move(session));
        }
        sampleLocked();
        return sessions;
    }
    // 仅清理本轮持有的未提交会话，不遍历或删除旧根。
    void release(std::vector<std::unique_ptr<c::application::ports::SongResourceBlobWriteSession>> &sessions)
    {
        std::lock_guard guard(mutex_);
        sampleLocked();
        for (auto &session : sessions) session->abort();
        sessions.clear();
        sampleLocked();
    }
    // 文件size边界观测与5ms周期观测共用同一口径。
    void sample()
    {
        std::lock_guard guard(mutex_);
        sampleLocked();
    }
    // 只返回已观测到的磁盘文件峰值，没有按并发乘大小的估算。
    Json result()
    {
        std::lock_guard guard(mutex_);
        sampleLocked();
        return {{"peak_observed_bytes", peak_}, {"peak_directory_metadata_bytes", metadata_peak_}, {"peak_file_count", file_peak_}, {"final_bytes", scale::directoryBytes(root_)}, {"scans", scans_}, {"method", "actual_open_ifstream_ate_EOF"}, {"scope", "generated_FileStorage_uncommitted_sessions_only"}};
    }

private:
    // 持锁扫描专属目录；不含源文件、输出日志或历史local文件。
    void sampleLocked()
    {
        const auto observed = scale::directoryObservation(root_);
        peak_ = std::max(peak_, observed.eof_bytes);
        metadata_peak_ = std::max(metadata_peak_, observed.metadata_bytes);
        file_peak_ = std::max(file_peak_, observed.files);
        ++scans_;
    }
    std::filesystem::path root_;      // 本轮专属临时根
    std::mutex mutex_;                // 同步创建、abort与实际文件扫描
    std::uint64_t peak_ = 0;          // 实测文件size峰值
    std::uint64_t metadata_peak_ = 0; // 目录缓存size峰值，仅用于核对Windows旧值
    std::uint64_t file_peak_ = 0;     // 同时存在的实际文件峰值
    std::uint64_t scans_ = 0;         // 实际完成的扫描次数
};

// 精确校验新ID和三端状态，拒绝prepare后被其他writer改动的fixture。
std::vector<std::uint64_t> verifyFixtures(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, const Json &plan, const Json &prepared)
{
    scale::require(prepared.at("scope") == plan.at("scope") && prepared.at("fixture_resources") == 2108 && prepared.at("resource_ids").size() == 2108, "sealed_scale_fixtures_required");
    const auto ids = prepared.at("resource_ids").get<std::vector<std::uint64_t>>();
    scale::require(std::is_sorted(ids.begin(), ids.end()) && std::adjacent_find(ids.begin(), ids.end()) == ids.end(), "scale_fixture_ids_not_unique");
    const auto prefix = "scale-" + plan.at("scope").at("run_id").get<std::string>() + "-";
    const auto rows = scale::execute(pool, "SELECT r.id,r.status,COALESCE(r.local_path,'') FROM singer_resources r JOIN singer_source_refs s ON s.id=r.singer_source_ref_id WHERE s.source='migu' AND s.source_singer_id LIKE '" + prefix + "%' ORDER BY r.id");
    scale::require(rows.rows().size() == ids.size(), "scale_fixture_origin_changed");
    std::size_t index = 0;
    for (const auto row : rows.rows()) {
        scale::require(mysql::MySqlValueReader::uint64At(row, 0) == ids[index++] && mysql::MySqlValueReader::uint64At(row, 1) == 0 && mysql::MySqlValueReader::stringAt(row, 2).empty(), "scale_fixture_state_changed");
    }
    for (const auto &profile : profiles(plan)) {
        const auto name = std::string(model::resourceDestinationName(profile.destination));
        scale::require(scale::scalar(pool, "SELECT COUNT(*) FROM resource_destination_states WHERE destination='" + name + "' AND dispatch_state='active' AND baseline_ready=1 AND target_fingerprint='" + profile.fingerprint + "' AND max_queued_transfers=10000") == 1, "scale_target_or_limit_changed");
    }
    scale::require(taskCounts(pool).at("resource_transfer_operations") == 0, "scale_workload_already_started");
    const auto current = scale::backgroundCounts(pool);
    for (const auto &[name, original] : prepared.at("background_counts").items()) {
        if (name == "singers" || name == "singer_source_refs" || name == "singer_resources")
            scale::require(current.at(name).get<std::uint64_t>() == original.get<std::uint64_t>() + ids.size(), "scale_background_fixture_count_changed");
        else if (name != "governance_events")
            scale::require(current.at(name) == original, "scale_original_background_count_changed");
    }
    return ids;
}

// 每个真实受理样本紧随正式查询和相同热点锁获取，独立记录各自延迟。
Json sample(mysql::MySqlResourceTransferJobs &jobs, mysql::MySqlResourceTransferQueries &queries, const std::shared_ptr<mysql::MySqlConnectionPool> &pool, const Json &plan, const std::vector<std::uint64_t> &ids, const std::vector<std::uint64_t> &song_ids, std::size_t offset, int concurrency, int worker, int index, Staging &staging)
{
    auto sessions = staging.hold(offset);
    auto input = request(ids, offset, 10, "scale-" + plan.at("scope").at("run_id").get<std::string>() + "-" + std::to_string(offset));
    const auto begin = Clock::now();
    const auto operation = scale::get(jobs.enqueue(input, profiles(plan)));
    const auto accepted = Clock::now();
    scale::require(operation.tasks.size() == 30 && !operation.replayed && std::all_of(operation.tasks.begin(), operation.tasks.end(), [](const auto &task) { return task.state == "pending"; }), "scale_enqueue_not_real_pending_work");
    const auto found = scale::get(jobs.find(operation.operation_id));
    const auto found_at = Clock::now();
    scale::require(found.tasks.size() == operation.tasks.size(), "scale_find_result_changed");
    model::ResourceStatusQuery query;
    query.resources = input.resources;
    query.destinations = input.destinations;
    const auto status = scale::get(queries.status(std::move(query)));
    const auto queried = Clock::now();
    scale::require(status.items.size() == 10 && std::all_of(status.items.begin(), status.items.end(), [](const auto &item) { return item.found; }), "scale_status_missing_fixture");
    model::ResourceStatusQuery original_query;
    for (const auto id : song_ids) original_query.resources.push_back({model::ResourceOwner::Song, id});
    original_query.destinations = input.destinations;
    const auto original_begin = Clock::now();
    const auto original_status = scale::get(queries.status(std::move(original_query)));
    const auto original_queried = Clock::now();
    scale::require(original_status.items.size() == 3 && std::all_of(original_status.items.begin(), original_status.items.end(), [](const auto &item) { return item.found; }), "scale_status_missing_original_song");
    const auto lock_ms = c::tests::migu::support::runOnExecutor(pool->executor(), scale::lockAcquisition(pool), "scale hotspot", std::chrono::seconds(120));
    staging.release(sessions);
    return {{"event", "sample"}, {"concurrency", concurrency}, {"worker", worker}, {"sample", index}, {"fixture_offset", offset}, {"operation_id", operation.operation_id}, {"accepted_tasks", 30}, {"enqueue_ms", std::chrono::duration<double, std::milli>(accepted - begin).count()}, {"find_ms", std::chrono::duration<double, std::milli>(found_at - accepted).count()}, {"status_ms", std::chrono::duration<double, std::milli>(queried - found_at).count()}, {"old_song_status_ms", std::chrono::duration<double, std::milli>(original_queried - original_begin).count()}, {"lock_acquisition_upper_bound_ms", lock_ms}, {"passed", true}};
}

// 边界场景同样只走正式仓储：一个剩余名额、多端整笔回滚、满队幂等重放。
Json boundary(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, const Json &plan, const std::vector<std::uint64_t> &ids)
{
    mysql::MySqlResourceTransferJobs jobs(pool);
    mysql::MySqlResourceQueueCapacity limits(pool);
    const auto states = scale::get(limits.status());
    const auto local = std::find_if(states.begin(), states.end(), [](const auto &state) { return state.destination == D::Local; });
    scale::require(local != states.end() && local->transfers == 2100, "scale_real_backlog_missing");
    scale::get(limits.configure(D::Local, local->version, 2101, 100));
    const auto before = taskCounts(pool);
    std::barrier start(4);
    std::vector<std::future<c::application::result::ServiceResult<model::ResourceTransferOperation>>> pending;
    const auto key = "scale-" + plan.at("scope").at("run_id").get<std::string>();
    for (int index = 0; index < 4; ++index) pending.push_back(std::async(std::launch::async, [&, index] { start.arrive_and_wait(); return jobs.enqueue(request(ids, 2100 + index, 1, key + "-race-" + std::to_string(index), {D::Local}), profiles(plan)); }));
    int accepted = 0, rejected = 0, winner = -1;
    std::string accepted_id;
    for (int index = 0; index < 4; ++index) {
        auto result = pending[index].get();
        if (result.isSuccess()) {
            ++accepted;
            winner = index;
            accepted_id = result.data()->operation_id;
        } else {
            scale::require(result.message() == "resource_queue_full", "scale_race_unexpected_failure");
            ++rejected;
        }
    }
    scale::require(accepted == 1 && rejected == 3, "scale_capacity_race_crossed_limit");
    const auto after = taskCounts(pool);
    for (const auto &[name, count] : before.items()) {
        const auto delta = name == "resource_transfer_operations" || name == "resource_transfer_tasks" || name == "resource_transfer_slots" ? 1 : 0;
        scale::require(after.at(name).get<std::uint64_t>() == count.get<std::uint64_t>() + delta, "scale_capacity_race_left_partial_work");
    }
    const auto replay = scale::get(jobs.enqueue(request(ids, 2100 + winner, 1, key + "-race-" + std::to_string(winner), {D::Local}), profiles(plan)));
    scale::require(replay.replayed && replay.operation_id == accepted_id && taskCounts(pool) == after, "scale_full_queue_replay_changed_work");
    const auto refused = jobs.enqueue(request(ids, 2104, 2, key + "-atomic"), profiles(plan));
    scale::require(!refused.isSuccess() && refused.message() == "resource_queue_full" && taskCounts(pool) == after, "scale_multi_target_rejection_not_atomic");
    const auto final = scale::get(limits.status());
    Json usage = Json::object();
    for (const auto &state : final) usage[std::string(model::resourceDestinationName(state.destination))] = {{"transfers", state.transfers}, {"limit", state.transfer_limit}, {"exact", state.transfers_exact}};
    return {{"event", "capacity_boundary"}, {"accepted", accepted}, {"expected_rejections", rejected + 1}, {"concurrent_contenders", 4}, {"remaining_capacity_before", 1}, {"idempotent_replay_preserved", true}, {"multi_target_atomic_rollback", true}, {"before", before}, {"after", after}, {"final_usage", usage}};
}

// 只验证s1遗留的完整固定积压；所有操作、任务、槽和回执均须属于原新ID。
std::vector<std::uint64_t> supplementGuard(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, const Json &plan, const Json &prepared)
{
    scale::require(prepared.at("scope") == plan.at("scope") && prepared.at("resource_ids").size() == 2108, "supplement_original_fixture_scope_changed");
    const auto ids = prepared.at("resource_ids").get<std::vector<std::uint64_t>>();
    scale::require(std::is_sorted(ids.begin(), ids.end()) && std::adjacent_find(ids.begin(), ids.end()) == ids.end(), "supplement_fixture_ids_invalid");
    const std::map<std::string, std::uint64_t> expected_counts {{"resource_transfer_operations", 210}, {"resource_transfer_tasks", 6300}, {"resource_transfer_slots", 6300}, {"resource_cloud_receipts", 4200}};
    for (const auto &[table, ignored] : prepared.at("new_table_counts_before").items()) {
        scale::require(std::regex_match(table, std::regex("resource_[a-z_]+")), "supplement_table_name_invalid");
        const auto expected = expected_counts.contains(table) ? expected_counts.at(table) : 0;
        scale::require(scale::scalar(pool, "SELECT COUNT(*) FROM `" + table + "`") == expected, "supplement_foreign_work_or_backlog_count_changed");
    }
    const auto prefix = "scale-" + plan.at("scope").at("run_id").get<std::string>() + "-";
    const auto resources = scale::execute(pool, "SELECT r.id,r.status,COALESCE(r.local_path,'') FROM singer_resources r JOIN singer_source_refs s ON s.id=r.singer_source_ref_id WHERE s.source='migu' AND s.source_singer_id LIKE '" + prefix + "%' ORDER BY r.id");
    scale::require(resources.rows().size() == ids.size(), "supplement_fixture_origins_changed");
    std::size_t ordinal = 0;
    for (const auto row : resources.rows()) scale::require(mysql::MySqlValueReader::uint64At(row, 0) == ids[ordinal++] && mysql::MySqlValueReader::uint64At(row, 1) == 0 && mysql::MySqlValueReader::stringAt(row, 2).empty(), "supplement_fixture_state_changed");
    std::map<std::string, std::set<std::uint64_t>> expected_requests, operations;
    for (std::size_t offset = 0; offset < 2100; offset += 10) expected_requests[prefix + std::to_string(offset)] = std::set<std::uint64_t>(ids.begin() + offset, ids.begin() + offset + 10);
    const auto parents = scale::execute(pool, "SELECT operation_id,CAST(idempotency_key AS CHAR),logical_resource_count,task_count,state,action,request_snapshot FROM resource_transfer_operations");
    for (const auto row : parents.rows()) {
        const auto key = mysql::MySqlValueReader::stringAt(row, 1);
        scale::require(expected_requests.contains(key) && mysql::MySqlValueReader::uint64At(row, 2) == 10 && mysql::MySqlValueReader::uint64At(row, 3) == 30 && mysql::MySqlValueReader::stringAt(row, 4) == "running" && mysql::MySqlValueReader::stringAt(row, 5) == "process", "supplement_foreign_operation");
        const auto snapshot = Json::parse(mysql::MySqlValueReader::stringAt(row, 6));
        scale::require(snapshot.at("resources").size() == 10 && snapshot.at("destinations") == Json({"local", "baidu", "gdrive"}), "supplement_original_request_changed");
        std::set<std::uint64_t> members;
        for (const auto &member : snapshot.at("resources")) {
            scale::require(member.at("owner_type") == "singer", "supplement_foreign_request_owner");
            members.insert(member.at("resource_id").get<std::uint64_t>());
        }
        scale::require(members == expected_requests.at(key), "supplement_original_request_ids_changed");
        operations.emplace(mysql::MySqlValueReader::stringAt(row, 0), std::move(members));
        expected_requests.erase(key);
    }
    scale::require(expected_requests.empty() && operations.size() == 210, "supplement_original_operations_incomplete");
    const auto tasks = scale::execute(pool, "SELECT t.operation_id,t.owner_type,t.resource_id,t.destination,t.state,t.claim_generation,t.cancel_requested,COALESCE(t.attempt_path,''),s.task_id=t.task_id AND s.owner_type=t.owner_type AND s.resource_id=t.resource_id AND s.destination=t.destination FROM resource_transfer_tasks t LEFT JOIN resource_transfer_slots s ON s.task_id=t.task_id");
    std::map<std::string, std::uint64_t> per_target;
    for (const auto row : tasks.rows()) {
        const auto operation = mysql::MySqlValueReader::stringAt(row, 0), target = mysql::MySqlValueReader::stringAt(row, 3);
        scale::require(operations.contains(operation) && operations.at(operation).contains(mysql::MySqlValueReader::uint64At(row, 2)) && mysql::MySqlValueReader::stringAt(row, 1) == "singer", "supplement_foreign_task_resource");
        scale::require((target == "local" || target == "baidu" || target == "gdrive") && mysql::MySqlValueReader::stringAt(row, 4) == "pending" && mysql::MySqlValueReader::uint64At(row, 5) == 0 && mysql::MySqlValueReader::uint64At(row, 6) == 0 && mysql::MySqlValueReader::stringAt(row, 7).empty() && mysql::MySqlValueReader::uint64At(row, 8) == 1, "supplement_task_claim_or_slot_changed");
        ++per_target[target];
    }
    scale::require(per_target == std::map<std::string, std::uint64_t> {{"local", 2100}, {"baidu", 2100}, {"gdrive", 2100}}, "supplement_target_backlog_changed");
    const std::set<std::uint64_t> measured_ids(ids.begin(), ids.begin() + 2100);
    const auto receipts = scale::execute(pool, "SELECT owner_type,resource_id,cloud_provider,task_state,claim_generation,COALESCE(cloud_path,'') FROM resource_cloud_receipts");
    for (const auto row : receipts.rows()) scale::require(mysql::MySqlValueReader::stringAt(row, 0) == "singer" && measured_ids.contains(mysql::MySqlValueReader::uint64At(row, 1)) && (mysql::MySqlValueReader::stringAt(row, 2) == "baidu" || mysql::MySqlValueReader::stringAt(row, 2) == "gdrive") && mysql::MySqlValueReader::stringAt(row, 3) == "pending" && mysql::MySqlValueReader::uint64At(row, 4) == 0 && mysql::MySqlValueReader::stringAt(row, 5).empty(), "supplement_foreign_or_claimed_receipt");
    for (const auto &profile : profiles(plan)) scale::require(scale::scalar(pool, "SELECT COUNT(*) FROM resource_destination_states WHERE destination='" + std::string(model::resourceDestinationName(profile.destination)) + "' AND dispatch_state='active' AND baseline_ready=1 AND target_fingerprint='" + profile.fingerprint + "' AND max_queued_transfers=10000") == 1, "supplement_target_or_limits_changed");
    scale::output({{"event", "supplement_guard"}, {"original_operations", 210}, {"original_tasks", tasks.rows().size()}, {"original_slots", 6300}, {"original_receipts", receipts.rows().size()}, {"per_target", per_target}, {"all_task_ids_belong_to_original_fixture", true}, {"foreign_work", 0}, {"new_workload_requests", 0}});
    return ids;
}

// 纯本机复现四worker各三端的实际staging，无数据库和网络依赖。
void stagingCheck(const std::filesystem::path &root)
{
    Staging staging(root);
    std::vector<std::vector<std::unique_ptr<c::application::ports::SongResourceBlobWriteSession>>> held;
    for (std::size_t index = 0; index < 4; ++index) held.push_back(staging.hold(index));
    const auto before = staging.result();
    scale::output({{"event", "staging_observation"}, {"phase", "writers_open"}, {"staging", before}});
    for (auto &sessions : held) staging.release(sessions);
    auto after = staging.result();
    after["sampling"] = "session_boundaries_only";
    scale::output({{"event", "staging_observation"}, {"phase", "after_abort"}, {"staging", after}});
    scale::require(before.at("peak_observed_bytes").get<std::uint64_t>() > 0 && after.at("final_bytes") == 0, "scale_staging_self_check_failed");
    scale::output({{"event", "completed"}, {"scope", "local_staging_check_only"}, {"staging", after}, {"database_connections", 0}, {"cloud_requests", 0}, {"historical_local_files_read", 0}});
}

// 完成事件后保持进程存活，让父进程可靠读取OS内核峰RSS。
void awaitParent()
{
    std::string command;
    std::getline(std::cin, command);
    scale::require(command == "release", "scale_parent_peak_rss_ack_required");
}

// 父进程独占窗口内测量；不认领、不消费积压，也不装配音乐源或云目标。
void measure(const std::shared_ptr<mysql::MySqlConnectionPool> &pool, const Json &plan, const Json &prepared, const std::filesystem::path &root)
{
    const auto ids = verifyFixtures(pool, plan, prepared);
    const auto song_ids = prepared.at("original_song_ids_first_middle_last").get<std::vector<std::uint64_t>>();
    scale::require(song_ids.size() == 3 && std::is_sorted(song_ids.begin(), song_ids.end()) && std::adjacent_find(song_ids.begin(), song_ids.end()) == song_ids.end(), "fixed_original_song_ids_required");
    Staging staging(root);
    std::atomic<bool> stopped {false};
    std::atomic<bool> scan_failed {false};
    std::atomic<std::uint64_t> periodic_scans {0};
    std::jthread scanner([&](std::stop_token token) {
        while (!token.stop_requested() && !stopped.load()) {
            try {
                staging.sample();
                ++periodic_scans;
            } catch (...) {
                scan_failed.store(true);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    });
    mysql::MySqlResourceTransferJobs jobs(pool);
    mysql::MySqlResourceTransferQueries queries(pool);
    std::mutex output_mutex;
    std::size_t offset = 0;
    for (const int concurrency : {1, 2, 4}) {
        const auto phase_offset = offset;
        const auto begin = Clock::now();
        std::barrier start(concurrency);
        std::vector<std::future<void>> pending;
        for (int worker = 0; worker < concurrency; ++worker) {
            pending.push_back(std::async(std::launch::async, [&, worker] {
                start.arrive_and_wait();
                for (int index = 0; index < 30; ++index) {
                    const auto record = sample(jobs, queries, pool, plan, ids, song_ids, phase_offset + (worker * 30 + index) * 10, concurrency, worker, index, staging);
                    std::lock_guard guard(output_mutex);
                    scale::output(record);
                }
            }));
        }
        for (auto &work : pending) work.get();
        const auto seconds = std::chrono::duration<double>(Clock::now() - begin).count();
        offset += concurrency * 300;
        const auto counts = taskCounts(pool);
        scale::require(counts.at("resource_transfer_tasks") == offset * 3 && counts.at("resource_transfer_slots") == offset * 3, "scale_actual_backlog_count_mismatch");
        scale::output({{"event", "phase"}, {"concurrency", concurrency}, {"samples", concurrency * 30}, {"wall_seconds", seconds}, {"accepted_requests_per_second", concurrency * 30 / seconds}, {"accepted_target_tasks_per_second", concurrency * 900 / seconds}, {"backlog_tasks", counts.at("resource_transfer_tasks")}, {"throughput_scope", "sustained_enqueue_with_find_status_hotspot_probe_and_generated_staging"}});
    }
    stopped.store(true);
    scanner.request_stop();
    scanner.join();
    scale::require(!scan_failed.load(), "scale_periodic_staging_scan_failed");
    auto staged = staging.result();
    staged["sampling"] = "session_boundaries_and_5ms_periodic";
    staged["periodic_scans"] = periodic_scans.load();
    staged["periodic_interval_ms"] = 5;
    scale::output({{"event", "staging_observation"}, {"staging", staged}});
    scale::require(periodic_scans.load() > 0 && staged.at("peak_observed_bytes").get<std::uint64_t>() > 0 && staged.at("final_bytes") == 0, "scale_actual_staging_not_observed_or_leaked");
    scale::output(boundary(pool, plan, ids));
    const auto final_counts = scale::backgroundCounts(pool);
    for (const auto &[name, original] : prepared.at("background_counts").items()) {
        if (name == "singers" || name == "singer_source_refs" || name == "singer_resources")
            scale::require(final_counts.at(name).get<std::uint64_t>() == original.get<std::uint64_t>() + ids.size(), "scale_final_fixture_count_changed");
        else if (name != "governance_events")
            scale::require(final_counts.at(name) == original, "scale_final_background_count_changed");
    }
    scale::output({{"event", "completed"}, {"samples", 210}, {"accepted_workload_operations", 210}, {"accepted_workload_tasks", 6300}, {"staging", staged}, {"final_task_counts", taskCounts(pool)}, {"final_background_counts", final_counts}, {"failures", 0}, {"cloud_requests", 0}, {"music_source_requests", 0}, {"historical_local_files_read", 0}, {"release_scope_bound", false}});
}
} // namespace

// 探针只提供两阶段原语；Python负责测前SHA封存、OS峰RSS与门槛判定。
int main(int argc, char **argv)
{
    try {
        scale::require(argc >= 3, "explicit_scale_mode_and_plan_required");
        const std::string mode = argv[1];
        if (mode == "staging-check" && argc == 3) {
            stagingCheck(std::filesystem::u8path(argv[2]));
            awaitParent();
            return 0;
        }
        const auto plan = scale::readJson(std::filesystem::u8path(argv[2]));
        validatePlan(plan);
        if (mode == "supplement") {
            scale::require(argc == 5, "supplement_original_inputs_required");
            const auto supplement = scale::readJson(std::filesystem::u8path(argv[4]));
            scale::require(supplement.at("format") == "crawler.resource-scale-supplement-plan.v1" && supplement.at("supplement_only") == true && supplement.at("original_scale_passed") == false && supplement.at("thresholds") == plan.at("thresholds"), "supplement_fixed_scope_and_thresholds_required");
            scale::require(scale::jsonFileSha256(std::filesystem::u8path(argv[2])) == supplement.at("original_inputs").at("plan_file_sha256").get<std::string>() && scale::jsonFileSha256(std::filesystem::u8path(argv[3])) == supplement.at("original_inputs").at("prepared_file_sha256").get<std::string>(), "supplement_original_bytes_changed");
        }
        const auto config = scale::configuration();
        boost::asio::io_context context;
        const auto observed = c::tests::migu::support::runInIoContext(context, scale::identity(config, plan), "scale exclusive preflight", std::chrono::seconds(30));
        auto pool = std::make_shared<mysql::MySqlConnectionPool>(config);
        if (mode == "prepare" && argc == 3)
            prepare(pool, plan, observed);
        else if (mode == "measure" && argc == 5) {
            measure(pool, plan, scale::readJson(std::filesystem::u8path(argv[3])), std::filesystem::u8path(argv[4]));
            // 保持进程及OS高水位计数存活，父进程读取实际峰RSS后释放。
            awaitParent();
        } else if (mode == "supplement" && argc == 5) {
            const auto prepared = scale::readJson(std::filesystem::u8path(argv[3]));
            const auto ids = supplementGuard(pool, plan, prepared);
            scale::output(boundary(pool, plan, ids));
            scale::output({{"event", "completed"}, {"supplement_only", true}, {"original_scale_passed", false}, {"new_workload_requests", 0}, {"final_task_counts", taskCounts(pool)}, {"cloud_requests", 0}, {"music_source_requests", 0}});
            awaitParent();
        } else
            throw std::runtime_error("invalid_scale_probe_arguments");
        return 0;
    } catch (const scale::Assertion &failure) {
        scale::output({{"event", "failure"}, {"result_code", failure.what()}});
        return 1;
    } catch (const std::exception &) {
        std::cerr << "scale_probe_failed: isolated assertion or dependency failed; preserve run artifacts\n";
        return 1;
    }
}
