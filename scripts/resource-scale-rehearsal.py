#!/usr/bin/env python3
"""真实MySQL受理/查询规模预演；prepare封存，measure须显式独占窗口。"""
import argparse
import ctypes
import datetime as dt
import hashlib
import json
import math
import os
from pathlib import Path
import re
import subprocess
import sys
import tempfile
import time

DATABASE = "crawler_ownership_test_release_scale4_1030"
AUTHORIZED_DATABASES = (DATABASE, "crawler_ownership_test_release_1030_331763",
                        "crawler_ownership_test_release_scale4b_1030", "crawler_ownership_test_release_scale4c_1030")
SERVER_UUID = "79c305ac-d413-11f1-8dd9-b025aa3a9cbb"
REPOSITORY = Path(__file__).resolve().parents[1]
SOURCES = ("scripts/resource-scale-rehearsal.py", "tests/migu/integration/ResourceScaleProbe.cpp",
           "tests/migu/support/ResourceScaleSupport.h")
RUNTIME_SOURCE_ROOTS = ("src/infrastructure/music/persistence/mysql",
                        "include/crawler/infrastructure/music/persistence/mysql", "sql/mysql")
MAX_LOG_BYTES = 2 * 1024 * 1024


def now():
    """所有证据时间使用明确UTC时区。"""
    return dt.datetime.now(dt.timezone.utc).isoformat()


def digest(path):
    """只对显式输入和本轮生成文件流式计算SHA-256。"""
    value = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(65536):
            value.update(block)
    return value.hexdigest()


def canonical(value):
    """规范JSON摘要封存范围、负载和门槛，而非测后描述。"""
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":"), allow_nan=False).encode("utf-8")


def write_json(path, value):
    """输出必须全新，禁止覆盖失败现场或旧证据。"""
    with path.open("x", encoding="utf-8", newline="\n") as stream:
        stream.write(json.dumps(value, ensure_ascii=False, indent=2, sort_keys=True, allow_nan=False) + "\n")


def read_json(path):
    """读取有界普通JSON，拒绝链接和非有限数值。"""
    if path.is_symlink() or not path.is_file() or path.stat().st_size > MAX_LOG_BYTES:
        raise ValueError("bounded_regular_json_required")
    return json.loads(path.read_text(encoding="utf-8"), parse_constant=lambda _: (_ for _ in ()).throw(ValueError("nonfinite_json")))


def source_manifest(root=REPOSITORY):
    """绑定工具、生产MySQL实现/头及运行SQL；完整发布范围仍由主验收核对。"""
    paths = {root / name for name in SOURCES}
    for name in RUNTIME_SOURCE_ROOTS:
        directory = root / name
        if not directory.is_dir():
            raise ValueError("runtime_source_directory_missing")
        paths.update(path for path in directory.rglob("*") if path.suffix in (".cpp", ".h", ".sql"))
    if any(not path.is_file() or path.is_symlink() or not path.resolve().is_relative_to(root.resolve()) for path in paths):
        raise ValueError("bounded_regular_runtime_source_required")
    return {path.relative_to(root).as_posix(): digest(path) for path in sorted(paths)}


def environment(database):
    """仅接受显式本轮连接环境，不读取私有env或密码命令参数。"""
    names = ("HOST", "PORT", "USER", "PASSWORD")
    values = {name: os.environ.get("CRAWLER_SCALE_MYSQL_" + name) for name in names}
    if any(value is None for value in values.values()) or values["HOST"] != "127.0.0.1" or values["PORT"] != "6111" or not values["USER"] or database not in AUTHORIZED_DATABASES:
        raise ValueError("explicit_authorized_restore_connection_required")
    result = {key: value for key, value in os.environ.items() if not key.startswith("CRAWLER_") and key != "MYSQL_PWD"}
    result.update({"CRAWLER_SCALE_MYSQL_" + name: value for name, value in values.items()})
    result["CRAWLER_SCALE_MYSQL_DATABASE"] = database
    return result


def make_plan(run_id, probe, database=DATABASE):
    """首轮工程阈值在任何受理测量前固定，失败后不自动放宽。"""
    if not re.fullmatch(r"[a-z0-9]{8,24}", run_id):
        raise ValueError("run_id_requires_8_to_24_lowercase_letters_or_digits")
    if database not in AUTHORIZED_DATABASES:
        raise ValueError("authorized_restore_database_required")
    fingerprints = {target: hashlib.sha256(("controlled_fixture:" + run_id + ":" + target).encode()).hexdigest()
                    for target in ("local", "baidu", "gdrive")}
    return {"format": "crawler.resource-scale-plan.v1", "not_before": now(),
            "scope": {"scope_type": "controlled_fixture", "run_id": run_id, "database": database,
                      "server_uuid": SERVER_UUID, "host": "127.0.0.1", "port": 6111, "schema_version": 2026102912,
                      "minimum_background_rows": 4439979, "minimum_song_resources": 1505955,
                      "synthetic_target_fingerprints": fingerprints, "probe_sha256": digest(probe),
                      "tool_source_sha256": source_manifest(), "production_authorized": False,
                      "release_scope_bound": False, "historical_local_scope": "excluded_preserve_existing_paths"},
            "workload": {"concurrency": [1, 2, 4], "samples_per_worker": 30, "resources_per_request": 10,
                         "targets_per_request": 3, "accepted_requests": 210, "accepted_target_tasks": 6300,
                         "fixture_resources": 2108, "staging_bytes_per_target": 1048576,
                         "staging_scan_period_ms": 5, "queue_transfer_limit": 10000,
                         "capacity_contenders": 4, "capacity_remaining_slots": 1,
                         "queries": ["MySqlResourceTransferJobs.find", "MySqlResourceTransferQueries.status:new_Singer_10_IDs", "MySqlResourceTransferQueries.status:original_Song_first_middle_last_3_IDs"],
                         "lock_metric": "same_destination_rows_FOR_UPDATE_execute_elapsed_including_network_and_SQL_not_pure_InnoDB_wait",
                         "throughput_metric": "sustained_acceptance_including_find_status_hotspot_probe_and_generated_staging",
                         "staging_scope": "actual_FileStorage_uncommitted_generated_fixture_only_no_cloud_IO"},
            "thresholds": {"unexpected_failures_max": 0, "expected_capacity_rejections": 4,
                           "enqueue_p95_ms_max": 1000, "query_p95_ms_max": 250,
                           "lock_acquisition_upper_bound_p95_ms_max": 500,
                           "accepted_requests_per_second_min_each_phase": 2,
                           "probe_peak_rss_bytes_max": 512 * 1024 * 1024,
                           "staging_peak_bytes_max": 64 * 1024 * 1024,
                           "measure_timeout_seconds": 600, "prepare_timeout_seconds": 600}}


