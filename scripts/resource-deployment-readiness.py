#!/usr/bin/env python3
"""部署配置、脱敏库存及调用方声明检查；不迁移、启停服务或授权发布。"""
import argparse
import base64
import datetime as dt
import hashlib
import http.client
import json
import os
from pathlib import Path
import re
import runpy
import subprocess
import sys
import tempfile
import time
from urllib.parse import urlsplit

ROOT = Path(__file__).resolve().parent.parent
RELEASE = runpy.run_path(str(Path(__file__).with_name("resource-release-check.py")))
HEX = re.compile(r"[0-9a-f]{64}\Z")
ROUTE = re.compile(r"/api/migu/(?:resources/(?:process|retry|reset|recover-stale)|covers/(?:process|recover-stale)|maintenance/recover-stale|(?:metadata|backfill)/[A-Za-z0-9_/-]+|(?:singers|songs|albums|singer-songs|singer-albums|album-details|album-songs)/[A-Za-z0-9_/-]+)")
UNSUPPORTED = ("/maintenance/recover-stale", "/metadata/", "/backfill/")


def now():
    """记录带时区的本轮观测时刻。"""
    return dt.datetime.now(dt.timezone.utc).isoformat()


def digest(path):
    """固定缓冲计算文件摘要，不输出正文。"""
    value = hashlib.sha256()
    with path.open("rb") as stream:
        while block := stream.read(65536):
            value.update(block)
    return value.hexdigest()


