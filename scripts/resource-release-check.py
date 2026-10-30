#!/usr/bin/env python3
"""离线上线证据门禁；不连接数据库、启动服务或授权生产切换。"""
import argparse
import datetime as dt
import hashlib
import json
import math
from pathlib import Path
import re
import sys
import time

KINDS = ("history_adoption", "writer_compatibility", "writers_stopped", "clone_upgrade",
         "database_restore", "file_restore", "multi_instance_crash", "scale_workload", "cloud_live")
HEX = re.compile(r"^[0-9a-f]{64}$")
DATABASE = re.compile(r"^crawler_ownership_test_release_[a-z0-9_]{1,24}$")


def canonical(value):
    """摘要仅绑定明确定义的范围，排除文件路径及认证。"""
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(",", ":"), ensure_ascii=False).encode()).hexdigest()


def timestamp(value):
    """证据时刻必须带时区，避免宿主时区改变有效期。"""
    if not isinstance(value, str):
        raise ValueError("timestamp_requires_string")
    parsed = dt.datetime.fromisoformat(value.replace("Z", "+00:00"))
    if parsed.tzinfo is None:
        raise ValueError("timestamp_requires_timezone")
    return parsed.astimezone(dt.timezone.utc)


def read_json(path):
    """只读取有界的机器可读元数据，不打印输入正文。"""
    if path.stat().st_size > 2 * 1024 * 1024:
        raise ValueError("metadata_too_large")
    with path.open(encoding="utf-8") as stream:
        value = json.load(stream)
    if not isinstance(value, dict):
        raise ValueError("metadata_requires_object")
    return value


def write_json(path, value):
    """独占创建输出，避免改写审阅过的证据。"""
    with path.open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2)
        stream.write("\n")


def positive(value):
    """拒绝布尔值、非有限数及没有意义的阈值。"""
    return type(value) in (int, float) and math.isfinite(value) and value > 0


def validate_plan(plan):
    """未填写范围与测量阈值的模板不能成为放行计划。"""
    if plan.get("format") != "crawler.resource-release-plan.v1":
        raise ValueError("plan_format_invalid")
    scope = plan["scope"]
    if not isinstance(scope, dict):
        raise ValueError("scope_requires_object")
    policy = scope.get("history_local_files", "verify")
    if policy not in ("verify", "preserve_paths_only"):
        raise ValueError("history_local_files_invalid")
    if policy == "preserve_paths_only" and (not isinstance(scope.get("local_path_manifest_sha256"), str) or not HEX.fullmatch(scope["local_path_manifest_sha256"])):
        raise ValueError("local_path_manifest_required")
    if not re.fullmatch(r"[A-Za-z0-9_.-]{1,96}", scope["release_id"]):
        raise ValueError("release_id_invalid")
    if not re.fullmatch(r"[0-9a-f]{40}", scope["revision"]):
        raise ValueError("revision_invalid")
    if type(scope["schema_version"]) is not int or scope["schema_version"] < 2026102912:
        raise ValueError("schema_version_invalid")
    if not HEX.fullmatch(scope["database_identity"]) or not HEX.fullmatch(scope["baseline_sha256"]):
        raise ValueError("scope_digest_invalid")
    targets = scope["target_fingerprints"]
    if not isinstance(targets, dict) or set(targets) != {"local", "baidu", "gdrive"} or any(not HEX.fullmatch(v) for v in targets.values()):
        raise ValueError("target_scope_invalid")
    if len(set(targets.values())) != 3:
        raise ValueError("target_scope_not_independent")
    for key in ("source_manifest_sha256", "cli_sha256", "server_sha256"):
        if not HEX.fullmatch(scope[key]):
            raise ValueError("build_scope_digest_invalid")
    revisions = scope["target_revisions"]
    if not isinstance(revisions, dict) or set(revisions) != set(targets) or any(type(v) is not int or v < 1 for v in revisions.values()):
        raise ValueError("target_revision_invalid")
    limits = plan["limits"]
    if not isinstance(limits, dict):
        raise ValueError("limits_require_object")
    for key in ("minimum_rows", "minimum_samples", "query_p95_ms", "lock_wait_p95_ms", "minimum_enqueues_per_second", "peak_rss_bytes", "peak_staging_bytes", "recovery_seconds", "writers_observation_seconds", "max_evidence_age_hours"):
        if not positive(limits[key]):
            raise ValueError("measurement_limit_invalid")
    if limits["minimum_samples"] < 30 or limits["minimum_rows"] < 1:
        raise ValueError("workload_scope_invalid")
    if type(limits["minimum_samples"]) is not int or type(limits["minimum_rows"]) is not int:
        raise ValueError("workload_integer_required")
    timestamp(plan["not_before"])
    return canonical({"scope": scope, "limits": limits, "not_before": plan["not_before"]})