def validate_plan(plan, probe):
    """固定首轮场景，不允许封存文件把生产目标或旧ID伪装为fixture。"""
    scope, work, thresholds = plan["scope"], plan["workload"], plan["thresholds"]
    if plan["format"] != "crawler.resource-scale-plan.v1" or scope["scope_type"] != "controlled_fixture" or scope["database"] not in AUTHORIZED_DATABASES or scope["server_uuid"] != SERVER_UUID or scope["production_authorized"] is not False or scope["release_scope_bound"] is not False:
        raise ValueError("invalid_controlled_fixture_scope")
    if work["concurrency"] != [1, 2, 4] or work["samples_per_worker"] != 30 or work["resources_per_request"] != 10 or work["accepted_target_tasks"] != 6300 or work["fixture_resources"] != 2108:
        raise ValueError("fixed_first_round_workload_required")
    if not 1 <= thresholds["measure_timeout_seconds"] <= 1800 or not 1 <= thresholds["prepare_timeout_seconds"] <= 1800:
        raise ValueError("bounded_scale_timeout_required")
    if source_manifest() != scope["tool_source_sha256"] or digest(probe) != scope["probe_sha256"]:
        raise ValueError("sealed_tool_or_binary_changed")
    return hashlib.sha256(canonical(plan)).hexdigest()


def process_peak_rss(process):
    """读取OS进程工作集高水位，绝不用轮询RSS最大值冒充内核峰值。"""
    if os.name == "nt":
        class Counters(ctypes.Structure):
            """Windows PROCESS_MEMORY_COUNTERS布局。"""
            _fields_ = [("cb", ctypes.c_ulong), ("PageFaultCount", ctypes.c_ulong),
                        ("PeakWorkingSetSize", ctypes.c_size_t), ("WorkingSetSize", ctypes.c_size_t),
                        ("QuotaPeakPagedPoolUsage", ctypes.c_size_t), ("QuotaPagedPoolUsage", ctypes.c_size_t),
                        ("QuotaPeakNonPagedPoolUsage", ctypes.c_size_t), ("QuotaNonPagedPoolUsage", ctypes.c_size_t),
                        ("PagefileUsage", ctypes.c_size_t), ("PeakPagefileUsage", ctypes.c_size_t)]
        counters = Counters()
        counters.cb = ctypes.sizeof(counters)
        function = ctypes.WinDLL("psapi", use_last_error=True).GetProcessMemoryInfo
        function.argtypes = [ctypes.c_void_p, ctypes.POINTER(Counters), ctypes.c_ulong]
        function.restype = ctypes.c_int
        if not function(int(process._handle), ctypes.byref(counters), counters.cb):
            raise ValueError("OS_peak_working_set_unavailable")
        return int(counters.PeakWorkingSetSize), "Windows_GetProcessMemoryInfo_PeakWorkingSetSize"
    if sys.platform.startswith("linux"):
        status = Path("/proc") / str(process.pid) / "status"
        match = re.search(r"^VmHWM:\s+(\d+) kB$", status.read_text(), re.M)
        if not match:
            raise ValueError("OS_VmHWM_unavailable")
        return int(match[1]) * 1024, "Linux_proc_status_VmHWM"
    raise ValueError("OS_peak_rss_method_unsupported")


def records(path):
    """原始日志位于受保护目录，公开证据只复制已解析固定JSON事件。"""
    if path.stat().st_size > MAX_LOG_BYTES:
        raise ValueError("native_log_bound_exceeded")
    result = []
    for line in path.read_text(encoding="utf-8").splitlines():
        if line.startswith("{"):
            try:
                value = json.loads(line)
            except json.JSONDecodeError:
                continue  # 原子JSON行尚未写完时下轮再读，最终结构会严格检查。
            if value.get("event") in ("prepared", "sample", "phase", "staging_observation", "supplement_guard", "failure", "capacity_boundary", "completed"):
                result.append(value)
    return result


