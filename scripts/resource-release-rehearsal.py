#!/usr/bin/env python3
"""仅隔离库的队列查询基准、认领恢复及不同实例的备份还原演练。"""
import argparse
import concurrent.futures
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import re
import runpy
import subprocess
import sys
import tempfile
import time

COMMON = runpy.run_path(str(Path(__file__).with_name("resource-release-check.py")))


def digest(path):
    """流式摘要，不把备份正文加载到内存或日志。"""
    value = hashlib.sha256()
    with path.open("rb") as stream:
        while data := stream.read(1024 * 1024):
            value.update(data)
    return value.hexdigest()


def run(arguments, environment, timeout, output=None, input_file=None, max_bytes=1024 * 1024):
    """不经过shell，不输出原生命令/错误，认证仅在进程环境内。"""
    with tempfile.TemporaryFile() as captured:
        target = output or captured
        process = subprocess.Popen(arguments, env=environment, stdin=input_file or subprocess.DEVNULL,
                                   stdout=target, stderr=subprocess.DEVNULL,
                                   creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        deadline = time.monotonic() + timeout
        while process.poll() is None:
            if time.monotonic() >= deadline or os.fstat(target.fileno()).st_size > max_bytes:
                process.kill()
                process.wait()
                raise ValueError("native_deadline_or_output_bound_exceeded")
            time.sleep(0.02)
        if process.returncode != 0 or os.fstat(target.fileno()).st_size > max_bytes:
            raise ValueError("native_dependency_failed_or_output_unbounded")
        if output:
            return ""
        captured.seek(0)
        return captured.read().decode("utf-8").strip()


def remaining(args):
    """每个调用也受整次演练剩余时间约束。"""
    left = args.deadline - time.monotonic()
    if left <= 0:
        raise ValueError("rehearsal_deadline_exceeded")
    return min(args.timeout_seconds, left)


def private_environment(prefix):
    """仅读取显式演练变量，不加载或复制任何私有env文件。"""
    if prefix not in ("CRAWLER_RELEASE_SOURCE_MYSQL_", "CRAWLER_RELEASE_RESTORE_MYSQL_"):
        raise ValueError("connection_prefix_invalid")
    values = {key: os.environ.get(prefix + key) for key in ("HOST", "PORT", "USER", "PASSWORD")}
    if any(v is None for v in values.values()) or not values["HOST"] or not values["USER"]:
        raise ValueError("explicit_rehearsal_connection_required")
    if not values["PORT"].isdigit() or not 1 <= int(values["PORT"]) <= 65535:
        raise ValueError("connection_port_invalid")
    # 子进程不继承原服务数据库/私有env定位，避免dotenv落回运行库。
    environment = {k: v for k, v in os.environ.items() if not k.startswith(("CRAWLER_", "MYSQL_"))}
    # MySQL 8.0没有--no-login-paths；显式隔离其仍会读取的login文件。
    environment["MYSQL_TEST_LOGIN_FILE"] = str(Path(tempfile.gettempdir()) / ("crawler-release-no-login-" + os.urandom(16).hex() + ".cnf"))
    environment.update({"MYSQL_PWD": values["PASSWORD"], "CRAWLER_MYSQL_HOST": values["HOST"],
                        "CRAWLER_MYSQL_PORT": values["PORT"], "CRAWLER_MYSQL_USER": values["USER"],
                        "CRAWLER_MYSQL_PASSWORD": values["PASSWORD"], "CRAWLER_MYSQL_INIT_SCHEMA": "false"})
    return values, environment


def mysql_arguments(executable, values, database):
    """固定TCP客户端参数，禁止默认文件暗中选择其他连接。"""
    return [str(executable), "--no-defaults", "--connect-timeout=5", "--protocol=TCP", "--host=" + values["HOST"],
            "--port=" + values["PORT"], "--user=" + values["USER"], "--database=" + database,
            "--batch", "--skip-column-names", "--default-character-set=utf8mb4"]


def query(executable, values, environment, database, sql, timeout):
    """查询语句由工具固定，不接受任意SQL输入。"""
    return run(mysql_arguments(executable, values, database) + ["--execute=" + sql], environment, timeout)


def evidence(args, kind, started, measurements, artifacts):
    """将实际命令结果绑定到预先审阅的发布范围。"""
    args.verify_scope()
    record = {"format": "crawler.resource-release-evidence.v1", "kind": kind, "scope_sha256": args.scope_hash,
              "execution": "actual", "started_at": started, "finished_at": dt.datetime.now(dt.timezone.utc).isoformat(),
              "measurements": measurements, "artifacts": [{"path": p.name, "bytes": p.stat().st_size, "sha256": digest(p)} for p in artifacts]}
    COMMON["write_json"](args.output_dir / (kind + ".json"), record)
    return record


def queue_command(args, environment, extra):
    """CLI也必须与显式库选择一致，dotenv指向新目录内不存在文件。"""
    environment = dict(environment)
    environment["CRAWLER_MYSQL_DATABASE"] = args.database
    environment["CRAWLER_ENV_FILE"] = str((args.output_dir / "no-dotenv-is-loaded").resolve())
    return run([str(args.cli), *extra, "--database", args.database], environment, remaining(args))


def benchmark(args, environment, started):
    """只测queue-status完整CLI时延，不冒充SQL/入队/媒体吞吐。"""
    if not 5 <= args.samples <= 1000 or not 1 <= args.concurrency <= 8:
        raise ValueError("benchmark_bounds_invalid")
    def sample(index):
        begin = time.monotonic()
        try:
            value = json.loads(queue_command(args, environment, ["queue-status"]))
            if not isinstance(value, list) or len(value) != 3 or {v.get("destination") for v in value} != {"local", "baidu", "gdrive"}:
                raise ValueError("queue_response_invalid")
            return {"sample": index, "elapsed_ms": (time.monotonic() - begin) * 1000, "passed": True}
        except (ValueError, OSError):
            return {"sample": index, "elapsed_ms": (time.monotonic() - begin) * 1000, "passed": False}
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.concurrency) as executor:
        rows = list(executor.map(sample, range(args.samples)))
    durations = sorted(v["elapsed_ms"] for v in rows)
    measurements = {"database": args.database, "samples": len(rows), "concurrency": args.concurrency,
                    "failures": sum(not v["passed"] for v in rows), "p50_ms": durations[(len(rows) - 1) // 2],
                    "p95_ms": durations[max(0, (95 * len(rows) + 99) // 100 - 1)], "max_ms": durations[-1],
                    "measurement": "queue-status CLI end-to-end latency", "enqueue_throughput_measured": False}
    log = args.output_dir / "queue-status-samples.jsonl"
    with log.open("x", encoding="utf-8", newline="\n") as stream:
        for row in rows:
            stream.write(json.dumps(row) + "\n")
    evidence(args, "queue_status_benchmark", started, measurements, [log])
    return 0 if measurements["failures"] == 0 else 2


def recover(args, environment, started):
    """仅在已声明停写的隔离库复用正式恢复入口，不启动worker。"""
    if not args.writers_stopped or not 1 <= args.stale_seconds <= 604800:
        raise ValueError("stopped_writers_and_stale_window_required")
    before = json.loads(queue_command(args, environment, ["queue-status"]))
    recovered = json.loads(queue_command(args, environment, ["recover-tasks", "--destination", args.destination,
                                                           "--stale-seconds", str(args.stale_seconds), "--writers-stopped"]))
    after = json.loads(queue_command(args, environment, ["queue-status"]))
    path = args.output_dir / "recover-tasks.json"
    COMMON["write_json"](path, {"before": before, "recovery": recovered, "after": after})
    evidence(args, "queue_recovery", started, {"database": args.database, "destination": args.destination,
                                              "stale_seconds": args.stale_seconds, "operator_declared_writers_stopped": True,
                                              "forced_termination_tested": False}, [path])
    return 0


def dump(args, executable, values, environment, target, data_only=False, schema_only=False, triggers_only=False, omit_triggers=False):
    """不加--databases，恢复时同名库位于另一个已核实实例。"""
    command = [str(executable), "--no-defaults", "--protocol=TCP", "--host=" + values["HOST"],
               "--port=" + values["PORT"], "--user=" + values["USER"], "--single-transaction",
               "--default-character-set=utf8mb4", "--no-tablespaces", "--skip-comments", "--skip-dump-date",
               "--set-gtid-purged=OFF", "--skip-add-locks", "--skip-lock-tables", "--column-statistics=0", "--quick", "--no-autocommit"]
    if data_only:
        command += ["--no-create-info", "--skip-triggers", "--order-by-primary", "--skip-extended-insert"]
    elif triggers_only:
        command += ["--no-create-info", "--no-data", "--triggers"]
    else:
        command += ["--routines", "--skip-triggers" if omit_triggers else "--triggers", "--skip-add-drop-table"]
    if schema_only:
        command += ["--no-data"]
    with target.open("xb") as output:
        run(command + [args.database], environment, remaining(args), output=output, max_bytes=args.max_backup_bytes)


def validate_schema(path, database):
    """有界检查DDL中的跨库引用；保留MySQL可执行版本注释，忽略字符串。"""
    if path.stat().st_size > 32 * 1024 * 1024:
        raise ValueError("schema_preview_unbounded")
    pattern = re.compile(r"/\*!(?:\d+)?(.*?)\*/|/\*.*?\*/|--[^\n]*(?:\n|$)|\#[^\n]*(?:\n|$)|'(?:\\.|''|[^'\\])*'|\"(?:\\.|\"\"|[^\"\\])*\"|`((?:``|[^`])*)`|([A-Za-z_][A-Za-z_0-9]*)|([.;()])", re.S)
    def tokens(text, depth=0):
        if depth > 4:
            raise ValueError("nested_schema_comments_invalid")
        values = []
        for match in pattern.finditer(text):
            if match.group(1) is not None:
                values.extend(tokens(match.group(1), depth + 1))
            elif match.group(2) is not None:
                values.append(match.group(2).replace("``", "`").lower())
            elif match.group(3) or match.group(4):
                values.append((match.group(3) or match.group(4)).lower())
        return values
    values = tokens(path.read_text(encoding="utf-8"))
    for i, token in enumerate(values):
        following = values[i+1: i+6]
        if token == "use" or token in ("create", "alter", "drop") and following and following[0] in ("database", "user"):
            raise ValueError("schema_database_or_account_mutation_rejected")
        if token == "grant" or token == "set" and following and following[0] == "global":
            raise ValueError("schema_server_mutation_rejected")
        if token in ("from", "join", "update", "into", "references", "view") and len(following) >= 3 and following[1] == "." and following[0] != database:
            raise ValueError("schema_cross_database_reference_rejected")
        if i + 4 < len(values) and values[i+1] == "." and values[i+3] == "." and token != database:
            raise ValueError("schema_cross_database_column_rejected")


def restore(args, source_values, source_environment, started):
    """禁止覆盖：同名隔离库、不同server_uuid、目标空库及停写缺一不可。"""
    if not args.writers_stopped or not args.mysql or not args.mysqldump:
        raise ValueError("restore_requires_clients_and_stopped_writers")
    restore_values, restore_environment = private_environment("CRAWLER_RELEASE_RESTORE_MYSQL_")
    sql = "SELECT @@server_uuid"
    source_id = query(args.mysql, source_values, source_environment, args.database, sql, remaining(args))
    restore_id = query(args.mysql, restore_values, restore_environment, args.database, sql, remaining(args))
    if not source_id or not restore_id or source_id == restore_id:
        raise ValueError("restore_requires_distinct_server_identity")
    tables_sql = "SELECT COUNT(*) FROM information_schema.tables WHERE table_schema=DATABASE()"
    routines_sql = "SELECT COUNT(*) FROM information_schema.routines WHERE routine_schema=DATABASE()"
    events_sql = "SELECT COUNT(*) FROM information_schema.events WHERE event_schema=DATABASE()"
    if any(query(args.mysql, restore_values, restore_environment, args.database, sql, remaining(args)) != "0" for sql in (tables_sql, routines_sql, events_sql)):
        raise ValueError("restore_database_not_empty")
    if query(args.mysql, source_values, source_environment, args.database, events_sql, remaining(args)) != "0":
        raise ValueError("scheduled_event_restore_not_supported")
    usage_sql = "SELECT COUNT(*) FROM information_schema.VIEW_TABLE_USAGE WHERE VIEW_SCHEMA=DATABASE() AND TABLE_SCHEMA<>DATABASE()"
    if query(args.mysql, source_values, source_environment, args.database, usage_sql, remaining(args)) != "0":
        raise ValueError("source_views_reference_other_database")
    migration_sql = "SELECT version,checksum FROM schema_migrations ORDER BY version"
    migrations_before = query(args.mysql, source_values, source_environment, args.database, migration_sql, remaining(args))
    backup, source_data, restored_data = (args.output_dir / name for name in ("backup.sql", "source-data.sql", "restored-data.sql"))
    schema_before, schema_after = (args.output_dir / name for name in ("schema-before.sql", "schema-after.sql"))
    triggers, restored_triggers = (args.output_dir / name for name in ("triggers.sql", "restored-triggers.sql"))
    dump(args, args.mysqldump, source_values, source_environment, schema_before, schema_only=True)
    validate_schema(schema_before, args.database)
    dump(args, args.mysqldump, source_values, source_environment, backup, omit_triggers=True)
    dump(args, args.mysqldump, source_values, source_environment, triggers, triggers_only=True)
    validate_schema(triggers, args.database)
    dump(args, args.mysqldump, source_values, source_environment, source_data, data_only=True)
    dump(args, args.mysqldump, source_values, source_environment, schema_after, schema_only=True)
    if digest(schema_before) != digest(schema_after):
        raise ValueError("source_schema_changed_during_backup")
    # dump只由固定客户端生成，恢复目标与源同名；不接受用户提交的任意SQL文件。
    with backup.open("rb") as source:
        run(mysql_arguments(args.mysql, restore_values, args.database), restore_environment, remaining(args), input_file=source)
    # 格式表INSERT触发器会更新资源代次；必须等全部原始数据恢复完再安装。
    with triggers.open("rb") as source:
        run(mysql_arguments(args.mysql, restore_values, args.database), restore_environment, remaining(args), input_file=source)
    dump(args, args.mysqldump, restore_values, restore_environment, restored_data, data_only=True)
    dump(args, args.mysqldump, restore_values, restore_environment, restored_triggers, triggers_only=True)
    migrations_after = query(args.mysql, restore_values, restore_environment, args.database, migration_sql, remaining(args))
    measurements = {"database": args.database, "source_server_uuid": source_id, "restore_server_uuid": restore_id,
                    "restore_was_empty": True, "source_data_sha256": digest(source_data), "restored_data_sha256": digest(restored_data),
                    "migration_checksums_verified": bool(migrations_before) and migrations_before == migrations_after,
                    "trigger_definitions_verified": digest(triggers) == digest(restored_triggers),
                    "triggers_installed_after_data": True,
                    "production_writes": 0, "files_or_journal_restored": False, "operator_declared_writers_stopped": True}
    evidence(args, "database_restore", started, measurements, [backup, source_data, restored_data, schema_before, schema_after, triggers, restored_triggers])
    return 0 if measurements["source_data_sha256"] == measurements["restored_data_sha256"] and measurements["migration_checksums_verified"] and measurements["trigger_definitions_verified"] else 2


def verify_scope(args, values, environment, plan):
    """开始与证据落盘前核对实际库/版本/目标；计划采用首次只读快照。"""
    identity_sql = "SELECT SHA2(CONCAT(@@server_uuid,':',DATABASE()),256)"
    if query(args.mysql, values, environment, args.database, identity_sql, remaining(args)) != plan["scope"]["database_identity"]:
        raise ValueError("actual_database_identity_mismatch")
    version_sql = "SELECT MAX(version) FROM schema_migrations"
    if query(args.mysql, values, environment, args.database, version_sql, remaining(args)) != str(plan["scope"]["schema_version"]):
        raise ValueError("actual_schema_version_mismatch")
    target_sql = "SELECT destination,target_fingerprint,target_revision FROM resource_destination_states ORDER BY destination"
    rows = [row.split("\t") for row in query(args.mysql, values, environment, args.database, target_sql, remaining(args)).splitlines()]
    if {v[0]: v[1] for v in rows} != plan["scope"]["target_fingerprints"] or {v[0]: int(v[2]) for v in rows} != plan["scope"]["target_revisions"]:
        raise ValueError("actual_target_scope_mismatch")


def main():
    """默认没有任何生产目标；输出目录必须新建，不接受凭据参数。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("benchmark", "recover", "backup-restore"))
    parser.add_argument("--database", required=True)
    parser.add_argument("--plan", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--cli", type=Path)
    parser.add_argument("--mysql", type=Path)
    parser.add_argument("--mysqldump", type=Path)
    parser.add_argument("--samples", type=int, default=30)
    parser.add_argument("--concurrency", type=int, default=1)
    parser.add_argument("--timeout-seconds", type=int, default=60)
    parser.add_argument("--total-timeout-seconds", type=int, default=600)
    parser.add_argument("--max-backup-bytes", type=int, default=20 * 1024 * 1024 * 1024)
    parser.add_argument("--destination", choices=("local", "baidu", "gdrive"), default="local")
    parser.add_argument("--stale-seconds", type=int, default=660)
    parser.add_argument("--writers-stopped", action="store_true")
    args = parser.parse_args()
    try:
        if not COMMON["DATABASE"].fullmatch(args.database) or not 1 <= args.timeout_seconds <= 3600:
            raise ValueError("isolation_or_timeout_invalid")
        if not 1 <= args.total_timeout_seconds <= 86400 or not 1 <= args.max_backup_bytes <= 1024 ** 5:
            raise ValueError("total_budget_invalid")
        args.deadline = time.monotonic() + args.total_timeout_seconds
        plan = COMMON["read_json"](args.plan)
        args.scope_hash = COMMON["validate_plan"](plan)
        if args.command != "backup-restore" and not args.cli:
            raise ValueError("cli_required")
        if args.cli and digest(args.cli) != plan["scope"]["cli_sha256"]:
            raise ValueError("actual_cli_build_mismatch")
        values, environment = private_environment("CRAWLER_RELEASE_SOURCE_MYSQL_")
        if not args.mysql:
            raise ValueError("mysql_required_for_scope_fence")
        args.verify_scope = lambda: verify_scope(args, values, environment, plan)
        args.verify_scope()
        args.output_dir.mkdir()
        started = dt.datetime.now(dt.timezone.utc).isoformat()
        code = restore(args, values, environment, started) if args.command == "backup-restore" else recover(args, environment, started) if args.command == "recover" else benchmark(args, environment, started)
        print(json.dumps({"completed": code == 0, "command": args.command, "production_writes": 0,
                          "cloud_requests": 0, "release_authorized": False}))
        return code
    except (ValueError, KeyError, TypeError, OSError, OverflowError, IndexError):
        print("FAIL: isolated rehearsal scope, dependency or exclusive output check failed", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
