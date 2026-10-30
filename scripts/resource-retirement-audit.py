#!/usr/bin/env python3
"""只读盘点旧云列/投影依赖，生成收缩门禁；永不执行DDL或删除源码。"""
import argparse
import datetime as dt
import hashlib
import json
from pathlib import Path
import re
import runpy
import sys

COMMON = runpy.run_path(str(Path(__file__).with_name("resource-release-check.py")))
PHYSICAL = runpy.run_path(str(Path(__file__).with_name("resource-retirement-sql.py")))
TABLES = ("song_resources", "singer_resources", "album_resources", "song_resource_artifacts")
PROJECTIONS = ("resource_legacy_profiles", "resource_legacy_batches", "resource_legacy_operations")
SUFFIXES = {".cpp", ".h", ".sql", ".yaml", ".md"}


def candidates(root):
    """合并当前结构与V2913固定DROP定义，收缩后仍保留候选身份。"""
    columns, indexes = {}, {}
    for table in TABLES:
        text = (root / "sql/mysql" / table / "create.sql").read_text(encoding="utf-8")
        columns[table] = sorted(set(re.findall(r"^\s*`?(cloud_[a-z_]+)`?\s+(?:varchar|text|bigint|tinyint|json)\b", text, re.M | re.I)))
        indexes[table] = sorted(set(re.findall(r"\bKEY\s+`?(idx_[a-z_]*cloud[a-z_]*)`?\s*\(", text, re.I)))
    migrations = sorted((root / "sql/mysql/migrations").glob("V2026102913__*.sql"))
    if len(migrations) > 1:
        raise ValueError("ambiguous_cloud_contract_migration")
    if migrations:
        text = migrations[0].read_text(encoding="utf-8")
        for match in re.finditer(r"ALTER\s+TABLE\s+`?(\w+)`?(.*?)(?=ALTER\s+TABLE|\Z)", text, re.I | re.S):
            table, body = match.group(1).lower(), match.group(2)
            if table not in columns:
                continue
            dropped = re.findall(r"DROP\s+COLUMN\s+`?(cloud_[a-z_]+)`?", body, re.I)
            keys = re.findall(r"DROP\s+INDEX\s+`?(idx_[a-z_]*cloud[a-z_]*)`?", body, re.I)
            columns[table] = sorted(set(columns[table]) | {value.lower() for value in dropped})
            indexes[table] = sorted(set(indexes[table]) | {value.lower() for value in keys})
    return columns, indexes


def category(name):
    """冻结迁移归档单独列出，不计为可删除的运行期依赖。"""
    if name.startswith("sql/mysql/migrations/"):
        return "frozen_migration_history"
    if name.endswith("/create.sql"):
        return "schema_snapshot"
    if name == "sql/mysql/schema/schema_status.sql":
        return "schema_guard"
    if name.startswith("docs/api/") or name.startswith("include/crawler/api/"):
        return "public_contract"
    if name.startswith("sql/mysql/"):
        return "runtime_sql"
    return "production_code"


def audit(root):
    """固定目录、后缀及大小上界，输出符号/行号而非可能敏感的源码正文。"""
    columns, indexes = candidates(root)
    symbols = set(PROJECTIONS)
    for values in columns.values():
        symbols.update(values)
    pattern = re.compile(r"\b(?:" + "|".join(re.escape(v) for v in sorted(symbols, key=len, reverse=True)) + r")\b")
    references, physical, scanned, errors = [], [], {}, []
    for directory in ("include", "src", "sql/mysql", "docs/api/mg"):
        base = root / directory
        if not base.is_dir():
            errors.append("required_source_directory_missing:" + directory)
            continue
        for path in sorted(base.rglob("*")):
            if not path.is_file() or path.suffix not in SUFFIXES:
                continue
            name = path.relative_to(root).as_posix()
            if path.is_symlink() or not path.resolve().is_relative_to(root) or path.stat().st_size > 32 * 1024 * 1024:
                errors.append("source_outside_scope_or_unbounded:" + name)
                continue
            if len(scanned) >= 10000:
                raise ValueError("source_inventory_unbounded")
            data = path.read_bytes()
            scanned[name] = hashlib.sha256(data).hexdigest()
            if name.startswith("sql/mysql/") and name.endswith(".sql") and category(name) not in ("frozen_migration_history", "schema_snapshot"):
                physical.extend({"file": name, **entry} for entry in PHYSICAL["physical_references"](data.decode("utf-8"), columns))
            for number, line in enumerate(data.decode("utf-8").splitlines(), 1):
                for symbol in sorted(set(pattern.findall(line))):
                    references.append({"file": name, "line": number, "symbol": symbol, "category": category(name)})
    counts = {}
    for entry in references:
        counts[entry["category"]] = counts.get(entry["category"], 0) + 1
    runtime = sum(counts.get(v, 0) for v in ("production_code", "runtime_sql", "schema_guard", "public_contract"))
    identity = {"candidate_columns": columns, "candidate_indexes": indexes, "projection_tables": PROJECTIONS, "files": scanned}
    return {"format": "crawler.resource-retirement-inventory.v1", "inventory_sha256": COMMON["canonical"](identity),
            "candidate_columns": columns, "candidate_indexes": indexes, "projection_tables": list(PROJECTIONS),
            "files_scanned": len(scanned), "reference_counts": counts, "runtime_dependencies": runtime,
            "physical_sql_dependencies": len(physical), "physical_sql_references": physical,
            "physical_sql_scanner_sha256": hashlib.sha256(Path(__file__).with_name("resource-retirement-sql.py").read_bytes()).hexdigest(),
            "physical_sql_scan_scope": "fixed SQL identifiers only; migration history/snapshots, strings, JSON keys and receipt/DTO projections excluded; verify executable SQL on contracted clone separately",
            "references": references, "errors": errors, "scan_complete": not errors,
            "destructive_operations_performed": 0, "drop_authorized": False,
            "preserve": ["migration history and checksums", "local receipts", "content/source/target versions", "historical skip and audit", "unified cloud receipts", "unresolved object intents"]}