def invoke(probe, arguments, env, log, timeout, measure=False):
    """只管理所持Popen句柄；完成事件后保持native存活直至实际峰RSS已读。"""
    peak, method, final_live_peak_read, controller_failure = 0, None, False, None
    with log.open("xb") as stream:
        process = subprocess.Popen([str(probe), *arguments], env=env, stdin=subprocess.PIPE if measure else subprocess.DEVNULL,
                                   stdout=stream, stderr=subprocess.STDOUT, cwd=REPOSITORY,
                                   creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        try:
            deadline = time.monotonic() + timeout
            while process.poll() is None:
                if time.monotonic() >= deadline or log.stat().st_size > MAX_LOG_BYTES:
                    raise ValueError("scale_native_deadline_or_log_bound_exceeded")
                if measure:
                    observed, method = process_peak_rss(process)
                    peak = max(peak, observed)
                    rows = records(log)
                    if not final_live_peak_read and rows and rows[-1].get("event") == "completed":
                        observed, method = process_peak_rss(process)
                        peak = max(peak, observed)
                        final_live_peak_read = True
                        process.stdin.write(b"release\n")
                        process.stdin.flush()
                        process.stdin.close()
                time.sleep(.02)
        except (ValueError, OSError, subprocess.SubprocessError):
            controller_failure = "scale_controller_deadline_OS_or_log_failure"
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=10)
            if measure and process.stdin and not process.stdin.closed:
                process.stdin.close()
        # 失败也保留已完成事件与最后实际OS读数，final标志区分完整生命周期峰。
        return records(log), {"pid": process.pid, "exit_code": process.returncode, "peak_rss_bytes": peak,
                              "method": method, "final_peak_read_while_alive": final_live_peak_read,
                              "controller_failure": controller_failure,
                              "scope": "ResourceScaleProbe_process_only_excludes_MySQL_server_and_Python_driver"}


def quantiles(values):
    """最近秩p50/p95；所有样本保留，不丢弃慢值或预热值。"""
    if not values or any(isinstance(value, bool) or not isinstance(value, (int, float)) or not math.isfinite(value) or value < 0 for value in values):
        raise ValueError("finite_nonnegative_samples_required")
    ordered = sorted(values)
    return {"samples": len(values), "p50_ms": ordered[math.ceil(.5 * len(ordered)) - 1],
            "p95_ms": ordered[math.ceil(.95 * len(ordered)) - 1], "max_ms": ordered[-1]}


def summarize(plan, rows, memory):
    """先校验样本身份/真实积压，再按已封存门槛逐档判定，预期拒绝单列。"""
    if memory.get("exit_code") != 0 or memory.get("controller_failure") is not None or memory.get("final_peak_read_while_alive") is not True or type(memory.get("peak_rss_bytes")) is not int or memory["peak_rss_bytes"] <= 0:
        raise ValueError("complete_same_process_OS_peak_required")
    samples = [row for row in rows if row["event"] == "sample"]
    phases = [row for row in rows if row["event"] == "phase"]
    completed = [row for row in rows if row["event"] == "completed"]
    boundaries = [row for row in rows if row["event"] == "capacity_boundary"]
    expected = {(concurrency, worker, sample) for concurrency in (1, 2, 4) for worker in range(concurrency) for sample in range(30)}
    observed = {(row["concurrency"], row["worker"], row["sample"]) for row in samples}
    if observed != expected or len(samples) != 210 or len({row["operation_id"] for row in samples}) != 210 or any(row["accepted_tasks"] != 30 or row["passed"] is not True for row in samples) or len(phases) != 3 or [row["concurrency"] for row in phases] != [1, 2, 4] or len(completed) != 1 or len(boundaries) != 1:
        raise ValueError("scale_sample_or_completion_coverage_invalid")
    final, boundary = completed[0], boundaries[0]
    staging = final["staging"]
    if staging.get("method") != "actual_open_ifstream_ate_EOF" or staging.get("sampling") != "session_boundaries_and_5ms_periodic" or staging.get("periodic_interval_ms") != 5 or type(staging.get("periodic_scans")) is not int or staging["periodic_scans"] <= 0 or type(staging.get("peak_observed_bytes")) is not int or type(staging.get("final_bytes")) is not int:
        raise ValueError("complete_actual_periodic_staging_required")
    if final["accepted_workload_tasks"] != 6300 or final["failures"] != 0 or final["staging"]["final_bytes"] != 0 or final["staging"]["peak_observed_bytes"] <= 0 or boundary["accepted"] != 1 or boundary["expected_rejections"] != 4 or not boundary["idempotent_replay_preserved"] or not boundary["multi_target_atomic_rollback"]:
        raise ValueError("scale_real_workload_or_boundary_invalid")
    limits, failures, details = plan["thresholds"], [], []
    expected_backlog = {1: 900, 2: 2700, 4: 6300}
    for phase in phases:
        selected = [row for row in samples if row["concurrency"] == phase["concurrency"]]
        if phase["samples"] != len(selected) or phase["backlog_tasks"] != expected_backlog[phase["concurrency"]] or not isinstance(phase["wall_seconds"], (int, float)) or not math.isfinite(phase["wall_seconds"]) or phase["wall_seconds"] <= 0:
            raise ValueError("scale_phase_samples_backlog_or_time_invalid")
        actual_rate = len(selected) / phase["wall_seconds"]
        if not math.isclose(actual_rate, phase["accepted_requests_per_second"], rel_tol=1e-9) or not math.isclose(30 * actual_rate, phase["accepted_target_tasks_per_second"], rel_tol=1e-9):
            raise ValueError("scale_phase_rate_does_not_match_actual_count_and_time")
        stats = {name: quantiles([row[name] for row in selected]) for name in
                 ("enqueue_ms", "find_ms", "status_ms", "old_song_status_ms", "lock_acquisition_upper_bound_ms")}
        checks = {"enqueue_p95": stats["enqueue_ms"]["p95_ms"] <= limits["enqueue_p95_ms_max"],
                  "find_p95": stats["find_ms"]["p95_ms"] <= limits["query_p95_ms_max"],
                  "status_p95": stats["status_ms"]["p95_ms"] <= limits["query_p95_ms_max"],
                  "old_song_status_p95": stats["old_song_status_ms"]["p95_ms"] <= limits["query_p95_ms_max"],
                  "lock_upper_bound_p95": stats["lock_acquisition_upper_bound_ms"]["p95_ms"] <= limits["lock_acquisition_upper_bound_p95_ms_max"],
                  "sustained_acceptance_rate": phase["accepted_requests_per_second"] >= limits["accepted_requests_per_second_min_each_phase"]}
        failures.extend("concurrency_" + str(phase["concurrency"]) + ":" + name for name, passed in checks.items() if not passed)
        details.append(dict(phase, latency=stats, checks=checks))
    if memory["peak_rss_bytes"] <= 0 or memory["peak_rss_bytes"] > limits["probe_peak_rss_bytes_max"]:
        failures.append("probe_peak_rss")
    if final["staging"]["peak_observed_bytes"] > limits["staging_peak_bytes_max"]:
        failures.append("actual_staging_peak")
    counts = final["final_task_counts"]
    if counts["resource_transfer_operations"] != 211 or counts["resource_transfer_tasks"] != 6301 or counts["resource_transfer_slots"] != 6301:
        raise ValueError("scale_final_real_task_counts_invalid")
    if {target: data["transfers"] for target, data in boundary["final_usage"].items()} != {"local": 2101, "baidu": 2100, "gdrive": 2100} or not all(data["exact"] for data in boundary["final_usage"].values()):
        raise ValueError("scale_each_destination_actual_backlog_invalid")
    return {"samples": 210, "unexpected_failures": 0, "expected_capacity_rejections": 4,
            "accepted_workload_operations": 210, "accepted_workload_target_tasks": 6300,
            "phase_results": details, "memory": memory, "staging": final["staging"],
            "capacity_boundary": boundary, "final_task_counts": counts,
            "workload_backlog_per_destination_before_capacity_test": {"local": 2100, "baidu": 2100, "gdrive": 2100},
            "backlog_near_default_10000_limit": False,
            "threshold_failures": failures, "passed": not failures,
            "lock_statistics_scope": plan["workload"]["lock_metric"], "pure_InnoDB_wait_distribution_available": False,
            "query_statistics_scope": "native_repository_call_elapsed_excludes_process_startup_includes_connection_acquisition_and_SQL",
            "throughput_statistics_scope": plan["workload"]["throughput_metric"],
            "staging_statistics_scope": plan["workload"]["staging_scope"]}


