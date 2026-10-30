#!/usr/bin/env python3
"""正式目标核验及小文件真实三路验收；仅写全新独占前缀，不访问音乐源。"""
import argparse
import base64
import datetime as dt
import hashlib
import http.client
import json
import os
from pathlib import Path
import re
import secrets
import shutil
import socket
import subprocess
import time
from urllib.parse import urlsplit


def timestamp():
    """证据使用带时区的真实时间。"""
    return dt.datetime.now(dt.timezone.utc).isoformat()


def write_json(path, value):
    """独占新文件，拒绝覆盖先前现场。"""
    with path.open("x", encoding="utf-8", newline="\n") as stream:
        json.dump(value, stream, ensure_ascii=False, indent=2)
        stream.write("\n")


def sha256(path):
    """完整文件摘要使用固定缓冲。"""
    value = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(65536):
            value.update(block)
    return value.hexdigest()


def read_environment(path):
    """只读指定 dotenv 的 RC 项，不把其他平台认证传播到探针。"""
    values = {}
    if path:
        for line in path.read_text(encoding="utf-8-sig").splitlines():
            if "=" not in line or line.lstrip().startswith("#"):
                continue
            key, value = line.split("=", 1)
            key = key.strip()
            if key.startswith("CRAWLER_RCLONE_") or "_RCLONE_" in key:
                values[key] = value.strip().strip('"').strip("'")
    values.update({key: value for key, value in os.environ.items()
                   if key.startswith("CRAWLER_RCLONE_") or "_RCLONE_" in key})
    return values


def rpc(settings, endpoint, body):
    """禁用系统代理与重定向，响应有界，错误不携带原始 URL 或正文。"""
    target = urlsplit(settings["RC_URL"])
    if target.scheme not in ("http", "https") or target.username or target.password or target.query or target.fragment:
        raise ValueError("invalid_rc_configuration")
    connection_type = http.client.HTTPSConnection if target.scheme == "https" else http.client.HTTPConnection
    connection = connection_type(target.hostname, target.port, timeout=120 if endpoint == "operations/hashsumfile" else 10)
    credentials = base64.b64encode((settings["RC_USER"] + ":" + settings["RC_PASSWORD"]).encode()).decode()
    try:
        connection.request("POST", target.path.rstrip("/") + "/" + endpoint, json.dumps(body).encode(),
                           {"Content-Type": "application/json", "Authorization": "Basic " + credentials})
        response = connection.getresponse()
        raw = response.read(1048577)
        if not 200 <= response.status < 300:
            raise ValueError("rc_http_status_" + str(response.status))
        if len(raw) > 1048576:
            raise ValueError("rc_response_size_limit")
        value = json.loads(raw)
        if not isinstance(value, dict) or value.get("error"):
            raise ValueError("rc_response_failed")
        return value
    finally:
        connection.close()


def invoke(probe, mode, work, environment, log):
    """持有本轮子进程句柄，超时只终止自己的探针，原始日志仅存私有目录。"""
    with log.open("xb") as stream:
        process = subprocess.Popen([str(probe), mode, str(work)], env=environment,
                                   stdin=subprocess.DEVNULL, stdout=stream, stderr=subprocess.STDOUT,
                                   creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        try:
            code = process.wait(timeout=240)
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=10)
    if log.stat().st_size > 8 * 1024 * 1024:
        raise ValueError("native_log_size_limit")
    value = json.loads(log.read_text(encoding="utf-8").splitlines()[-1])
    value["native_exit_code"] = code
    value["passed"] = value.get("passed", False) and code == 0
    return value