def retirement_check(inventory, attestation, release, now=None):
    """门禁检查不能替代兼容期观测，也不生成可执行删列迁移。"""
    now = now or dt.datetime.now(dt.timezone.utc)
    errors = []
    if not inventory["scan_complete"] or inventory["runtime_dependencies"] != 0:
        errors.append("live_dependencies_remain_or_scan_incomplete")
    if attestation.get("format") != "crawler.resource-retirement-attestation.v1" or attestation.get("inventory_sha256") != inventory["inventory_sha256"]:
        errors.append("attestation_scope_mismatch")
    if release.get("format") != "crawler.resource-release-check.v1" or release.get("evidence_complete") is not True or release.get("scope_sha256") != attestation.get("release_scope_sha256"):
        errors.append("release_evidence_incomplete_or_scope_mismatch")
    try:
        start = COMMON["timestamp"](attestation["compatibility_started_at"])
        end = COMMON["timestamp"](attestation["compatibility_ends_at"])
        last = COMMON["timestamp"](attestation["last_legacy_request_at"])
        days = attestation["minimum_compatibility_days"]
        if type(days) is not int or days < 1 or (end - start).total_seconds() < days * 86400 or now < end or last > start:
            errors.append("compatibility_window_not_satisfied")
    except (ValueError, TypeError, KeyError, OverflowError):
        errors.append("compatibility_window_unknown")
    for key in ("old_clients_remaining", "legacy_writers_remaining", "unresolved_remote_objects"):
        if type(attestation.get(key)) is not int or attestation[key] != 0:
            errors.append(key + "_not_zero")
    for key in ("history_adoption_complete", "backup_restore_verified", "legacy_projection_export_verified"):
        if attestation.get(key) is not True:
            errors.append(key + "_not_verified")
    if not COMMON["HEX"].fullmatch(attestation.get("legacy_projection_export_sha256", "")):
        errors.append("legacy_projection_export_digest_missing")
    return {"format": "crawler.resource-retirement-check.v1", "inventory_sha256": inventory["inventory_sha256"],
            "preconditions_complete": not errors, "errors": sorted(set(errors)), "drop_authorized": False,
            "destructive_operations_performed": 0, "note": "Prepare a separately reviewed forward migration only after all preconditions are verified."}


def main():
    """支持只读盘点和可审阅的门禁检查，输出不能覆盖旧审阅结果。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("inventory", "check"))
    parser.add_argument("--repo", type=Path, default=Path(__file__).resolve().parents[1])
    parser.add_argument("--attestation", type=Path)
    parser.add_argument("--release-check", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args()
    try:
        inventory = audit(args.repo.resolve())
        if args.command == "check":
            if not args.attestation or not args.release_check:
                raise ValueError("retirement_evidence_required")
            result = retirement_check(inventory, COMMON["read_json"](args.attestation), COMMON["read_json"](args.release_check))
        else:
            result = inventory
        COMMON["write_json"](args.output, result)
        print(json.dumps({"written": True, "runtime_dependencies": inventory["runtime_dependencies"],
                          "physical_sql_dependencies": inventory["physical_sql_dependencies"],
                          "preconditions_complete": result.get("preconditions_complete", False), "drop_authorized": False}))
        return 0 if args.command == "inventory" and inventory["scan_complete"] or result.get("preconditions_complete") else 2
    except (ValueError, KeyError, TypeError, OSError, UnicodeError):
        print("FAIL: source inventory or retirement evidence check failed", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