def prepare(args):
    """受控种子追加后保存ID清单；不自动开始测量。"""
    if not args.writers_stopped or args.database not in AUTHORIZED_DATABASES or not args.probe.is_file():
        raise ValueError("explicit_fixture_prepare_and_stopped_writers_required")
    env = environment(args.database)
    args.output_dir.mkdir()
    args.private_dir.mkdir()
    root, private = args.output_dir.resolve(), args.private_dir.resolve()
    if private.is_relative_to(REPOSITORY) or root == private:
        raise ValueError("separate_external_private_log_directory_required")
    plan = make_plan(args.run_id, args.probe, args.database)
    plan_path = root / "plan.json"
    write_json(plan_path, plan)
    scope_sha = validate_plan(plan, args.probe)
    write_json(root / "plan-seal.json", {"plan_file_sha256": digest(plan_path), "scope_sha256": scope_sha,
                                         "sealed_at": now(), "measurement_started": False})
    rows, native = invoke(args.probe, ["prepare", str(plan_path)], env, private / "prepare-native.log",
                     plan["thresholds"]["prepare_timeout_seconds"])
    if native["exit_code"] != 0 or native["controller_failure"] or len(rows) != 1 or rows[0].get("event") != "prepared" or rows[0].get("fixture_resources") != 2108:
        raise ValueError("scale_prepare_result_invalid")
    write_json(root / "prepared.json", rows[0])
    write_json(root / "prepared-seal.json", {"prepared_file_sha256": digest(root / "prepared.json"),
                                            "scope_sha256": scope_sha, "sealed_at": now()})
    result = {"format": "crawler.resource-scale-prepare.v1", "exit_code": 0, "prepared": True,
            "scope_sha256": scope_sha, "plan_file_sha256": digest(plan_path),
            "fixture_resources": 2108, "measurement_started": False, "release_scope_bound": False,
            "output_directory": str(root), "private_directory": str(private)}
    write_json(root / "prepare-result.json", result)
    return result


