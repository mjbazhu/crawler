#!/usr/bin/env python3
"""正式demo双实例、本机HTTP内容与独立MySQL的强杀/恢复演练。"""
import argparse
import datetime as dt
import hashlib
import http.server
import json
import os
from pathlib import Path
import re
import runpy
import socket
import struct
import subprocess
import threading
import time
import urllib.error
import urllib.request
import zlib

TOOLS = runpy.run_path(str(Path(__file__).with_name("resource-release-rehearsal.py")))


def digest(path):
    """流式核对本轮对象和二进制。"""
    value = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(65536):
            value.update(block)
    return value.hexdigest()


def png(index):
    """生成有有效CRC和完整IEND的独立PNG，不读取用户或音乐平台内容。"""
    def chunk(kind, data):
        return struct.pack(">I", len(data)) + kind + data + struct.pack(">I", zlib.crc32(kind + data) & 0xffffffff)
    raw = b"".join(b"\0" + bytes((index * 80, row * 7, 60, 255)) * 32 for row in range(32))
    return (b"\x89PNG\r\n\x1a\n" + chunk(b"IHDR", struct.pack(">IIBBBBB", 32, 32, 8, 6, 0, 0, 0))
            + chunk(b"IDAT", zlib.compress(raw)) + chunk(b"IEND", b""))


def free_port():
    """仅为本轮loopback服务选取临时端口。"""
    with socket.socket() as listener:
        listener.bind(("127.0.0.1", 0))
        return listener.getsockname()[1]


def api(port, path, method="GET", body=None):
    """所有控制请求固定发往loopback，拒绝代理及HTTP跳转。"""
    class NoRedirect(urllib.request.HTTPRedirectHandler):
        def redirect_request(self, request, fp, code, message, headers, target):
            return None
    request = urllib.request.Request("http://127.0.0.1:" + str(port) + path,
                                     data=None if body is None else json.dumps(body).encode(), method=method,
                                     headers={"Content-Type": "application/json"})
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({}), NoRedirect())
    with opener.open(request, timeout=3) as response:
        data = response.read(262145)
        if len(data) > 262144:
            raise ValueError("api_response_unbounded")
        return response.status, json.loads(data)