def artifact_errors(evidence, directory, deadline):
    """只校验报告目录内的普通证据文件，不跟随越界链接。"""
    errors = []
    artifacts = evidence.get("artifacts")
    if not isinstance(artifacts, list) or not 1 <= len(artifacts) <= 16:
        return ["artifacts_missing_or_unbounded"]
    root = directory.resolve()
    for entry in artifacts:
        if not isinstance(entry, dict):
            errors.append("artifact_metadata_requires_object")
            continue
        relative = entry.get("path", "")
        if not isinstance(relative, str) or not relative or Path(relative).is_absolute() or "\\" in relative or any(p in ("", ".", "..") for p in relative.split("/")):
            errors.append("artifact_path_invalid")
            continue
        path = root / relative
        resolved = path.resolve()
        if not resolved.is_relative_to(root) or not path.is_file() or path.is_symlink() or path.suffix.lower() not in (".log", ".json", ".jsonl", ".sql", ".dump", ".sha256"):
            errors.append("artifact_outside_scope")
            continue
        if type(entry.get("bytes")) is not int or path.stat().st_size != entry["bytes"] or not HEX.fullmatch(entry.get("sha256", "")):
            errors.append("artifact_metadata_mismatch")
            continue
        digest = hashlib.sha256()
        with path.open("rb") as stream:
            while block := stream.read(1024 * 1024):
                if time.monotonic() > deadline:
                    raise ValueError("verification_deadline_exceeded")
                digest.update(block)
        if digest.hexdigest() != entry["sha256"]:
            errors.append("artifact_digest_mismatch")
    return errors