def measure(args):
    """仅显式独占窗口运行；所有门槛和二进制必须与prepare封存一致。"""
    if not args.exclusive_window or not args.writers_stopped:
        raise ValueError("explicit_exclusive_window_and_stopped_writers_required")
    root = args.prepared_dir.resolve()
    plan, seal, prepared_seal = (read_json(root / name) for name in ("plan.json", "plan-seal.json", "prepared-seal.json"))
    scope_sha = validate_plan(plan, args.probe)
    if digest(root / "plan.json") != seal["plan_file_sha256"] or scope_sha != seal["scope_sha256"] or scope_sha != prepared_seal["scope_sha256"] or digest(root / "prepared.json") != prepared_seal["prepared_file_sha256"]:
        raise ValueError("scale_preparation_seal_changed")
    if (root / "measure-started.json").exists():
        raise ValueError("scale_prepared_run_already_measured_no_reuse")
    args.output_dir.mkdir()
    args.private_dir.mkdir()
    output, private = args.output_dir.resolve(), args.private_dir.resolve()
    if private.is_relative_to(REPOSITORY) or output == private:
        raise ValueError("separate_external_private_log_directory_required")
    staging = private / "staging"
    staging.mkdir()
    started = now()
    write_json(root / "measure-started.json", {"scope_sha256": scope_sha, "started_at": started,
                                              "exclusive_window_declared": True, "writers_stopped_declared": True})
    result = {"format": "crawler.resource-scale-rehearsal.v1", "execution": "actual_native_repositories_mysql_and_FileStorage_fixture",
              "scope_type": "controlled_fixture", "scope_sha256": scope_sha, "started_at": started,
              "release_scope_bound": False, "production_authorized": False, "production_writes": 0,
              "cloud_requests": 0, "music_source_requests": 0, "historical_local_files_read": 0,
              "historical_local_scope": "excluded_existing_paths_preserved", "synthetic_cloud_targets": True,
              "service_workers_tested": False, "ten_million_rows_tested": False,
              "measurement_complete": False, "same_run_rss_and_staging": False}
    try:
        rows, memory = invoke(args.probe, ["measure", str(root / "plan.json"), str(root / "prepared.json"), str(staging)],
                              environment(plan["scope"]["database"]), private / "measure-native.log",
                              plan["thresholds"]["measure_timeout_seconds"], measure=True)
        write_json(output / "native-events.json", rows)
        result["native_process"] = memory
        if memory["exit_code"] != 0 or memory["controller_failure"] or not memory["final_peak_read_while_alive"]:
            raise ValueError("scale_native_failed_or_final_OS_peak_unread")
        result["measurements"] = summarize(plan, rows, memory)
        result.update(measurement_complete=True, same_run_rss_and_staging=True)
        result["exit_code"] = 0 if result["measurements"]["passed"] else 2
        result["result_code"] = "passed" if result["exit_code"] == 0 else "sealed_threshold_failed"
    except (ValueError, OSError, KeyError, TypeError, subprocess.SubprocessError):
        result.update(exit_code=1, result_code="scale_actual_execution_failed_preserve_artifacts", passed=False)
    prepared = read_json(root / "prepared.json")
    result["background_counts_before_fixture"] = prepared["background_counts"]
    result["background_rows_before_fixture"] = sum(prepared["background_counts"].values())
    result["song_resources_before_fixture"] = prepared["background_counts"]["song_resources"]
    result["finished_at"] = now()
    result["artifacts"] = [{"path": name, "bytes": (output / name).stat().st_size, "sha256": digest(output / name)}
                           for name in ("native-events.json",) if (output / name).exists()]
    result["private_native_log_sha256"] = digest(private / "measure-native.log") if (private / "measure-native.log").exists() else None
    write_json(output / "scale-result.json", result)
    return result


def original_inputs(prepared_dir, failure_result):
    """补充运行只引用原失败现场，原字节SHA或原scope任一变动立即拒绝。"""
    root = prepared_dir.resolve()
    plan, seal, prepared_seal = (read_json(root / name) for name in ("plan.json", "plan-seal.json", "prepared-seal.json"))
    failure = read_json(failure_result)
    scope = hashlib.sha256(canonical(plan)).hexdigest()
    inputs = {"plan_file_sha256": digest(root / "plan.json"), "prepared_file_sha256": digest(root / "prepared.json"),
              "failure_result_file_sha256": digest(failure_result), "original_scope_sha256": scope}
    if inputs["plan_file_sha256"] != seal["plan_file_sha256"] or inputs["prepared_file_sha256"] != prepared_seal["prepared_file_sha256"] or scope != seal["scope_sha256"] or scope != prepared_seal["scope_sha256"] or scope != failure["scope_sha256"]:
        raise ValueError("supplement_original_byte_seal_changed")
    if plan["scope"]["database"] not in AUTHORIZED_DATABASES or plan["scope"]["server_uuid"] != SERVER_UUID or plan["scope"]["scope_type"] != "controlled_fixture" or failure["exit_code"] == 0 or failure["release_scope_bound"] is not False:
        raise ValueError("supplement_requires_original_controlled_failed_run")
    return plan, inputs


