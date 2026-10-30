#!/usr/bin/env python3
"""只读隔离库的历史路径摘要；不访问路径所指文件，不构成文件恢复证据。"""
import argparse
import datetime as dt
import hashlib
import json
from pathlib import Path
import runpy
import sys
import tempfile
import time

COMMON = runpy.run_path(str(Path(__file__).with_name("resource-release-check.py")))
NATIVE = runpy.run_path(str(Path(__file__).with_name("resource-release-rehearsal.py")))
OWNERS = ("song_resources", "singer_resources", "album_resources", "song_resource_artifacts")
FORMAT = "crawler.resource-local-path-manifest.v1"


def uint(value):
    """高水位和计数只接受真实无符号整数。"""
    return type(value) is int and 0 <= value <= 2**64 - 1


def content(value):
    """内容摘要不绑定实例名，支持升级前后及跨实例比较。"""
    return {key: value[key] for key in ("format", "owner_highwaters", "paths_count", "rows_sha256")}


def validate_reference(value):
    """只接受完整、固定owner集合且自身摘要一致的参考清单。"""
    waters = value.get("owner_highwaters")
    if value.get("format") != FORMAT or not isinstance(waters, dict) or set(waters) != set(OWNERS):
        raise ValueError("path_reference_invalid")
    if any(not uint(v) for v in waters.values()) or not uint(value.get("paths_count")):
        raise ValueError("path_reference_numbers_invalid")
    if any(not isinstance(value.get(key), str) or not COMMON["HEX"].fullmatch(value[key])
           for key in ("rows_sha256", "path_manifest_sha256")):
        raise ValueError("path_reference_digest_invalid")
    if COMMON["canonical"](content(value)) != value["path_manifest_sha256"]:
        raise ValueError("path_reference_tampered")
    return waters


def hash_rows(stream, waters):
    """验证有界的owner/id/路径散列行；从不读取或输出路径正文。"""
    digest = hashlib.sha256()
    count = 0
    previous = (-1, 0)
    for raw in stream:
        if len(raw) > 256 or count >= 1000000:
            raise ValueError("path_rows_unbounded")
        fields = raw.decode("ascii").strip().split("\t")
        if len(fields) != 3 or fields[0] not in OWNERS or not fields[1].isdigit() or not COMMON["HEX"].fullmatch(fields[2]):
            raise ValueError("path_row_invalid")
        owner, resource_id = fields[0], int(fields[1])
        current = (OWNERS.index(owner), resource_id)
        if not 0 < resource_id <= waters[owner] or current <= previous:
            raise ValueError("path_row_order_or_scope_invalid")
        digest.update(("\t".join(fields) + "\n").encode("ascii"))
        previous = current
        count += 1
    return count, digest.hexdigest()


def snapshot(args):
    """冻结历史owner高水位，只比较该范围的非空路径字符串。"""
    if not COMMON["DATABASE"].fullmatch(args.database) or not args.writers_stopped:
        raise ValueError("stopped_isolated_database_required")
    if not 1 <= args.timeout_seconds <= 60:
        raise ValueError("timeout_invalid")
    reference = COMMON["read_json"](args.reference_manifest) if args.reference_manifest else None
    waters = validate_reference(reference) if reference else {}
    values, environment = NATIVE["private_environment"]("CRAWLER_RELEASE_SOURCE_MYSQL_")
    deadline = time.monotonic() + args.timeout_seconds
    def remaining():
        left = deadline - time.monotonic()
        if left <= 0:
            raise ValueError("path_snapshot_deadline_exceeded")
        return left
    def query(sql):
        return NATIVE["query"](args.mysql, values, environment, args.database, sql, remaining())
    server_uuid = query("SELECT @@server_uuid")
    if server_uuid != args.expected_server_uuid:
        raise ValueError("path_snapshot_server_mismatch")
    statements = []
    for owner in OWNERS:
        exists = query("SELECT COUNT(*) FROM information_schema.tables WHERE table_schema=DATABASE() AND table_name='" + owner + "'") == "1"
        if not reference:
            waters[owner] = int(query("SELECT COALESCE(MAX(id),0) FROM " + owner)) if exists else 0
        if not uint(waters[owner]) or (not exists and waters[owner] > 0):
            raise ValueError("path_owner_missing_or_highwater_invalid")
        if exists:
            statements.append("SELECT '" + owner + "',id,LOWER(SHA2(CAST(local_path AS BINARY),256)) FROM " + owner
                              + " WHERE id<=" + str(waters[owner]) + " AND local_path IS NOT NULL AND OCTET_LENGTH(local_path)>0 ORDER BY id")
    # 固定SQL使用只读一致快照；数据流只包含路径散列，不含原路径。
    sql = "START TRANSACTION READ ONLY;" + ";".join(statements) + ";COMMIT;"
    with tempfile.TemporaryFile() as captured:
        NATIVE["run"](NATIVE["mysql_arguments"](args.mysql, values, args.database) + ["--execute=" + sql],
                      environment, remaining(), output=captured, max_bytes=128 * 1024**2)
        captured.seek(0)
        count, rows_digest = hash_rows(captured, waters)
    result = {"format": FORMAT, "owner_highwaters": waters, "paths_count": count, "rows_sha256": rows_digest,
              "server_uuid": server_uuid, "database": args.database, "created_at": dt.datetime.now(dt.timezone.utc).isoformat(),
              "filesystem_checked": False, "production_authorized": False}
    result["path_manifest_sha256"] = COMMON["canonical"](content(result))
    result["matches_reference"] = (result["path_manifest_sha256"] == reference["path_manifest_sha256"]) if reference else None
    COMMON["write_json"](args.output, result)
    return result


def main():
    """认证只来自显式进程环境；失败不回显原始数据库错误。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--mysql", type=Path, required=True)
    parser.add_argument("--database", required=True)
    parser.add_argument("--expected-server-uuid", required=True)
    parser.add_argument("--writers-stopped", action="store_true")
    parser.add_argument("--reference-manifest", type=Path)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--timeout-seconds", type=int, default=60)
    args = parser.parse_args()
    try:
        result = snapshot(args)
        print(json.dumps({key: result[key] for key in ("paths_count", "path_manifest_sha256", "matches_reference", "filesystem_checked")}))
        return 2 if result["matches_reference"] is False else 0
    except (ValueError, TypeError, KeyError, OSError, OverflowError, UnicodeError):
        print("FAIL: local path metadata snapshot failed", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