def main():
    """父进程持有所有demo/fixture句柄，异常和完成均精确清理本轮子进程。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--database", required=True)
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--demo", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--io-root", type=Path, help="可选全新短目录，避免Windows临时文件路径过长；日志仍写output-dir")
    args = parser.parse_args()
    # 正式SQL/migration使用仓库相对路径，固定工作目录避免用户从其他目录误读配置。
    args.probe = args.probe.resolve()
    args.demo = args.demo.resolve()
    args.output_dir = args.output_dir.absolute()
    if args.io_root:
        args.io_root = args.io_root.absolute()
    os.chdir(Path(__file__).resolve().parent.parent)
    children, streams, barriers = [], [], []
    server, server_thread, created = None, None, False
    received = [threading.Event(), threading.Event()]
    release = [threading.Event(), threading.Event()]
    requests, served = [], []
    result = {"format": "crawler.resource-service-rehearsal.v1", "execution": "actual_demo_processes_mysql_loopback_http",
              "started_at": dt.datetime.now(dt.timezone.utc).isoformat(), "release_scope_bound": False,
              "release_authorized": False, "production_writes": 0, "music_source_requests": 0, "cloud_requests": 0,
              "service_workers_tested": False, "fixture_baseline": True, "real_cloud_tested": False}
    try:
        if not re.fullmatch(r"crawler_ownership_test_service_[a-z0-9_]{1,24}", args.database):
            raise ValueError("isolated_service_database_required")
        if not args.probe.is_file() or not args.demo.is_file():
            raise ValueError("probe_and_demo_required")
        connection, environment = TOOLS["private_environment"]("CRAWLER_RELEASE_SOURCE_MYSQL_")
        if connection["HOST"] != "127.0.0.1":
            raise ValueError("loopback_mysql_required")
        args.output_dir.mkdir()
        created = True
        root = args.output_dir.resolve()
        io_root = root
        if args.io_root:
            args.io_root.mkdir()
            io_root = args.io_root.resolve()
        storage = io_root / "s"
        storage.mkdir()
        environment.update({"CRAWLER_MYSQL_DATABASE": args.database, "CRAWLER_ENV_FILE": str(root / "no-dotenv.env"),
                            "CRAWLER_TEST_MYSQL_TIMEOUT_SECONDS": "150", "CRAWLER_MYSQL_IO_THREADS": "2",
                            "CRAWLER_MYSQL_MAX_POOL_SIZE": "6", "CRAWLER_MYSQL_INIT_SCHEMA": "false",
                            "CRAWLER_LOG_TEXT_FILE_ENABLED": "false", "CRAWLER_LOG_JSON_FILE_ENABLED": "false",
                            "CRAWLER_HOST": "127.0.0.1", "CRAWLER_PERSISTENCE_BACKEND": "mysql",
                            "CRAWLER_STORAGE_PATH": str(storage), "CRAWLER_RESOURCE_STORAGE_BACKEND": "filesystem",
                            "CRAWLER_RESOURCE_PIPELINE_MODE": "parallel", "CRAWLER_RESOURCE_LOCAL_ENABLED": "true",
                            "CRAWLER_RESOURCE_CLOUD_PROVIDERS": "", "CRAWLER_RESOURCE_CLOUD_UPLOAD_ENABLED": "false",
                            "CRAWLER_RESOURCE_LOCAL_CONCURRENCY": "1", "CRAWLER_RESOURCE_LOCAL_TIMEOUT_MS": "120000",
                            "CRAWLER_RESOURCE_LOCAL_RETRIES": "0", "CRAWLER_RESOURCE_LOCAL_MAX_BYTES": "1048576",
                            "CRAWLER_RESOURCE_LOCAL_STAGING_PATH": str(io_root / "t"),
                            "CRAWLER_RESOURCE_LOCAL_STAGING_MAX_BYTES": "2097152",
                            "CRAWLER_RESOURCE_SOURCE_CONCURRENCY": "1", "CRAWLER_RESOURCE_TRANSFER_CONCURRENCY": "1",
                            "CRAWLER_RESOURCE_SHUTDOWN_GRACE_MS": "1000", "CRAWLER_RESOURCE_PRODUCER_ENABLED": "false",
                            "CRAWLER_RESOURCE_LEGACY_VIEW_PROVIDER": "", "CRAWLER_OUTBOUND_HTTP_IO_THREADS": "1",
                            "CRAWLER_HTTP_WORKER_THREADS": "2", "CRAWLER_ASYNC_WORKER_THREADS": "2",
                            "CRAWLER_RESOURCE_WORKER_THREADS": "1", "CRAWLER_PARSE_WORKER_THREADS": "1",
                            "CRAWLER_PERSISTENCE_WORKER_THREADS": "1"})
        result.update(database=args.database, demo_sha256=digest(args.demo), probe_sha256=digest(args.probe),
                      local_root=str(storage), io_root=str(io_root), content_source="generated_loopback_png", producer_enabled=False)
        sequence = 0

        def invoke(mode, *arguments):
            """有界运行观测probe，每份日志仅创建一次，不替换中间失败现场。"""
            nonlocal sequence
            sequence += 1
            log_path = root / (f"probe-{sequence:03d}-" + mode + ".log")
            with log_path.open("xb") as log:
                TOOLS["run"]([str(args.probe), mode, *map(str, arguments)], environment, 150, output=log)
            return json.loads(log_path.read_text(encoding="utf-8").splitlines()[-1])

        def launch(executable, arguments, label, env=None, pipe=False):
            """每个native进程都保留Popen句柄和独占日志。"""
            log = (root / (label + ".log")).open("xb")
            streams.append(log)
            process = subprocess.Popen([str(executable), *map(str, arguments)], env=env or environment,
                                       stdin=subprocess.PIPE if pipe else subprocess.DEVNULL,
                                       stdout=log, stderr=subprocess.STDOUT,
                                       creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
            children.append(process)
            return process, Path(log.name)

        def wait_for(predicate, code, seconds=30):
            """有限等待；上层阶段名称保留失败位置。"""
            deadline = time.monotonic() + seconds
            while not predicate():
                if time.monotonic() >= deadline:
                    raise ValueError(code)
                time.sleep(.1)

        class Content(http.server.BaseHTTPRequestHandler):
            """先挂住两个实际worker的来源，再按父控释放，隔离DB全局目标行锁。"""
            def do_GET(self):
                if self.path not in ("/1.png", "/2.png"):
                    self.send_error(404)
                    return
                index = int(self.path[1]) - 1
                requests.append(index + 1)
                received[index].set()
                if not release[index].wait(90):
                    return
                body = png(index + 1)
                self.send_response(200)
                self.send_header("Content-Type", "image/png")
                self.send_header("Content-Length", str(len(body)))
                self.end_headers()
                try:
                    self.wfile.write(body)
                    served.append(index + 1)
                except OSError:
                    pass

            def log_message(self, *unused):
                pass

        server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Content)
        server.daemon_threads = True
        server_thread = threading.Thread(target=server.serve_forever, daemon=True)
        server_thread.start()
        result["stage"] = "setup"
        seeded = invoke("setup", storage, "http://127.0.0.1:" + str(server.server_port))
        ids = seeded["resource_ids"]
        if seeded["seeded"] != 2 or len(set(ids)) != 2:
            raise ValueError("unexpected_fixture_scope")
        result["setup"] = seeded
        for index, resource in enumerate(ids):
            barrier, log = launch(args.probe, ["barrier", resource], "barrier-" + str(index + 1), pipe=True)
            barriers.append(barrier)
            wait_for(lambda: b'"barrier_ready"' in log.read_bytes() or barrier.poll() is not None, "barrier_not_ready")
            if barrier.poll() is not None:
                raise ValueError("barrier_failed")

        ports = [free_port(), free_port()]
        while ports[1] == ports[0]:
            ports[1] = free_port()
        demos, accepted = [], []

        def start_demo(index, label):
            """核对真实parallel与单local目标；cover路径不装载歌曲来源或刷新token。"""
            process, unused = launch(args.demo, [], label, dict(environment, CRAWLER_PORT=str(ports[index])))
            def ready():
                if process.poll() is not None:
                    raise ValueError("demo_exited_before_ready")
                try:
                    return api(ports[index], "/api/health")[0] == 200
                except (OSError, urllib.error.URLError):
                    return False
            wait_for(ready, "demo_not_ready")
            status, settings = api(ports[index], "/api/v2/migu/settings")
            targets = settings["readonly"]["targets"]
            if status != 200 or len(targets) != 1 or targets[0]["destination"] != "local" or not targets[0]["available"]:
                raise ValueError("unexpected_runtime_targets")
            return process

        def submit(index, action):
            """所有受理和修复均调用正式v2 HTTP，不让probe执行worker业务。"""
            status, operation = api(ports[index], "/api/v2/migu/covers/process", "POST",
                                    {"resources": [{"owner_type": "singer", "resource_id": ids[index]}],
                                     "destinations": ["local"], "action": action,
                                     "idempotency_key": "service-" + action + "-" + str(index + 1)})
            if status != 202 or len(operation["tasks"]) != 1:
                raise ValueError("service_acceptance_failed")
            return operation

        result["stage"] = "two_live_service_claims"
        for index in range(2):
            demos.append(start_demo(index, "demo-initial-" + str(index + 1)))
            accepted.append(submit(index, "process"))
            wait_for(received[index].is_set, "worker_did_not_request_loopback_content")
            if any(process.poll() is not None for process in demos):
                raise ValueError("demo_exited_during_claim")
        claims = invoke("snapshot")
        if len(claims["tasks"]) != 2 or any(task["state"] != "running" for task in claims["tasks"]):
            raise ValueError("two_service_claims_not_running")
        result.update(instances=2, claimed_tasks=2, initial_pids=[process.pid for process in demos], accepted_operations=accepted)
        published, originals, killed = [], [], []
        for index, process in enumerate(demos):
            result["stage"] = "publication_barrier_" + str(index + 1)
            release[index].set()
            last = None
            def published_barrier():
                nonlocal last
                if process.poll() is not None:
                    raise ValueError("demo_exited_before_publication")
                last = invoke("snapshot")
                return last["waiting_receipts"] == 1 and any(task["resource_id"] == ids[index] and task["attempt_path"] for task in last["tasks"])
            wait_for(published_barrier, "completed_file_barrier_not_reached")
            task = next(task for task in last["tasks"] if task["resource_id"] == ids[index])
            path = (storage / task["attempt_path"]).resolve()
            if not path.is_relative_to(storage) or path.is_symlink() or digest(path) != hashlib.sha256(png(index + 1)).hexdigest():
                raise ValueError("published_png_mismatch")
            journals = list((storage / ".resource-writes").glob("*.pending"))
            matching = [journal for journal in journals if task["attempt_path"] in journal.read_text(encoding="utf-8").splitlines()]
            if len(matching) != 1 or task["state"] != "running" or last["completed_resources"] != 0:
                raise ValueError("publication_journal_or_receipt_boundary_mismatch")
            published.append({"path": task["attempt_path"], "sha256": digest(path), "size_bytes": path.stat().st_size,
                              "mtime_ns": path.stat().st_mtime_ns, "journal": matching[0].name,
                              "journal_sha256": digest(matching[0])})
            originals.append(task)
            process.kill()
            process.wait(timeout=10)
            killed.append({"pid": process.pid, "exit_code": process.returncode, "resource_id": ids[index]})
            barriers[index].communicate(b"release\n", timeout=10)
            if barriers[index].returncode != 0:
                raise ValueError("fixture_lock_release_failed")
            wait_for(lambda: invoke("snapshot")["waiting_receipts"] == 0, "killed_connection_not_rolled_back")
        claim_file = root / "original-claims.json"
        claim_file.write_text(json.dumps(originals, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        result.update(forced_termination=True, terminated_processes=killed, objects=published)
        killed_at = time.monotonic()
        time.sleep(2)  # 真实租约时间，不修改claimed_at伪造过期。
        result["stage"] = "recover_expired_service_claims"
        result["recovery"] = invoke("recover")
        uncertain = invoke("snapshot")
        if uncertain["slots"] != 2 or any(task["state"] != "uncertain" for task in uncertain["tasks"]):
            raise ValueError("uncertain_service_objects_not_preserved")
        result["stale_before_repair"] = invoke("stale", claim_file)
        result["stage"] = "restart_two_services"
        restarted = [start_demo(index, "demo-restarted-" + str(index + 1)) for index in range(2)]
        time.sleep(.5)
        after_restart = invoke("snapshot")
        if after_restart["tasks"] != uncertain["tasks"] or requests != [1, 2]:
            raise ValueError("restart_silently_retransmitted_uncertain_object")
        result["stage"] = "explicit_http_repair"
        repairs = [submit(index, "repair_destination") for index in range(2)]
        def repaired():
            status = invoke("snapshot")
            return status["completed_resources"] == 2 and status["slots"] == 0
        wait_for(repaired, "explicit_service_repair_not_completed")
        result["stale_after_repair"] = invoke("stale", claim_file)
        result["final_snapshot"] = invoke("snapshot")
        if result["final_snapshot"].get("producer_tasks", 0) != 0:
            raise ValueError("unexpected_producer_task")
        repair_results = [api(ports[index], "/api/v2/migu/tasks/" + operation["batch_id"])[1]
                          for index, operation in enumerate(repairs)]
        if any(operation["state"] != "completed" or operation["tasks"][0]["state"] != "succeeded" for operation in repair_results):
            raise ValueError("repair_operation_not_successful")
        for record in published:
            path = storage / record["path"]
            if digest(path) != record["sha256"] or path.stat().st_mtime_ns != record["mtime_ns"]:
                raise ValueError("uncertain_file_was_overwritten")
            journal = storage / ".resource-writes" / record["journal"]
            if not journal.is_file() or digest(journal) != record["journal_sha256"]:
                raise ValueError("unconfirmed_service_journal_changed")
        if requests != [1, 2] or served != [1, 2] or len(list((storage / "resource-data").rglob("*.png"))) != 2:
            raise ValueError("unexpected_content_request_or_publication")
        result.update(exit_code=0, stage="completed", service_workers_tested=True, restarted_instances=2,
                      restart_pids=[process.pid for process in restarted], explicit_repair_operations=repairs,
                      completed_repair_operations=repair_results,
                      uncertain_slots_preserved=2, post_recovery_completed=2, stale_commits=0,
                      duplicate_publications=0, uncertain_overwrites=0, content_http_requests=2,
                      recovery_seconds=round(time.monotonic() - killed_at, 3), token_refresh_applicable=False,
                      token_refresh_scope="cover_only_source_no_song_or_token_requests",
                      journals_remaining=len(list((storage / ".resource-writes").glob("*.pending"))),
                      journal_cleanup_tested=False)
    except (ValueError, OSError, KeyError, TypeError, StopIteration, subprocess.SubprocessError) as error:
        result.update(exit_code=1, result_code="isolated_service_rehearsal_failed", failure_type=type(error).__name__)
        if isinstance(error, ValueError) and re.fullmatch(r"[a-z0-9_]{1,96}", str(error)):
            result["failure_code"] = str(error)
    finally:
        for event in release:
            event.set()
        cleanup = []
        for process in children:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=10)
                cleanup.append(process.pid)
        result["cleanup_terminated_pids"] = cleanup
        result["all_owned_processes_stopped"] = all(process.poll() is not None for process in children)
        for stream in streams:
            stream.close()
        if server:
            server.shutdown()
            server.server_close()
        if server_thread:
            server_thread.join(timeout=3)
    result["finished_at"] = dt.datetime.now(dt.timezone.utc).isoformat()
    if created:
        TOOLS["COMMON"]["write_json"](args.output_dir / "service-result.json", result)
    print(json.dumps(result, ensure_ascii=False))
    return result["exit_code"]


if __name__ == "__main__":
    raise SystemExit(main())