def main():
    """先正式只读核验，两端都可用后才允许创建独占前缀和执行真实写入。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--probe", type=Path, required=True)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--private-dir", type=Path, required=True)
    parser.add_argument("--env-file", type=Path)
    parser.add_argument("--rclone", type=Path)
    parser.add_argument("--rclone-config", type=Path)
    parser.add_argument("--baidu-fs", default="baidu:")
    parser.add_argument("--gdrive-fs", default="gdrive:")
    parser.add_argument("--allow-cloud-write", action="store_true")
    parser.add_argument("--only-gdrive", action="store_true", help="仅测试local+Google，百度明确未测试，不能作为三路通过")
    args = parser.parse_args()
    result = {"format": "crawler.resource-cloud-live-rehearsal.v1", "started_at": timestamp(),
              "execution": "actual_factory_io", "release_scope_bound": False, "release_authorized": False,
              "service_workers_tested": False, "database_connected": False, "music_source_requests": 0,
              "remote_deletes": 0, "production_configuration_modified": False, "passed": False}
    result.update(cloud_directories_created=0, cloud_upload_rounds_started=0)
    result.update(enabled_destinations=["local", "gdrive"] if args.only_gdrive else ["local", "baidu", "gdrive"],
                  baidu_tested=not args.only_gdrive, three_way_passed=False)
    process, native_log, output_created = None, None, False
    try:
        if not args.probe.is_file() or bool(args.rclone) != bool(args.rclone_config):
            raise ValueError("explicit_probe_and_optional_rclone_pair_required")
        if args.output_dir.exists() or args.private_dir.exists() or args.output_dir.resolve() == args.private_dir.resolve():
            raise ValueError("new_distinct_evidence_directories_required")
        for fs in (args.baidu_fs, args.gdrive_fs):
            if not re.fullmatch(r"[A-Za-z0-9_.-]+:(?:[A-Za-z0-9_./-]+)?", fs) or ".." in fs.split(":", 1)[1].split("/"):
                raise ValueError("invalid_explicit_remote_root")
        args.private_dir.mkdir(parents=True)
        # 私有样本、认证副本及native日志只允许当前用户和SYSTEM访问。
        if os.name == "nt":
            whoami = subprocess.check_output(["whoami", "/user", "/fo", "csv", "/nh"], creationflags=subprocess.CREATE_NO_WINDOW)
            user = re.search(rb"S-1-5-[0-9-]+", whoami).group().decode("ascii")
            subprocess.run(["icacls", str(args.private_dir), "/inheritance:r", "/grant:r", "*" + user + ":(OI)(CI)F", "*S-1-5-18:(OI)(CI)F"],
                           check=True, stdin=subprocess.DEVNULL, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL,
                           creationflags=subprocess.CREATE_NO_WINDOW)
        else:
            args.private_dir.chmod(0o700)
        args.output_dir.mkdir(parents=True)
        output_created = True
        result["probe_sha256"] = sha256(args.probe)
        result["stage"] = "configuration"
        values = read_environment(args.env_file)
        settings = {}
        for name, fs in (("BAIDU", args.baidu_fs), ("GDRIVE", args.gdrive_fs)):
            settings[name] = {key: values.get("CRAWLER_RESOURCE_" + name + "_RCLONE_" + key,
                                             values.get("CRAWLER_RCLONE_" + key, ""))
                              for key in ("RC_URL", "RC_USER", "RC_PASSWORD")}
            settings[name]["TARGET_FS"] = fs
        if args.rclone:
            result["stage"] = "owned_rc_start"
            if not args.rclone.is_file() or not args.rclone_config.is_file():
                raise ValueError("rclone_binary_or_configuration_missing")
            result["rclone_sha256"] = sha256(args.rclone)
            config_copy = args.private_dir / "rclone.conf"
            shutil.copyfile(args.rclone_config, config_copy)
            result["source_rclone_config_sha256_before"] = sha256(args.rclone_config)
            with socket.socket() as listener:
                listener.bind(("127.0.0.1", 0))
                port = listener.getsockname()[1]
            user, password = "crawler-live-" + secrets.token_hex(8), secrets.token_urlsafe(32)
            child_env = {key: value for key, value in os.environ.items() if not key.startswith("RCLONE_")}
            child_env.update(RCLONE_RC_USER=user, RCLONE_RC_PASS=password)
            native_log = (args.private_dir / "rclone-native.log").open("xb")
            process = subprocess.Popen([str(args.rclone), "rcd", "--config", str(config_copy),
                                        "--rc-addr", "127.0.0.1:" + str(port), "--log-level", "ERROR"],
                                       env=child_env, stdin=subprocess.DEVNULL, stdout=native_log, stderr=subprocess.STDOUT,
                                       creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
            for config in settings.values():
                config.update(RC_URL="http://127.0.0.1:" + str(port), RC_USER=user, RC_PASSWORD=password)
            deadline = time.monotonic() + 15
            while True:
                try:
                    result["rclone_version"] = rpc(settings["BAIDU"], "core/version", {}).get("version")
                    break
                except (OSError, ValueError):
                    if process.poll() is not None or time.monotonic() >= deadline:
                        raise ValueError("owned_rc_start_failed")
                    time.sleep(0.1)
        environment = dict(os.environ, CRAWLER_ENV_FILE=str(args.private_dir / "no-dotenv.env"))

        def work_for(mode):
            """每次执行都有独立本地根及三个不共享路径的完整样本。"""
            work = args.private_dir / mode
            work.mkdir()
            (work / "local").mkdir()
            if mode not in ("verify", "verify-gdrive"):
                block = secrets.token_bytes(65536)
                for index in range(3):
                    with (work / ("source-" + str(index) + ".bin")).open("xb") as stream:
                        for _ in range(8):
                            stream.write(block)
            return work

        def update_settings():
            """敏感RC认证仅进入子进程环境。"""
            for name, config in settings.items():
                for key, value in config.items():
                    environment["CRAWLER_CLOUD_LIVE_" + name + "_" + key] = value

        update_settings()
        result["stage"] = "read_only_target_verification"
        verify_mode = "verify-gdrive" if args.only_gdrive else "verify"
        verified = invoke(args.probe, verify_mode, work_for(verify_mode), environment, args.private_dir / "verify.log")
        write_json(args.output_dir / "target-verification.json", verified)
        if not verified.get("passed"):
            raise ValueError("real_target_identity_unavailable")
        if not args.allow_cloud_write:
            result.update(passed=True, stage="read_only_verification", cloud_writes=0)
        else:
            result["stage"] = "exclusive_prefix_preflight"
            prefix = "crawler-cloud-live-" + dt.datetime.now(dt.timezone.utc).strftime("%Y%m%dT%H%M%SZ-") + secrets.token_hex(12)
            result["exclusive_prefix"] = prefix
            selected = ("GDRIVE",) if args.only_gdrive else ("BAIDU", "GDRIVE")
            for name, config in settings.items():
                if name not in selected:
                    continue
                result["stage"] = "exclusive_prefix_stat_" + name.lower()
                if rpc(config, "operations/stat", {"fs": config["TARGET_FS"], "remote": prefix}).get("item", "missing") is not None:
                    raise ValueError("exclusive_cloud_prefix_not_absent")
            for name, config in settings.items():
                if name not in selected:
                    continue
                result["stage"] = "exclusive_prefix_mkdir_" + name.lower()
                rpc(config, "operations/mkdir", {"fs": config["TARGET_FS"], "remote": prefix})
                result["cloud_directories_created"] += 1
                config["TARGET_FS"] = config["TARGET_FS"].rstrip("/") + ("" if config["TARGET_FS"].endswith(":") else "/") + prefix
            update_settings()
            rounds = []
            modes = ("gdrive-only", "cancel-gdrive-only") if args.only_gdrive else ("parallel", "cancel-baidu", "cancel-gdrive")
            for mode in modes:
                result["stage"] = mode
                work = work_for(mode)
                result["cloud_upload_rounds_started"] += 1
                round_result = invoke(args.probe, mode, work, environment, args.private_dir / (mode + ".log"))
                if mode in ("parallel", "gdrive-only") and len(round_result.get("lanes", [])) == 3:
                    local_finished = round_result["lanes"][0].get("finished_us", -1)
                    completed_before_local = local_finished > 0 and all(0 < lane.get("finished_us", -1) < local_finished
                                                                      for lane in round_result["lanes"][1:] if lane.get("tested", True))
                    round_result["actual_cloud_writes_completed_before_local"] = completed_before_local
                    # 完成先后只作观察；硬条件仍为独立开始、最终发布和完整 SHA 回读。
                round_result["independent_full_readbacks"] = []
                for index, lane in enumerate(round_result.get("lanes", [])):
                    if not lane.get("passed"):
                        continue
                    attempt = json.loads((work / "attempts" / (str(index) + ".json")).read_text(encoding="utf-8"))
                    observed = {"destination": lane["destination"], "started_at": timestamp()}
                    if index == 0:
                        file = (work / "local" / attempt["path"]).resolve()
                        if not file.is_relative_to((work / "local").resolve()) or file.is_symlink():
                            raise ValueError("local_attempt_path_invalid")
                        digest = sha256(file)
                    else:
                        config = settings["BAIDU" if index == 1 else "GDRIVE"]
                        prefix = config["TARGET_FS"] + "/"
                        if not attempt["path"].startswith(prefix):
                            raise ValueError("foreign_attempt_path")
                        value = rpc(config, "operations/hashsumfile", {"fs": config["TARGET_FS"],
                                    "remote": attempt["path"][len(prefix):], "hashType": "SHA-256", "download": True, "base64": False})
                        if str(value.get("hashType", "")).lower() not in ("sha256", "sha-256"):
                            raise ValueError("readback_algorithm_invalid")
                        digest = value.get("hash", "").lower()
                    observed.update(sha256=digest, size_bytes=attempt["size_bytes"], actual_full_readback=True,
                                    matched=digest == attempt["sha256"], finished_at=timestamp())
                    round_result["independent_full_readbacks"].append(observed)
                    if not observed["matched"]:
                        round_result["passed"] = False
                write_json(args.output_dir / (mode + ".json"), round_result)
                rounds.append(round_result)
                if not round_result.get("passed"):
                    raise ValueError("actual_cloud_round_failed")
            result.update(passed=True, stage="gdrive_only_factory_and_cancellation" if args.only_gdrive else "three_way_and_independent_cancellation", rounds=len(rounds),
                          all_sha256_verified=True, stop_confirmed=False, uncertain_objects_retained=True)
            result["three_way_passed"] = not args.only_gdrive
        if args.rclone:
            result["source_rclone_config_unchanged"] = result["source_rclone_config_sha256_before"] == sha256(args.rclone_config)
            if not result["source_rclone_config_unchanged"]:
                raise ValueError("source_configuration_changed")
    except (ValueError, OSError, KeyError, TypeError, subprocess.SubprocessError) as error:
        code = str(error) if isinstance(error, ValueError) and re.fullmatch(r"[a-z0-9_]+", str(error)) else "real_cloud_rehearsal_failed"
        result.update(passed=False, result_code=code)
    finally:
        if process is not None:
            if process.poll() is None:
                process.terminate()
                try:
                    process.wait(timeout=10)
                except subprocess.TimeoutExpired:
                    process.kill()
                    process.wait(timeout=10)
            result["owned_rc_stopped"] = process.poll() is not None
            if result.get("passed") and args.allow_cloud_write:
                result["stop_confirmed"] = result["owned_rc_stopped"]
        if native_log:
            native_log.close()
    result["finished_at"] = timestamp()
    if output_created:
        write_json(args.output_dir / "cloud-live-result.json", result)
    print(json.dumps(result, ensure_ascii=False))
    return 0 if result["passed"] else 2


if __name__ == "__main__":
    raise SystemExit(main())