def supplement(args):
    """独立补充封存：本机staging自检或只完成容量边界，绝不重放规模负载。"""
    database_action = args.command == "supplement"
    if database_action and (not args.exclusive_window or not args.writers_stopped):
        raise ValueError("supplement_exclusive_window_and_stopped_writers_required")
    original, inputs = original_inputs(args.prepared_dir, args.failure_result)
    env = environment(original["scope"]["database"]) if database_action else {key: value for key, value in os.environ.items() if not key.startswith("CRAWLER_") and key != "MYSQL_PWD"}
    args.output_dir.mkdir()
    args.private_dir.mkdir()
    output, private = args.output_dir.resolve(), args.private_dir.resolve()
    if private.is_relative_to(REPOSITORY) or output == private:
        raise ValueError("separate_external_private_log_directory_required")
    plan = {"format": "crawler.resource-scale-supplement-plan.v1", "mode": args.command,
            "supplement_only": True, "original_scale_passed": False, "release_scope_bound": False,
            "not_before": now(), "original_inputs": inputs, "thresholds": original["thresholds"],
            "scope": original["scope"], "new_probe_sha256": digest(args.probe), "new_tool_source_sha256": source_manifest(),
            "authorized_new_work": "capacity_boundary_only_maximum_one_local_task" if database_action else "generated_local_staging_only_no_database"}
    plan_path = output / "supplement-plan.json"
    write_json(plan_path, plan)
    write_json(output / "supplement-plan-seal.json", {"plan_file_sha256": digest(plan_path),
                                                      "scope_sha256": hashlib.sha256(canonical(plan)).hexdigest(), "sealed_at": now()})
    result = {"format": "crawler.resource-scale-supplement.v1", "mode": args.command, "supplement_only": True,
              "original_scale_passed": False, "release_scope_bound": False, "production_authorized": False,
              "started_at": now(), "original_inputs": inputs, "supplement_plan_file_sha256": digest(plan_path),
              "production_writes": 0, "cloud_requests": 0, "music_source_requests": 0,
              "historical_local_files_read": 0, "new_scale_workload_requests": 0}
    result["rss_belongs_to_original_scale_process"] = False
    try:
        if source_manifest() != plan["new_tool_source_sha256"] or digest(args.probe) != plan["new_probe_sha256"]:
            raise ValueError("supplement_new_tool_or_binary_changed")
        if database_action:
            arguments = ["supplement", str(args.prepared_dir.resolve() / "plan.json"),
                         str(args.prepared_dir.resolve() / "prepared.json"), str(plan_path)]
        else:
            staging = private / "staging"
            staging.mkdir()
            arguments = ["staging-check", str(staging)]
        rows, memory = invoke(args.probe, arguments, env, private / "native.log", 120, measure=True)
        write_json(output / "native-events.json", rows)
        result["native_process"] = memory
        completed = [row for row in rows if row["event"] == "completed"]
        if memory["exit_code"] != 0 or memory["controller_failure"] or not memory["final_peak_read_while_alive"] or len(completed) != 1:
            raise ValueError("supplement_native_did_not_complete")
        if database_action:
            guards = [row for row in rows if row["event"] == "supplement_guard"]
            boundaries = [row for row in rows if row["event"] == "capacity_boundary"]
            if len(guards) != 1 or guards[0]["original_operations"] != 210 or guards[0]["original_tasks"] != 6300 or guards[0]["foreign_work"] != 0 or guards[0]["per_target"] != {"local": 2100, "baidu": 2100, "gdrive": 2100} or len(boundaries) != 1:
                raise ValueError("supplement_original_backlog_guard_failed")
            boundary, final = boundaries[0], completed[0]["final_task_counts"]
            if boundary["accepted"] != 1 or boundary["expected_rejections"] != original["thresholds"]["expected_capacity_rejections"] or not boundary["idempotent_replay_preserved"] or not boundary["multi_target_atomic_rollback"] or final["resource_transfer_operations"] != 211 or final["resource_transfer_tasks"] != 6301 or final["resource_transfer_slots"] != 6301:
                raise ValueError("supplement_capacity_boundary_failed")
            result.update(guard=guards[0], capacity_boundary=boundary, final_task_counts=final)
        else:
            observed = completed[0]["staging"]
            if observed["peak_observed_bytes"] <= 0 or observed["peak_observed_bytes"] > original["thresholds"]["staging_peak_bytes_max"] or observed["final_bytes"] != 0:
                raise ValueError("supplement_actual_staging_failed")
            result["staging"] = observed
        if memory["peak_rss_bytes"] <= 0 or memory["peak_rss_bytes"] > original["thresholds"]["probe_peak_rss_bytes_max"]:
            raise ValueError("supplement_OS_peak_RSS_threshold_failed")
        if original_inputs(args.prepared_dir, args.failure_result)[1] != inputs:
            raise ValueError("supplement_original_evidence_changed_after_run")
        result.update(exit_code=0, supplement_passed=True)
    except (ValueError, OSError, KeyError, TypeError, subprocess.SubprocessError):
        result.update(exit_code=1, supplement_passed=False, result_code="supplement_failed_preserve_original_and_new_artifacts")
    result["finished_at"] = now()
    result["artifacts"] = [{"path": name, "bytes": (output / name).stat().st_size, "sha256": digest(output / name)}
                           for name in ("supplement-plan.json", "supplement-plan-seal.json", "native-events.json") if (output / name).exists()]
    result["private_native_log_sha256"] = digest(private / "native.log") if (private / "native.log").exists() else None
    write_json(output / "supplement-result.json", result)
    return result