def scenario_errors(kind, value, limits, scope):
    """各场景以可复核测量值判定，unknown/缺失均失败关闭。"""
    if not isinstance(value, dict):
        return ["measurements_require_object"]
    conditions = []
    numeric = {
        "history_adoption": ("source_conflicts", "unresolved_decisions"),
        "writer_compatibility": ("unmigrated_writers",),
        "writers_stopped": ("observation_seconds", "running_claims", "uncertain_tasks", "external_writers"),
        "clone_upgrade": ("before_schema_version", "schema_version", "business_rows_before", "business_rows_after", "unexpected_columns_removed", "production_writes"),
        "database_restore": ("production_writes",), "file_restore": ("missing_files", "mismatched_files"),
        "multi_instance_crash": ("instances", "claimed_tasks", "duplicate_publications", "stale_commits", "uncertain_overwrites", "post_recovery_completed", "recovery_seconds"),
        "scale_workload": ("rows", "samples", "failures", "query_p95_ms", "lock_wait_p95_ms", "enqueues_per_second", "peak_rss_bytes", "peak_staging_bytes")}
    if any(type(value.get(k)) not in (int, float) or not math.isfinite(value[k]) for k in numeric.get(kind, ())):
        return ["measurement_unknown_or_invalid"]
    continuous = {"observation_seconds", "recovery_seconds", "query_p95_ms", "lock_wait_p95_ms", "enqueues_per_second"}
    if any(type(value[k]) is not int for k in numeric.get(kind, ()) if k not in continuous):
        return ["measurement_integer_required"]
    if kind == "history_adoption":
        conditions = [value.get("all_targets_reviewed") is True, value.get("source_conflicts") == 0,
                      value.get("unresolved_decisions") == 0, value.get("baseline_sha256") == scope["baseline_sha256"]]
    elif kind == "writer_compatibility":
        conditions = [value.get("unmigrated_writers") == 0, value.get("old_clients_verified") is True]
    elif kind == "writers_stopped":
        conditions = [value.get("observation_seconds", -1) >= limits["writers_observation_seconds"],
                      value.get("running_claims") == 0, value.get("uncertain_tasks") == 0,
                      value.get("external_writers") == 0, value.get("all_instances_stopped") is True]
    elif kind == "clone_upgrade":
        conditions = [bool(DATABASE.fullmatch(value.get("database", ""))), value.get("before_schema_version", scope["schema_version"]) < scope["schema_version"],
                      value.get("schema_version") == scope["schema_version"], value.get("migration_checksums_verified") is True,
                      type(value.get("business_rows_before")) is int, value.get("business_rows_before") == value.get("business_rows_after"),
                      value.get("unexpected_columns_removed") == 0, value.get("production_writes") == 0]
        if scope.get("history_local_files") == "preserve_paths_only":
            conditions.extend([value.get("historical_local_paths_preserved") is True,
                               value.get("local_path_manifest_sha256_before") == scope["local_path_manifest_sha256"],
                               value.get("local_path_manifest_sha256_after") == scope["local_path_manifest_sha256"],
                               type(value.get("local_paths_count_before")) is int,
                               type(value.get("local_paths_count_after")) is int,
                               value.get("local_paths_count_before", -1) >= 0,
                               value.get("local_paths_count_before") == value.get("local_paths_count_after")])
    elif kind == "database_restore":
        conditions = [bool(DATABASE.fullmatch(value.get("database", ""))), bool(value.get("source_server_uuid")), bool(value.get("restore_server_uuid")),
                      value.get("source_server_uuid") != value.get("restore_server_uuid"), value.get("restore_was_empty") is True,
                      bool(HEX.fullmatch(value.get("source_data_sha256", ""))), value.get("source_data_sha256") == value.get("restored_data_sha256"),
                      value.get("migration_checksums_verified") is True, value.get("trigger_definitions_verified") is True,
                      value.get("triggers_installed_after_data") is True, value.get("production_writes") == 0]
    elif kind == "file_restore":
        conditions = [value.get("source_was_stopped") is True, value.get("journal_included") is True,
                      bool(HEX.fullmatch(value.get("source_manifest_sha256", ""))), value.get("source_manifest_sha256") == value.get("restored_manifest_sha256"),
                      value.get("missing_files") == 0, value.get("mismatched_files") == 0]
    elif kind == "multi_instance_crash":
        conditions = [value.get("instances", 0) >= 2, value.get("claimed_tasks", 0) > 0, value.get("forced_termination") is True,
                      value.get("duplicate_publications") == 0, value.get("stale_commits") == 0, value.get("uncertain_overwrites") == 0,
                      value.get("post_recovery_completed", 0) > 0, 0 <= value.get("recovery_seconds", -1) <= limits["recovery_seconds"]]
    elif kind == "scale_workload":
        conditions = [value.get("rows", 0) >= limits["minimum_rows"], value.get("samples", 0) >= limits["minimum_samples"], value.get("failures") == 0,
                      0 <= value.get("query_p95_ms", -1) <= limits["query_p95_ms"], 0 <= value.get("lock_wait_p95_ms", -1) <= limits["lock_wait_p95_ms"],
                      value.get("enqueues_per_second", 0) >= limits["minimum_enqueues_per_second"],
                      0 < value.get("peak_rss_bytes", 0) <= limits["peak_rss_bytes"], 0 <= value.get("peak_staging_bytes", -1) <= limits["peak_staging_bytes"]]
    elif kind == "cloud_live":
        destinations = value.get("destinations", {})
        conditions = [set(destinations) == {"local", "baidu", "gdrive"}, value.get("clouds_started_before_local_completed") is True]
        for name in ("local", "baidu", "gdrive"):
            item = destinations.get(name, {})
            if any(type(item.get(k)) is not int for k in ("published_objects", "verified_objects")):
                return ["measurement_unknown_or_invalid"]
            conditions.extend([item.get("actual_io") is True, item.get("target_fingerprint") == scope["target_fingerprints"][name],
                               item.get("published_objects", 0) > 0, item.get("verified_objects") == item.get("published_objects"),
                               item.get("sha256_verified") is True, item.get("stop_confirmed") is True])
    return [] if all(conditions) else ["scenario_requirements_not_met"]