def native(arguments, environment, timeout=20, limit=65536):
    """只持有本轮子进程，限制时间/输出并隐藏原始诊断。"""
    with tempfile.TemporaryFile() as captured:
        process = subprocess.Popen(arguments, env=environment, stdin=subprocess.DEVNULL,
                                   stdout=captured, stderr=subprocess.DEVNULL,
                                   creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
        deadline = time.monotonic() + timeout
        try:
            while process.poll() is None:
                if time.monotonic() >= deadline or os.fstat(captured.fileno()).st_size > limit:
                    raise ValueError("native_deadline_or_output_limit")
                time.sleep(0.02)
            if os.fstat(captured.fileno()).st_size > limit:
                raise ValueError("native_output_limit")
            captured.seek(0)
            return process.returncode, captured.read().decode("utf-8-sig")
        finally:
            if process.poll() is None:
                process.kill()
                process.wait(timeout=5)


def environment(path, inherited=None):
    """仅读显式 UTF-8 dotenv，保持首个非空值及进程非空覆盖语义。"""
    values = {}
    if path:
        if not path.is_file() or path.stat().st_size > 262144:
            raise ValueError("explicit_env_missing_or_too_large")
        raw = path.read_bytes()
        if raw.startswith(b"\xef\xbb\xbf"):
            raise ValueError("env_bom_rejected")
        for line in raw.decode("utf-8").splitlines():
            line = line.strip()
            if line.startswith("export "):
                line = line[7:].strip()
            if not line or line.startswith("#"):
                continue
            if "=" not in line:
                raise ValueError("env_syntax_invalid")
            key, value = (part.strip() for part in line.split("=", 1))
            if not re.fullmatch(r"[A-Za-z0-9_]+", key):
                raise ValueError("env_key_invalid")
            if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
                value = value[1:-1]
            if value and not values.get(key):
                values[key] = value
    source = os.environ if inherited is None else inherited
    values.update({k: v for k, v in source.items() if v and k.startswith("CRAWLER_")})
    return values


def check_configuration(values):
    """检查部署需要的显式选择，不读取历史本地文件或创建目录。"""
    errors = []
    required = ("CRAWLER_MYSQL_HOST", "CRAWLER_MYSQL_PORT", "CRAWLER_MYSQL_DATABASE",
                "CRAWLER_MYSQL_USER", "CRAWLER_MYSQL_PASSWORD", "CRAWLER_STORAGE_PATH",
                "CRAWLER_MYSQL_MIGRATIONS_PATH", "CRAWLER_RESOURCE_LEGACY_VIEW_PROVIDER")
    errors += ["missing:" + key for key in required if not values.get(key)]
    for key, expected in {"CRAWLER_PERSISTENCE_BACKEND": "mysql", "CRAWLER_RESOURCE_STORAGE_BACKEND": "filesystem",
                          "CRAWLER_RESOURCE_PIPELINE_MODE": "parallel", "CRAWLER_MYSQL_INIT_SCHEMA": "false",
                          "CRAWLER_OUTBOUND_VERIFY_TLS": "true", "CRAWLER_RESOURCE_BAIDU_ADAPTER": "rclone",
                          "CRAWLER_RESOURCE_GDRIVE_ADAPTER": "rclone"}.items():
        if values.get(key) != expected:
            errors.append("explicit_selection_required:" + key)
    if not re.fullmatch(r"[A-Za-z0-9_]{1,64}", values.get("CRAWLER_MYSQL_DATABASE", "")):
        errors.append("database_name_invalid")
    for key in ("CRAWLER_MYSQL_PORT", "CRAWLER_PORT"):
        raw = values.get(key, "")
        if not re.fullmatch(r"[0-9]{1,5}", raw) or not 1 <= int(raw) <= 65535:
            errors.append("explicit_port_invalid:" + key)
    if values.get("CRAWLER_RESOURCE_LEGACY_VIEW_PROVIDER") not in ("local", "baidu", "gdrive"):
        errors.append("fixed_legacy_view_required")
    if values.get("CRAWLER_RESOURCE_CLOUD_PROVIDERS") not in ("baidu,gdrive", "gdrive,baidu"):
        errors.append("both_cloud_providers_required")
    if values.get("CRAWLER_RESOURCE_LOCAL_ENABLED") not in ("true", "false", "1", "0"):
        errors.append("explicit_local_policy_required")
    if values.get("CRAWLER_RESOURCE_CLOUD_VERIFICATION_POLICY") not in ("hash_match", "strict_sha256"):
        errors.append("verification_policy_invalid")
    if values.get("CRAWLER_RESOURCE_PRODUCER_ENABLED") not in ("true", "false", "1", "0"):
        errors.append("explicit_producer_policy_required")
    paths = {}
    for key in ("CRAWLER_STORAGE_PATH", "CRAWLER_RESOURCE_LOCAL_STAGING_PATH",
                "CRAWLER_RESOURCE_BAIDU_STAGING_PATH", "CRAWLER_RESOURCE_GDRIVE_STAGING_PATH",
                "CRAWLER_MYSQL_MIGRATIONS_PATH"):
        value = values.get(key, "")
        path = Path(value)
        if not value or not path.is_absolute() or not path.is_dir():
            errors.append("absolute_existing_directory_required:" + key)
        else:
            paths[key] = path.resolve()
    staging = [(key, path) for key, path in paths.items() if "STAGING" in key]
    for index, (key, path) in enumerate(staging):
        for other_key, other in staging[index + 1:]:
            if path == other or path.is_relative_to(other) or other.is_relative_to(path):
                errors.append("staging_paths_overlap:" + key + ":" + other_key)
        storage = paths.get("CRAWLER_STORAGE_PATH")
        if storage and "LOCAL_STAGING" not in key and (path == storage or path.is_relative_to(storage) or storage.is_relative_to(path)):
            errors.append("cloud_staging_overlaps_storage:" + key)
    for name in ("LOCAL", "BAIDU", "GDRIVE"):
        prefix = "CRAWLER_RESOURCE_" + name + "_"
        budgets = {"CONCURRENCY": (1, 32), "TIMEOUT_MS": (100, 600000), "RETRIES": (0, 5),
                   "MAX_BYTES": (1, 4294967296), "MAX_BYTES_PER_SECOND": (0, 1099511627776),
                   "STAGING_MAX_BYTES": (1, 1099511627776)}
        numbers = {}
        for suffix, (lower, upper) in budgets.items():
            raw = values.get(prefix + suffix, "")
            if not re.fullmatch(r"[0-9]{1,20}", raw) or not lower <= int(raw) <= upper:
                errors.append("explicit_budget_invalid:" + prefix + suffix)
            else:
                numbers[suffix] = int(raw)
        if numbers.get("STAGING_MAX_BYTES", 0) < numbers.get("MAX_BYTES", 1):
            errors.append("staging_cannot_hold_one_resource:" + name)
    targets = 2 + (values.get("CRAWLER_RESOURCE_LOCAL_ENABLED") in ("true", "1"))
    for suffix, lower, upper in (("SOURCE_CONCURRENCY", targets, 32), ("TRANSFER_CONCURRENCY", targets, 96), ("SHUTDOWN_GRACE_MS", 0, 600000)):
        raw = values.get("CRAWLER_RESOURCE_" + suffix, "")
        if not re.fullmatch(r"[0-9]{1,20}", raw) or not lower <= int(raw) <= upper:
            errors.append("global_budget_invalid:" + suffix)
    for name in ("BAIDU", "GDRIVE"):
        settings = rc_settings(values, name)
        if any(not settings.get(key) for key in ("RC_URL", "RC_USER", "RC_PASSWORD", "TARGET_FS")):
            errors.append("rc_configuration_missing:" + name)
        try:
            target = urlsplit(settings["RC_URL"])
            if target.scheme not in ("http", "https") or not target.hostname or target.username or target.password or target.query or target.fragment:
                raise ValueError()
            if target.scheme == "http" and target.hostname not in ("127.0.0.1", "::1", "localhost"):
                raise ValueError()
            _ = target.port
        except ValueError:
            errors.append("authenticated_rc_transport_invalid:" + name)
    return sorted(set(errors))


def rc_settings(values, name):
    """复用 C++ 的逐端覆盖全局 RC 规则。"""
    return {key: values.get("CRAWLER_RESOURCE_" + name + "_RCLONE_" + key) or values.get("CRAWLER_RCLONE_" + key, "")
            for key in ("RC_URL", "RC_USER", "RC_PASSWORD", "TARGET_FS")}


def rc_version(settings):
    """只读 core/version，先确认匿名请求被拒，禁代理/重定向。"""
    target = urlsplit(settings["RC_URL"])
    connection_type = http.client.HTTPSConnection if target.scheme == "https" else http.client.HTTPConnection
    connection = connection_type(target.hostname, target.port, timeout=5)
    token = base64.b64encode((settings["RC_USER"] + ":" + settings["RC_PASSWORD"]).encode()).decode()
    try:
        connection.request("POST", target.path.rstrip("/") + "/core/version", b"{}", {"Content-Type": "application/json"})
        anonymous = connection.getresponse()
        anonymous.read(65537)
        if anonymous.status != 401:
            raise ValueError("rc_authentication_not_enforced")
        connection.close()
        connection.request("POST", target.path.rstrip("/") + "/core/version", b"{}",
                           {"Content-Type": "application/json", "Authorization": "Basic " + token})
        response = connection.getresponse()
        raw = response.read(65537)
        if response.status != 200 or len(raw) > 65536:
            raise ValueError("rc_version_failed")
        version = json.loads(raw).get("version", "")
        if not re.fullmatch(r"v?[0-9]+\.[0-9]+\.[0-9]+(?:[-.A-Za-z0-9]{0,40})", version):
            raise ValueError("rc_version_invalid")
        return version
    finally:
        connection.close()


def config_report(args):
    """区分离线配置与真实观察，任何未核对项均保持 unknown。"""
    values = environment(args.env_file)
    errors = check_configuration(values)
    scope = None
    scope_hash = None
    if args.release_plan:
        release = RELEASE["read_json"](args.release_plan)
        scope_hash = RELEASE["validate_plan"](release)
        scope = release["scope"]
    report = {"format": "crawler.resource-deployment-config.v1", "observed_at": now(),
              "configuration_complete": not errors, "errors": errors, "scope_sha256": scope_hash,
              "configuration_source": "explicit_env_with_process_overrides" if args.env_file else "process_environment_only",
              "database_observation": "unknown", "targets": {}, "binary_matches_scope": False,
              "production_authorized": False, "activation_performed": False,
              "historical_local_files_inspected": False, "readiness_verified": False}
    if args.cli:
        report["binary_matches_scope"] = bool(scope and args.cli.is_file() and digest(args.cli) == scope["cli_sha256"])
    if args.probe_rc and not errors:
        for name in ("BAIDU", "GDRIVE"):
            item = {"rc_reachable": False, "identity_verified": False, "matches_scope": False}
            try:
                item["rclone_version"] = rc_version(rc_settings(values, name))
                item["rc_reachable"] = True
            except (ValueError, OSError, http.client.HTTPException, json.JSONDecodeError):
                item["result_code"] = "rc_unavailable_or_invalid"
            report["targets"][name.lower()] = item
    if args.probe_targets:
        if errors or not report["binary_matches_scope"] or not args.cli:
            report["errors"].append("target_probe_requires_valid_config_and_scope_bound_cli")
        else:
            with tempfile.TemporaryDirectory(prefix="crawler-deployment-probe-") as directory:
                child = {k: v for k, v in os.environ.items() if not k.startswith("CRAWLER_")}
                child.update({k: v for k, v in values.items() if k.startswith("CRAWLER_")})
                child.update(CRAWLER_ENV_FILE=str(Path(directory) / "absent.env"), CRAWLER_LOG_CONSOLE="false",
                             CRAWLER_LOG_TEXT_FILE_ENABLED="false", CRAWLER_LOG_JSON_FILE_ENABLED="false")
                for name in ("baidu", "gdrive"):
                    item = report["targets"].setdefault(name, {"rc_reachable": False})
                    try:
                        code, output = native([str(args.cli.resolve()), "target-probe", "--destination", name], child)
                        observed = json.loads(output)
                        fingerprint = observed.get("fingerprint", "")
                        valid = code == 0 and observed.get("available") is True and HEX.fullmatch(fingerprint) is not None
                        item.update(identity_verified=valid, matches_scope=valid and fingerprint == scope["target_fingerprints"][name],
                                    identity_scope=observed.get("identity_scope") if observed.get("identity_scope") in ("webdav_mount", "provider_account") else "unknown")
                    except (ValueError, OSError, TypeError):
                        item.update(identity_verified=False, matches_scope=False, result_code="target_probe_failed")
    # 正式库版本/准入不靠本机 env 推断；认证只在本轮子进程 MYSQL_PWD 内传递。
    if args.mysql:
        if errors or not scope:
            report["errors"].append("database_probe_requires_valid_configuration_and_scope")
        else:
            sql = ("START TRANSACTION READ ONLY; SELECT SHA2(CONCAT(@@server_uuid,':',DATABASE()),256),"
                   "(SELECT COALESCE(MAX(schema_version),0) FROM crawler_schema_versions WHERE component='crawler_mysql'),"
                   "(SELECT COALESCE(MAX(version),0) FROM schema_migrations); "
                   "SELECT destination,target_fingerprint,target_revision,dispatch_state FROM resource_destination_states ORDER BY destination; COMMIT;")
            try:
                child = {k: v for k, v in os.environ.items() if not k.startswith("MYSQL")}
                # MySQL 8.0不支持--no-login-paths，改为独立不存在的login文件。
                child["MYSQL_TEST_LOGIN_FILE"] = str(Path(tempfile.gettempdir()) / ("crawler-deployment-no-login-" + os.urandom(16).hex() + ".cnf"))
                child["MYSQL_PWD"] = values["CRAWLER_MYSQL_PASSWORD"]
                code, output = native([str(args.mysql.resolve()), "--no-defaults", "--protocol=TCP",
                                       "--host=" + values["CRAWLER_MYSQL_HOST"], "--port=" + values["CRAWLER_MYSQL_PORT"],
                                       "--user=" + values["CRAWLER_MYSQL_USER"], "--connect-timeout=5",
                                       "--batch", "--skip-column-names", "--raw", "--database=" + values["CRAWLER_MYSQL_DATABASE"], "--execute=" + sql], child)
                rows = [line.split("\t") for line in output.strip().splitlines()]
                first = rows[0]
                valid = code == 0 and len(rows) == 4 and len(first) == 3 and first[0] == scope["database_identity"] and int(first[1]) == int(first[2]) == scope["schema_version"]
                targets = {row[0]: row for row in rows[1:] if len(row) == 4}
                valid = valid and set(targets) == {"local", "baidu", "gdrive"} and all(targets[name][1] == scope["target_fingerprints"][name] and int(targets[name][2]) == scope["target_revisions"][name] and targets[name][3] == "maintenance" for name in targets)
                report["database_observation"] = "scope_and_maintenance_match" if valid else "mismatch"
            except (ValueError, OSError, IndexError, TypeError):
                report["database_observation"] = "unavailable_or_invalid"
    report["configuration_complete"] = not report["errors"]
    report["readiness_verified"] = bool(not report["errors"] and report["binary_matches_scope"] and report["database_observation"] == "scope_and_maintenance_match" and all(report["targets"].get(name, {}).get("matches_scope") is True and report["targets"][name].get("rc_reachable") is True for name in ("baidu", "gdrive")))
    return report


def inventory(root):
    """只输出脱敏有界库存；本机无匹配不能证明其他机器没有 writer。"""
    result = {"format": "crawler.resource-deployment-inventory.v1", "observed_at": now(),
              "host_scan": "unknown", "host": {}, "repository_references": [], "repository_scan_complete": True,
              "external_writers": None, "writers_stopped_proven": False, "production_authorized": False}
    if os.name == "nt":
        # 原始参数仅在子进程内用于匹配，不离开 PowerShell；执行本身排除当前扫描器。
        script = r'''
$ErrorActionPreference='Stop'
function HashId($value) {
    $sha=[System.Security.Cryptography.SHA256]::Create()
    try { -join ($sha.ComputeHash([Text.Encoding]::UTF8.GetBytes([string]$value)) | ForEach-Object { $_.ToString('x2') }) }
    finally { $sha.Dispose() }
}
$pattern='(?i)(crawler|rclone|baidupcs|resource-cloud|resource-transfer|resource-release)'
$images='^(demo|rclone|BaiduPCS-Go|crawler_cloud_receipts|mysqld)[.]exe$'
$p=@(Get-CimInstance Win32_Process | Where-Object {
    $_.ProcessId -ne $PID -and $_.ProcessId -ne $env:CRAWLER_DEPLOYMENT_SCANNER_PID -and
    $_.ProcessId -ne $env:CRAWLER_DEPLOYMENT_SCANNER_PARENT_PID -and ($_.Name -match $images -or $_.CommandLine -match $pattern)
} | ForEach-Object { @{pid=[int]$_.ProcessId; image=if($_.Name -match $images){$_.Name.ToLower()}else{'other_matching_process'}; identity_sha256=(HashId ($_.ProcessId.ToString()+':'+$_.CreationDate))} })
$s=@(Get-CimInstance Win32_Service | Where-Object { $_.Name -match $pattern -or $_.PathName -match $pattern } |
    ForEach-Object { @{identity_sha256=(HashId $_.Name); state=[string]$_.State; pid=[int]$_.ProcessId} })
$t=@(Get-ScheduledTask | Where-Object {
    ($_.TaskName+' '+$_.TaskPath+' '+(($_.Actions | ForEach-Object {$_.Execute+' '+$_.Arguments}) -join ' ')) -match $pattern
} | ForEach-Object { @{identity_sha256=(HashId ($_.TaskPath+$_.TaskName)); state=[string]$_.State; action_count=@($_.Actions).Count} })
@{processes=@($p | Select-Object -First 64); process_count=$p.Count; services=@($s | Select-Object -First 64);
  service_count=$s.Count; scheduled_tasks=@($t | Select-Object -First 64); scheduled_task_count=$t.Count;
  truncated=($p.Count -gt 64 -or $s.Count -gt 64 -or $t.Count -gt 64)} | ConvertTo-Json -Depth 5 -Compress
'''
        try:
            child = dict(os.environ, CRAWLER_DEPLOYMENT_SCANNER_PID=str(os.getpid()), CRAWLER_DEPLOYMENT_SCANNER_PARENT_PID=str(os.getppid()))
            code, output = native(["powershell.exe", "-NoProfile", "-NonInteractive", "-Command", script], child, timeout=30)
            if code != 0:
                raise ValueError("host_inventory_unavailable")
            result["host"] = json.loads(output)
            result["host_scan"] = "bounded_complete" if result["host"].get("truncated") is False else "truncated"
        except (ValueError, OSError):
            result["host_scan"] = "unavailable"
    # git 只列已跟踪及未忽略文件，不读取私有 env、数据库、日志或本地构建目录。
    code, names = native(["git", "-C", str(root), "ls-files", "--cached", "--others", "--exclude-standard", "-z"], dict(os.environ), limit=2097152)
    if code:
        raise ValueError("repository_inventory_failed")
    priorities = {"scripts": 0, "config": 1, ".vscode": 2, "src": 3, "tests": 4, "docs": 5}
    candidates = sorted(set(names.split("\0")) - {""}, key=lambda name: (priorities.get(name.split("/", 1)[0], 6), name))
    count = 0
    for name in candidates:
        path = root / name
        if path.suffix.lower() not in (".py", ".ps1", ".cmd", ".bat", ".sh", ".json", ".yml", ".yaml", ".cpp", ".h", ".md") or not name.startswith(("scripts/", "config/", "src/", "tests/", "docs/", ".vscode/")):
            continue
        if path.is_symlink() or not path.is_file() or not path.resolve().is_relative_to(root.resolve()):
            result["repository_scan_complete"] = False
            continue
        count += 1
        if count > 3000 or path.stat().st_size > 2097152:
            result["repository_scan_complete"] = False
            continue
        try:
            lines = path.read_text(encoding="utf-8").splitlines()
        except (OSError, UnicodeError):
            result["repository_scan_complete"] = False
            continue
        for line_number, line in enumerate(lines, 1):
            for route in sorted(set(ROUTE.findall(line))):
                if len(result["repository_references"]) >= 256:
                    result["repository_scan_complete"] = False
                    break
                result["repository_references"].append({"path": name, "line": line_number, "route": route,
                    "classification": "implementation" if name.startswith("src/") else "test" if name.startswith("tests/") or name.endswith("-tests.py") else "documentation" if name.startswith("docs/") or ".example" in name else "potential_caller"})
    result["repository_files_examined"] = min(count, 3000)
    result["repository_references_are_deployed_callers"] = False
    return result


def declarations(plan, release, directory):
    """只校验明确的人工作业声明及证据一致性，不自动产生 actual 发布证据。"""
    scope_hash = RELEASE["validate_plan"](release)
    errors = []
    if plan.get("format") != "crawler.resource-deployment-plan.v1" or plan.get("release_scope_sha256") != scope_hash:
        errors.append("deployment_scope_mismatch")
    if plan.get("initial_mode") != "maintenance" or plan.get("history_local_files") != "preserve_paths_only" or release["scope"].get("history_local_files", "verify") != plan.get("history_local_files"):
        errors.append("deployment_boundary_invalid")
    errors.extend(RELEASE["artifact_errors"](plan, directory, time.monotonic() + 30))
    artifact_digests = {entry["sha256"] for entry in plan.get("artifacts", []) if isinstance(entry, dict) and isinstance(entry.get("sha256"), str) and HEX.fullmatch(entry["sha256"])}
    discovery = plan.get("discovery", {})
    if discovery.get("coverage_complete") is not True or discovery.get("other_hosts_reviewed") is not True or type(discovery.get("unknown_callers")) is not int or discovery.get("unknown_callers") != 0 or discovery.get("evidence_sha256") not in artifact_digests:
        errors.append("caller_coverage_unknown_or_unverified")
    callers = plan.get("callers", [])
    if not isinstance(callers, list) or not 1 <= len(callers) <= 256:
        errors.append("bounded_explicit_caller_inventory_required")
        callers = []
    identifiers = set()
    unresolved = 0
    for caller in callers:
        identifier = caller.get("caller_id", "")
        if not re.fullmatch(r"[A-Za-z0-9_.-]{1,96}", identifier) or identifier in identifiers:
            errors.append("caller_identifier_invalid_or_duplicated")
        identifiers.add(identifier)
        routes = caller.get("write_routes", [])
        if not caller.get("owner") or caller.get("kind") not in ("process", "service", "scheduled_task", "script", "external_host") or not isinstance(routes, list) or not 1 <= len(routes) <= 64:
            errors.append("caller_details_incomplete")
            unresolved += 1
            continue
        for item in routes:
            route = item.get("route", "")
            decision = item.get("decision")
            valid = decision in ("upgraded", "disabled") and item.get("evidence_sha256") in artifact_digests
            if not ROUTE.fullmatch(route):
                valid = False
            if decision == "upgraded":
                if any(part in route for part in UNSUPPORTED):
                    valid = False
                elif route.endswith("recover-stale"):
                    valid = valid and item.get("replacement") == "maintenance_cli_recover_tasks" and item.get("writers_stop_required") is True and item.get("explicit_destination") is True
                elif route.endswith(("/retry", "/reset")):
                    valid = valid and item.get("replacement") == "v2_resources_process_action_retry" and item.get("starts_transfer_accepted") is True and item.get("equivalent") is False
                elif route.endswith("/process"):
                    valid = valid and item.get("replacement") in ("v2_resources_process", "fixed_legacy_process")
                    if item.get("replacement") == "fixed_legacy_process":
                        valid = valid and plan.get("legacy_view_provider") in ("local", "baidu", "gdrive")
                else:
                    valid = False
            if not valid:
                unresolved += 1
    if unresolved:
        errors.append("callers_require_upgrade_or_explicit_disable")
    cleanup = plan.get("cleanup", {})
    if any(cleanup.get(key) is not True for key in ("deletion_disabled_until_capture", "capture_before_every_delete", "parent_cascade_four_owners_reviewed", "writers_and_maintenance_required")) or cleanup.get("procedure_evidence_sha256") not in artifact_digests:
        errors.append("cleanup_capture_procedure_incomplete")
    stopped = plan.get("stop_window", {})
    stopped_valid = stopped.get("basis") == "operator_attestation" and bool(stopped.get("attested_by")) and stopped.get("all_instances_stopped") is True and stopped.get("evidence_sha256") in artifact_digests
    for key in ("running_claims", "uncertain_tasks", "external_writers"):
        stopped_valid = stopped_valid and type(stopped.get(key)) is int and stopped.get(key) == 0
    try:
        start, finish = (RELEASE["timestamp"](stopped.get(key)) for key in ("started_at", "finished_at"))
        duration = (finish - start).total_seconds()
        age = (dt.datetime.now(dt.timezone.utc) - finish).total_seconds()
        stopped_valid = stopped_valid and start >= RELEASE["timestamp"](release["not_before"]) and duration >= release["limits"]["writers_observation_seconds"] and -300 <= age <= 300
    except (ValueError, TypeError):
        stopped_valid = False
    if not stopped_valid:
        errors.append("stop_window_unknown_stale_or_attestation_incomplete")
    return {"format": "crawler.resource-deployment-declarations.v1", "scope_sha256": scope_hash,
            "observed_at": now(), "declarations_consistent": not errors, "errors": sorted(set(errors)),
            "unresolved_route_declarations": unresolved, "writer_compatibility_ready_for_review": not any(error for error in errors if not error.startswith("stop_window")),
            "writers_stopped_declaration_consistent": bool(stopped_valid), "stop_verification_basis": "operator_attestation_with_artifacts",
            "evidence_authenticity_independently_verified": False, "production_authorized": False,
            "activation_performed": False, "schema_retirement_authorized": False,
            "release_evidence_kinds": ["writer_compatibility", "writers_stopped"], "release_evidence_generated": False}


def main():
    """所有输出独占创建；错误不回显路径、参数、凭据或输入正文。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("command", choices=("template", "config", "inventory", "declarations"))
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--env-file", type=Path)
    parser.add_argument("--plan", type=Path)
    parser.add_argument("--release-plan", type=Path)
    parser.add_argument("--repo-root", type=Path, default=ROOT)
    parser.add_argument("--cli", type=Path)
    parser.add_argument("--mysql", type=Path)
    parser.add_argument("--probe-rc", action="store_true")
    parser.add_argument("--probe-targets", action="store_true")
    args = parser.parse_args()
    try:
        if args.output.exists():
            raise ValueError("exclusive_output_required")
        if args.command == "template":
            result = RELEASE["read_json"](ROOT / "config/resource-deployment-plan.example.json")
        elif args.command == "config":
            result = config_report(args)
        elif args.command == "inventory":
            result = inventory(args.repo_root.resolve())
        else:
            if not args.plan or not args.release_plan:
                raise ValueError("both_plans_required")
            result = declarations(RELEASE["read_json"](args.plan), RELEASE["read_json"](args.release_plan), args.plan.parent)
        RELEASE["write_json"](args.output, result)
        passed = result.get("readiness_verified", result.get("declarations_consistent", False))
        print(json.dumps({"written": True, "passed": passed, "production_authorized": False}))
        return 0 if args.command in ("template", "inventory") or passed else 2
    except (ValueError, TypeError, KeyError, OSError, OverflowError, AttributeError, subprocess.SubprocessError):
        print("FAIL: deployment input, dependency or exclusive output invalid", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
