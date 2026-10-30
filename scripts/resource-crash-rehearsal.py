#!/usr/bin/env python3
"""两真实进程强杀的隔离MySQL/文件fixture演练；不访问音乐源或网盘。"""
import argparse
import datetime as dt
import hashlib
import json
import os
from pathlib import Path
import re
import runpy
import subprocess
import time

TOOLS = runpy.run_path(str(Path(__file__).with_name("resource-release-rehearsal.py")))


def sha256(path):
    """流式核对本轮对象和探针二进制，不读取生产文件。"""
    value = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(65536):
            value.update(block)
    return value.hexdigest()


def main():
    """仅使用显式隔离库和全新输出目录；持有的Popen句柄限定终止对象。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--database", required=True)
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    args = parser.parse_args()
    processes, streams, created = [], [], False
    started = dt.datetime.now(dt.timezone.utc).isoformat()
    result = {"format": "crawler.resource-crash-rehearsal.v1", "execution": "actual_processes_and_mysql_fixture",
              "release_scope_bound": False, "release_authorized": False, "production_writes": 0,
              "cloud_requests": 0, "music_source_requests": 0, "synthetic_targets": True,
              "service_workers_tested": False, "started_at": started}
    try:
        if not re.fullmatch(r"crawler_ownership_test_crash_[a-z0-9_]{1,24}", args.database) or not args.probe.is_file():
            raise ValueError("explicit_isolated_crash_database_and_probe_required")
        _, environment = TOOLS["private_environment"]("CRAWLER_RELEASE_SOURCE_MYSQL_")
        args.output_dir.mkdir()
        created = True
        root = args.output_dir.resolve()
        environment.update({"CRAWLER_MYSQL_DATABASE": args.database, "CRAWLER_ENV_FILE": str(root/"no-dotenv.env"),
                            "CRAWLER_TEST_MYSQL_TIMEOUT_SECONDS": "120", "CRAWLER_MYSQL_IO_THREADS": "2", "CRAWLER_MYSQL_MAX_POOL_SIZE": "4"})
        result.update(database=args.database, probe_sha256=sha256(args.probe))

        def invoke(mode, role=None):
            """原始日志独占保存，只解析末尾有界JSON。"""
            command = [str(args.probe), mode] + ([str(root), role] if role else [])
            result["stage"] = mode + ("-"+role if role else "")
            env = dict(environment, CRAWLER_MYSQL_INIT_SCHEMA="true" if mode == "setup" else "false")
            with (root/(mode + ("-"+role if role else "")+".log")).open("xb") as log:
                TOOLS["run"](command, env, 120, output=log)
            rows = Path(log.name).read_bytes()
            return json.loads(rows.decode().splitlines()[-1])

        if invoke("setup").get("seeded") != 2:
            raise ValueError("seed_count_mismatch")
        for role in ("one", "two"):
            log = (root/("hold-"+role+".log")).open("xb"); streams.append(log)
            processes.append(subprocess.Popen([str(args.probe), "hold", str(root), role], env=environment,
                                              stdin=subprocess.DEVNULL, stdout=log, stderr=subprocess.STDOUT,
                                              creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0))
        result["stage"] = "publication_barrier"
        deadline = time.monotonic()+20
        while not all((root/(role+".json")).exists() for role in ("one", "two")):
            if time.monotonic() >= deadline or any(p.poll() is not None for p in processes):
                raise ValueError("workers_did_not_reach_publication_barrier")
            time.sleep(.05)
        held = [TOOLS["COMMON"]["read_json"](root/(role+".json")) for role in ("one", "two")]
        if len({r["task_id"] for r in held}) != 2:
            raise ValueError("duplicate_claim")
        snapshots = []
        for record in held:
            path = Path(record["path"]).resolve()
            if path.parent != root or path.is_symlink() or path.stat().st_size != 3 or sha256(path) != "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad":
                raise ValueError("fixture_object_mismatch")
            snapshots.append({"path": path.name, "sha256": sha256(path), "mtime_ns": path.stat().st_mtime_ns})
        if any(p.poll() is not None for p in processes):
            raise ValueError("worker_exited_before_forced_termination")
        killed_at = time.monotonic()
        for process in processes:
            process.kill(); process.wait(timeout=10)
        result.update(instances=2, claimed_tasks=2, forced_termination=True,
                      terminated_processes=[{"pid": p.pid, "exit_code": p.returncode} for p in processes])
        time.sleep(2)  # 使用真实租约时间，不在库中伪造claimed_at。
        result["recovery"] = invoke("recover")
        result["repairs"] = [invoke("repair", role) for role in ("one", "two")]
        if result["recovery"].get("recovered") != 2 or result["repairs"][-1].get("remaining_slots") != 0:
            raise ValueError("recovery_or_slot_mismatch")
        for record in snapshots:
            path = root/record["path"]
            if sha256(path) != record["sha256"] or path.stat().st_mtime_ns != record["mtime_ns"]:
                raise ValueError("uncertain_object_was_overwritten")
        if len(list(root.glob("*.object"))) != 2:
            raise ValueError("unexpected_publication")
        result.update(exit_code=0, stage="completed", duplicate_publications=0, stale_commits=0, uncertain_overwrites=0,
                      post_recovery_completed=2, recovery_seconds=round(time.monotonic()-killed_at, 3), objects=snapshots)
    except (ValueError, OSError, KeyError, TypeError, subprocess.SubprocessError):
        result.update(exit_code=1, result_code="isolated_crash_rehearsal_failed")
    finally:
        for process in processes:
            if process.poll() is None:
                process.kill(); process.wait(timeout=10)
        for stream in streams:
            stream.close()
    result["finished_at"] = dt.datetime.now(dt.timezone.utc).isoformat()
    if created:
        TOOLS["COMMON"]["write_json"](args.output_dir/"crash-result.json", result)
    print(json.dumps(result, ensure_ascii=False))
    return result["exit_code"]


if __name__ == "__main__":
    raise SystemExit(main())