def check(plan, directory, now=None, timeout=60):
    """检查证据一致性；结果是人工审阅输入，不是执行授权或真实性认证。"""
    scope_hash = validate_plan(plan)
    now = now or dt.datetime.now(dt.timezone.utc)
    deadline = time.monotonic() + timeout
    checks = []
    policy = plan["scope"].get("history_local_files", "verify")
    excluded = ([{"kind": "file_restore", "status": "excluded_not_tested",
                  "reason": "historical_local_files_exempt_paths_preserved_in_database"}]
                if policy == "preserve_paths_only" else [])
    required = [kind for kind in KINDS if kind not in {item["kind"] for item in excluded}]
    for kind in required:
        errors = []
        choices = [p for p in (directory / (kind + ".json"), directory / kind / (kind + ".json")) if p.is_file()]
        if not choices:
            checks.append({"kind": kind, "passed": False, "errors": ["evidence_missing"]})
            continue
        if len(choices) != 1 or choices[0].is_symlink() or not choices[0].resolve().is_relative_to(directory.resolve()):
            checks.append({"kind": kind, "passed": False, "errors": ["evidence_ambiguous_or_outside_scope"]})
            continue
        path = choices[0]
        try:
            record = read_json(path)
            if record.get("format") != "crawler.resource-release-evidence.v1" or record.get("kind") != kind or record.get("scope_sha256") != scope_hash:
                errors.append("evidence_scope_mismatch")
            if record.get("execution") != "actual":
                errors.append("fixture_or_unknown_execution")
            start, finish = timestamp(record["started_at"]), timestamp(record["finished_at"])
            if start < timestamp(plan["not_before"]) or finish < start or (finish - now).total_seconds() > 300 or (now - finish).total_seconds() > plan["limits"]["max_evidence_age_hours"] * 3600:
                errors.append("evidence_time_invalid_or_stale")
            if kind == "writers_stopped" and (now - finish).total_seconds() > 300:
                errors.append("writers_observation_stale")
            errors.extend(artifact_errors(record, path.parent, deadline))
            errors.extend(scenario_errors(kind, record.get("measurements", {}), plan["limits"], plan["scope"]))
        except (KeyError, TypeError, ValueError, OSError, OverflowError):
            errors.append("evidence_invalid")
        checks.append({"kind": kind, "passed": not errors, "errors": sorted(set(errors))})
    return {"format": "crawler.resource-release-check.v1", "scope_sha256": scope_hash,
            "evidence_complete": all(v["passed"] for v in checks), "checks": checks,
            "history_local_files": policy, "required_evidence": required, "excluded_evidence": excluded,
            "evidence_authenticity_independently_verified": False, "production_authorized": False,
            "activation_performed": False, "note": "Operator-supplied evidence consistency only; inspect execution artifacts before release."}


def template():
    """模板故意保留未知项，不能作为默认放行配置。"""
    return {"format": "crawler.resource-release-plan.v1", "not_before": dt.datetime.now(dt.timezone.utc).isoformat(),
            "scope": {"release_id": "fill-release-id", "revision": "", "schema_version": 2026102912,
                      "history_local_files": "verify",
                      "source_manifest_sha256": "", "cli_sha256": "", "server_sha256": "",
                      "database_identity": "", "baseline_sha256": "", "target_fingerprints": {"local": "", "baidu": "", "gdrive": ""},
                      "target_revisions": {"local": None, "baidu": None, "gdrive": None}},
            "limits": {"minimum_rows": 10000000, "minimum_samples": 30, "query_p95_ms": None,
                       "lock_wait_p95_ms": None, "minimum_enqueues_per_second": None, "peak_rss_bytes": None,
                       "peak_staging_bytes": None, "recovery_seconds": None, "writers_observation_seconds": None, "max_evidence_age_hours": 72}}


def main():
    """命令只创建新文件，失败诊断不回显原始输入。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("template", "check"))
    parser.add_argument("--plan", type=Path)
    parser.add_argument("--evidence-dir", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--timeout-seconds", type=int, default=60)
    args = parser.parse_args()
    try:
        if not 1 <= args.timeout_seconds <= 600:
            raise ValueError("timeout_invalid")
        if args.command == "template":
            result = template()
        else:
            if not args.plan or not args.evidence_dir or not args.evidence_dir.is_dir():
                raise ValueError("plan_and_evidence_directory_required")
            result = check(read_json(args.plan), args.evidence_dir, timeout=args.timeout_seconds)
        write_json(args.output, result)
        print(json.dumps({"written": True, "evidence_complete": result.get("evidence_complete", False), "production_authorized": False}))
        return 0 if args.command == "template" or result["evidence_complete"] else 2
    except (ValueError, TypeError, KeyError, OSError, OverflowError):
        print("FAIL: release evidence validation or exclusive output failed", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