def self_test():
    """标准库离线自测覆盖封存、样本、失败门槛和真实子进程OS高水位。"""
    checks = 0
    if quantiles(list(range(1, 31))) != {"samples": 30, "p50_ms": 15, "p95_ms": 29, "max_ms": 30}:
        raise ValueError("quantile_self_test_failed")
    checks += 1
    for invalid in ([], [True], [float("nan")], [-1]):
        try:
            quantiles(invalid)
        except ValueError:
            checks += 1
        else:
            raise ValueError("invalid_sample_accepted")
    with tempfile.TemporaryDirectory(prefix="crawler-scale-selftest-") as name:
        root = Path(name)
        probe = root / "probe.bin"
        probe.write_bytes(b"offline identity fixture")
        for database in AUTHORIZED_DATABASES:
            if make_plan("selftest20261030", probe, database)["scope"]["database"] != database:
                raise ValueError("exact_database_selection_self_test_failed")
        for database in ("crawler", "crawler_ownership_test_release_other", "crawler_ownership_test_release_scale4b_1030_extra"):
            try:
                make_plan("selftest20261030", probe, database)
            except ValueError:
                continue
            raise ValueError("unapproved_database_accepted")
        checks += 1
        source_root = root / "manifest-fixture"
        for source in SOURCES:
            path = source_root / source
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("offline fixture", encoding="utf-8")
        for directory in RUNTIME_SOURCE_ROOTS:
            path = source_root / directory / "fixture.h"
            path.parent.mkdir(parents=True, exist_ok=True)
            path.write_text("first", encoding="utf-8")
        manifest = source_manifest(source_root)
        changed_source = source_root / RUNTIME_SOURCE_ROOTS[0] / "fixture.h"
        changed_source.write_text("changed", encoding="utf-8")
        if source_manifest(source_root) == manifest:
            raise ValueError("runtime_source_drift_accepted")
        checks += 1
        new_sql = source_root / "sql/mysql/fixture.sql"
        new_sql.write_text("SELECT 1", encoding="utf-8")
        if "sql/mysql/fixture.sql" not in source_manifest(source_root):
            raise ValueError("new_runtime_sql_missing_from_manifest")
        checks += 1
        plan = make_plan("selftest20261030", probe)
        first = validate_plan(plan, probe)
        original = root / "original"
        original.mkdir()
        write_json(original / "plan.json", plan)
        write_json(original / "prepared.json", {"scope": plan["scope"], "offline_fixture": True})
        write_json(original / "plan-seal.json", {"plan_file_sha256": digest(original / "plan.json"), "scope_sha256": first})
        write_json(original / "prepared-seal.json", {"prepared_file_sha256": digest(original / "prepared.json"), "scope_sha256": first})
        failure = root / "failure.json"
        write_json(failure, {"scope_sha256": first, "exit_code": 1, "release_scope_bound": False})
        if original_inputs(original, failure)[1]["original_scope_sha256"] != first:
            raise ValueError("original_supplement_chain_self_test_failed")
        checks += 1
        with (original / "prepared.json").open("a", encoding="utf-8") as changed:
            changed.write("\n")
        try:
            original_inputs(original, failure)
        except ValueError:
            checks += 1
        else:
            raise ValueError("original_byte_change_accepted")
        fixture_rows = []
        for concurrency, backlog in ((1, 900), (2, 2700), (4, 6300)):
            for worker in range(concurrency):
                for sample in range(30):
                    fixture_rows.append({"event": "sample", "concurrency": concurrency, "worker": worker,
                                         "sample": sample, "operation_id": f"fixture-{concurrency}-{worker}-{sample}",
                                         "accepted_tasks": 30, "passed": True, "enqueue_ms": 5, "find_ms": 1,
                                         "status_ms": 2, "old_song_status_ms": 3, "lock_acquisition_upper_bound_ms": .5})
            fixture_rows.append({"event": "phase", "concurrency": concurrency, "samples": concurrency * 30,
                                 "backlog_tasks": backlog, "wall_seconds": 10,
                                 "accepted_requests_per_second": concurrency * 3,
                                 "accepted_target_tasks_per_second": concurrency * 90})
        fixture_rows.extend([{"event": "capacity_boundary", "accepted": 1, "expected_rejections": 4,
                              "idempotent_replay_preserved": True, "multi_target_atomic_rollback": True,
                              "final_usage": {target: {"transfers": 2101 if target == "local" else 2100, "exact": True}
                                              for target in ("local", "baidu", "gdrive")}},
                             {"event": "completed", "accepted_workload_tasks": 6300, "failures": 0,
                              "staging": {"final_bytes": 0, "peak_observed_bytes": 12 * 1024 * 1024,
                                          "method": "actual_open_ifstream_ate_EOF", "periodic_interval_ms": 5,
                                          "sampling": "session_boundaries_and_5ms_periodic", "periodic_scans": 30},
                              "final_task_counts": {"resource_transfer_operations": 211,
                                                    "resource_transfer_tasks": 6301, "resource_transfer_slots": 6301}}])
        fixture_memory = {"peak_rss_bytes": 64 * 1024 * 1024, "exit_code": 0,
                          "controller_failure": None, "final_peak_read_while_alive": True}
        if not summarize(plan, fixture_rows, fixture_memory)["passed"]:
            raise ValueError("complete_coverage_self_test_failed")
        checks += 1
        for invalid in (dict(fixture_memory, final_peak_read_while_alive=False),
                        dict(fixture_memory, peak_rss_bytes=True)):
            try:
                summarize(plan, fixture_rows, invalid)
            except ValueError:
                checks += 1
            else:
                raise ValueError("partial_or_invalid_OS_peak_accepted")
        fixture_rows[-1]["staging"]["periodic_scans"] = 0
        try:
            summarize(plan, fixture_rows, fixture_memory)
        except ValueError:
            checks += 1
        else:
            raise ValueError("unobserved_periodic_staging_accepted")
        fixture_rows[-1]["staging"]["periodic_scans"] = 30
        if "probe_peak_rss" not in summarize(plan, fixture_rows, dict(fixture_memory, peak_rss_bytes=513 * 1024 * 1024))["threshold_failures"]:
            raise ValueError("complete_but_over_limit_RSS_not_threshold_failure")
        checks += 1
        fixture_rows[0]["old_song_status_ms"] = 1000
        fixture_rows[1]["old_song_status_ms"] = 1000
        if "concurrency_1:old_song_status_p95" not in summarize(plan, fixture_rows, fixture_memory)["threshold_failures"]:
            raise ValueError("separate_old_song_threshold_self_test_failed")
        checks += 1
        fixture_rows[0]["old_song_status_ms"] = fixture_rows[1]["old_song_status_ms"] = 3
        fixture_rows[0]["operation_id"] = fixture_rows[1]["operation_id"]
        try:
            summarize(plan, fixture_rows, fixture_memory)
        except ValueError:
            checks += 1
        else:
            raise ValueError("duplicate_operation_self_test_failed")
        fixture_rows[0]["operation_id"] = "fixture-1-0-0"
        fixture_rows[30]["backlog_tasks"] = 9000
        try:
            summarize(plan, fixture_rows, fixture_memory)
        except ValueError:
            checks += 1
        else:
            raise ValueError("fake_backlog_self_test_failed")
        plan["thresholds"]["query_p95_ms_max"] += 1
        if validate_plan(plan, probe) == first:
            raise ValueError("threshold_change_did_not_change_scope")
        checks += 1
        probe.write_bytes(b"changed")
        try:
            validate_plan(plan, probe)
        except ValueError:
            checks += 1
        else:
            raise ValueError("binary_change_accepted")
        path = root / "single.json"
        write_json(path, {"value": 1})
        try:
            write_json(path, {"value": 2})
        except FileExistsError:
            checks += 1
        else:
            raise ValueError("evidence_overwrite_accepted")
        child = subprocess.Popen([sys.executable, "-c", "import sys; x=bytearray(4*1024*1024); print('ready',flush=True); sys.stdin.readline()"],
                                 stdin=subprocess.PIPE, stdout=subprocess.PIPE,
                                 creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        try:
            if child.stdout.readline().strip() != b"ready" or process_peak_rss(child)[0] <= 4 * 1024 * 1024:
                raise ValueError("OS_peak_rss_self_test_failed")
            checks += 1
        finally:
            if child.poll() is None:
                child.stdin.write(b"release\n"); child.stdin.flush()
                child.wait(timeout=10)
            child.stdin.close(); child.stdout.close()
        failure_rows, failure_memory = invoke(Path(sys.executable), ["-c", "import json,time,sys; x=bytearray(4194304); print(json.dumps({'event':'failure','result_code':'offline_expected_failure'}),flush=True); time.sleep(.1); sys.exit(1)"],
                                              os.environ.copy(), root / "failure-native.log", 10, measure=True)
        if len(failure_rows) != 1 or failure_rows[0]["event"] != "failure" or failure_memory["exit_code"] != 1 or failure_memory["peak_rss_bytes"] <= 0 or failure_memory["final_peak_read_while_alive"]:
            raise ValueError("failed_native_evidence_not_preserved")
        checks += 1
    return {"format": "crawler.resource-scale-self-test.v1", "exit_code": 0, "checks": checks,
            "mysql_connections": 0, "load_started": False, "actual_OS_peak_rss_tested": True}


def main():
    """prepare允许受控种子，measure另需操作者显式打开独占窗口。"""
    parser = argparse.ArgumentParser(description=__doc__)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("self-test")
    setup = commands.add_parser("prepare")
    setup.add_argument("--database", required=True, choices=AUTHORIZED_DATABASES)
    setup.add_argument("--run-id", required=True)
    setup.add_argument("--probe", type=Path, required=True)
    setup.add_argument("--output-dir", type=Path, required=True)
    setup.add_argument("--private-dir", type=Path, required=True)
    setup.add_argument("--writers-stopped", action="store_true")
    run = commands.add_parser("measure")
    run.add_argument("--prepared-dir", type=Path, required=True)
    run.add_argument("--probe", type=Path, required=True)
    run.add_argument("--output-dir", type=Path, required=True)
    run.add_argument("--private-dir", type=Path, required=True)
    run.add_argument("--exclusive-window", action="store_true")
    run.add_argument("--writers-stopped", action="store_true")
    for mode in ("staging-check", "supplement"):
        extra = commands.add_parser(mode)
        extra.add_argument("--prepared-dir", type=Path, required=True)
        extra.add_argument("--failure-result", type=Path, required=True)
        extra.add_argument("--probe", type=Path, required=True)
        extra.add_argument("--output-dir", type=Path, required=True)
        extra.add_argument("--private-dir", type=Path, required=True)
        if mode == "supplement":
            extra.add_argument("--exclusive-window", action="store_true")
            extra.add_argument("--writers-stopped", action="store_true")
    args = parser.parse_args()
    try:
        result = self_test() if args.command == "self-test" else prepare(args) if args.command == "prepare" else measure(args) if args.command == "measure" else supplement(args)
    except (ValueError, OSError, KeyError, TypeError, subprocess.SubprocessError):
        result = {"format": "crawler.resource-scale-error.v1", "exit_code": 1,
                  "result_code": "scale_input_preparation_or_dependency_failed_preserve_artifacts"}
    print(json.dumps(result, ensure_ascii=False, allow_nan=False))
    return result["exit_code"]


if __name__ == "__main__":
    raise SystemExit(main())
