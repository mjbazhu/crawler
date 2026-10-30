#!/usr/bin/env python3
"""部署准备工具的标准库离线/本机协议测试，不连接真实平台或数据库。"""
import base64
import datetime as dt
import http.server
import json
from pathlib import Path
import runpy
import tempfile
import threading
from types import SimpleNamespace
import unittest
from unittest import mock

TOOL = runpy.run_path(str(Path(__file__).with_name("resource-deployment-readiness.py")))
RELEASE = TOOL["RELEASE"]


def release_plan():
    """合成范围仅用于工具测试，不构成部署证据。"""
    value = RELEASE["template"]()
    value["not_before"] = (dt.datetime.now(dt.timezone.utc) - dt.timedelta(hours=1)).isoformat()
    value["scope"].update(release_id="fixture", revision="a" * 40, source_manifest_sha256="a" * 64,
                          cli_sha256="b" * 64, server_sha256="c" * 64, database_identity="d" * 64,
                          baseline_sha256="e" * 64, target_fingerprints={"local": "1" * 64, "baidu": "2" * 64, "gdrive": "3" * 64},
                          target_revisions={"local": 1, "baidu": 2, "gdrive": 3},
                          history_local_files="preserve_paths_only", local_path_manifest_sha256="4" * 64)
    value["limits"].update(query_p95_ms=100, lock_wait_p95_ms=100, minimum_enqueues_per_second=1,
                           peak_rss_bytes=1073741824, peak_staging_bytes=1073741824, recovery_seconds=120,
                           writers_observation_seconds=30)
    return value


class DeploymentTests(unittest.TestCase):
    """覆盖未知值、证据围栏及认证输送，失败时不泄露样例密钥。"""
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(prefix="crawler-deployment-test-")
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        self.values = TOOL["environment"](TOOL["ROOT"] / "config/resource-production.env.example", {})
        self.values.update(CRAWLER_MYSQL_HOST="127.0.0.1", CRAWLER_MYSQL_PORT="3306", CRAWLER_MYSQL_USER="fixture",
                           CRAWLER_MYSQL_PASSWORD="fixture-secret-do-not-print", CRAWLER_MYSQL_DATABASE="fixture",
                           CRAWLER_RESOURCE_LEGACY_VIEW_PROVIDER="baidu")
        for key in ("CRAWLER_STORAGE_PATH", "CRAWLER_MYSQL_MIGRATIONS_PATH", "CRAWLER_RESOURCE_LOCAL_STAGING_PATH",
                    "CRAWLER_RESOURCE_BAIDU_STAGING_PATH", "CRAWLER_RESOURCE_GDRIVE_STAGING_PATH"):
            path = self.root / key.lower()
            path.mkdir()
            self.values[key] = str(path)
        for name in ("BAIDU", "GDRIVE"):
            self.values.update({"CRAWLER_RESOURCE_" + name + "_RCLONE_" + key: value
                               for key, value in {"RC_USER": "fixture", "RC_PASSWORD": "fixture-rc-secret", "TARGET_FS": name.lower() + ":"}.items()})

    def declaration_fixture(self):
        """每份声明绑定本目录真实文件摘要，内容为合成样例。"""
        release = release_plan()
        plan = RELEASE["read_json"](TOOL["ROOT"] / "config/resource-deployment-plan.example.json")
        evidence = self.root / "fixture.json"
        evidence.write_text('{"execution":"fixture"}\n', encoding="utf-8")
        sha = TOOL["digest"](evidence)
        plan.update(release_scope_sha256=RELEASE["validate_plan"](release), legacy_view_provider="baidu",
                    artifacts=[{"path": evidence.name, "bytes": evidence.stat().st_size, "sha256": sha}])
        plan["discovery"].update(coverage_complete=True, other_hosts_reviewed=True, unknown_callers=0, evidence_sha256=sha)
        plan["callers"][0]["owner"] = "fixture-owner"
        plan["callers"][0]["write_routes"][0].update(decision="disabled", evidence_sha256=sha)
        finish = dt.datetime.now(dt.timezone.utc)
        plan["stop_window"].update(attested_by="fixture-operator", started_at=(finish - dt.timedelta(seconds=60)).isoformat(),
                                    finished_at=finish.isoformat(), running_claims=0, uncertain_tasks=0,
                                    external_writers=0, all_instances_stopped=True, evidence_sha256=sha)
        plan["cleanup"].update(capture_before_every_delete=True, parent_cascade_four_owners_reviewed=True,
                                procedure_evidence_sha256=sha)
        return plan, release

    def test_complete_config_without_network(self):
        self.assertEqual(TOOL["check_configuration"](self.values), [])

    def test_template_unknowns_fail(self):
        values = TOOL["environment"](TOOL["ROOT"] / "config/resource-production.env.example", {})
        self.assertIn("missing:CRAWLER_MYSQL_DATABASE", TOOL["check_configuration"](values))

    def test_env_precedence_and_no_expansion(self):
        path = self.root / "selected.env"
        path.write_text("CRAWLER_X=first\nCRAWLER_X=second\nCRAWLER_Y='${SECRET}'\n", encoding="utf-8")
        self.assertEqual(TOOL["environment"](path, {})["CRAWLER_X"], "first")
        self.assertEqual(TOOL["environment"](path, {"CRAWLER_X": "runtime"})["CRAWLER_X"], "runtime")
        self.assertEqual(TOOL["environment"](path, {})["CRAWLER_Y"], "${SECRET}")

    def test_missing_env_and_bom_rejected(self):
        with self.assertRaises(ValueError):
            TOOL["environment"](self.root / "absent.env", {})
        path = self.root / "bom.env"
        path.write_bytes(b"\xef\xbb\xbfCRAWLER_X=x")
        with self.assertRaises(ValueError):
            TOOL["environment"](path, {})

    def test_staging_overlap_and_unsafe_rc_rejected(self):
        self.values["CRAWLER_RESOURCE_GDRIVE_STAGING_PATH"] = self.values["CRAWLER_RESOURCE_BAIDU_STAGING_PATH"]
        self.values["CRAWLER_RESOURCE_BAIDU_RCLONE_RC_URL"] = "http://remote.invalid:5572"
        errors = TOOL["check_configuration"](self.values)
        self.assertTrue(any(error.startswith("staging_paths_overlap:") for error in errors))
        self.assertIn("authenticated_rc_transport_invalid:BAIDU", errors)

    def test_invalid_budget_and_auto_init_rejected(self):
        self.values["CRAWLER_RESOURCE_BAIDU_STAGING_MAX_BYTES"] = "1"
        self.values["CRAWLER_MYSQL_INIT_SCHEMA"] = "true"
        self.assertIn("staging_cannot_hold_one_resource:BAIDU", TOOL["check_configuration"](self.values))
        self.assertIn("explicit_selection_required:CRAWLER_MYSQL_INIT_SCHEMA", TOOL["check_configuration"](self.values))

    def test_rc_inheritance(self):
        self.values.pop("CRAWLER_RESOURCE_BAIDU_RCLONE_RC_USER")
        self.values["CRAWLER_RCLONE_RC_USER"] = "global-fixture"
        self.assertEqual(TOOL["rc_settings"](self.values, "BAIDU")["RC_USER"], "global-fixture")
        self.assertEqual(TOOL["rc_settings"](self.values, "GDRIVE")["RC_USER"], "fixture")

    def test_offline_config_keeps_runtime_unknown(self):
        args = SimpleNamespace(env_file=None, release_plan=None, cli=None, mysql=None, probe_rc=False, probe_targets=False)
        with mock.patch.dict(TOOL["config_report"].__globals__, {"environment": lambda _: self.values}):
            result = TOOL["config_report"](args)
        self.assertTrue(result["configuration_complete"])
        self.assertEqual(result["database_observation"], "unknown")
        self.assertFalse(result["readiness_verified"])
        self.assertFalse(result["historical_local_files_inspected"])

    def test_native_probe_scope_and_password_boundary(self):
        release = release_plan()
        cli = self.root / "fixture-cli.exe"
        cli.write_bytes(b"fixture-not-executable")
        release["scope"]["cli_sha256"] = TOOL["digest"](cli)
        plan_path = self.root / "release.json"
        RELEASE["write_json"](plan_path, release)
        args = SimpleNamespace(env_file=None, release_plan=plan_path, cli=cli, mysql=cli, probe_rc=True, probe_targets=True)
        calls = []

        def fixture_native(arguments, child):
            calls.append(arguments)
            self.assertFalse(any(self.values["CRAWLER_MYSQL_PASSWORD"] in value for value in arguments))
            if "--destination" in arguments:
                name = arguments[-1]
                self.assertFalse(Path(child["CRAWLER_ENV_FILE"]).exists())
                return 0, json.dumps({"available": True, "fingerprint": release["scope"]["target_fingerprints"][name], "identity_scope": "webdav_mount" if name == "baidu" else "provider_account"})
            self.assertEqual(child["MYSQL_PWD"], self.values["CRAWLER_MYSQL_PASSWORD"])
            self.assertFalse(Path(child["MYSQL_TEST_LOGIN_FILE"]).exists())
            self.assertNotIn("--no-login-paths", arguments)
            self.assertTrue(arguments[-1].startswith("--execute=START TRANSACTION READ ONLY;"))
            return 0, release["scope"]["database_identity"] + "\t2026102912\t2026102912\n" + "\n".join(name + "\t" + release["scope"]["target_fingerprints"][name] + "\t" + str(release["scope"]["target_revisions"][name]) + "\tmaintenance" for name in ("baidu", "gdrive", "local"))

        with mock.patch.dict(TOOL["config_report"].__globals__, {"environment": lambda _: self.values, "native": fixture_native, "rc_version": lambda _: "v1.75.2"}):
            result = TOOL["config_report"](args)
        self.assertTrue(result["readiness_verified"])
        self.assertFalse(result["production_authorized"])
        self.assertEqual(len(calls), 3)

    def test_declarations_consistent_still_not_release_authority(self):
        plan, release = self.declaration_fixture()
        result = TOOL["declarations"](plan, release, self.root)
        self.assertTrue(result["declarations_consistent"])
        self.assertFalse(result["evidence_authenticity_independently_verified"])
        self.assertFalse(result["release_evidence_generated"])
        self.assertFalse(result["schema_retirement_authorized"])

    def test_unknown_discovery_and_boolean_zero_fail(self):
        plan, release = self.declaration_fixture()
        plan["discovery"]["unknown_callers"] = None
        plan["stop_window"]["external_writers"] = False
        result = TOOL["declarations"](plan, release, self.root)
        self.assertFalse(result["declarations_consistent"])
        self.assertFalse(result["writers_stopped_declaration_consistent"])

    def test_retry_and_reset_require_explicit_semantics(self):
        plan, release = self.declaration_fixture()
        item = plan["callers"][0]["write_routes"][0]
        item.update(decision="upgraded", replacement="v2_resources_process", equivalent=True)
        self.assertFalse(TOOL["declarations"](plan, release, self.root)["declarations_consistent"])
        item.update(replacement="v2_resources_process_action_retry", starts_transfer_accepted=True, equivalent=False)
        self.assertTrue(TOOL["declarations"](plan, release, self.root)["declarations_consistent"])
        item["route"] = "/api/migu/resources/reset"
        self.assertTrue(TOOL["declarations"](plan, release, self.root)["declarations_consistent"])

    def test_metadata_has_no_automatic_v2_equivalent(self):
        plan, release = self.declaration_fixture()
        item = plan["callers"][0]["write_routes"][0]
        item.update(route="/api/migu/metadata/backfill", decision="upgraded", replacement="v2_resources_process")
        self.assertFalse(TOOL["declarations"](plan, release, self.root)["declarations_consistent"])

    def test_stale_window_and_artifact_change_fail(self):
        plan, release = self.declaration_fixture()
        plan["stop_window"]["finished_at"] = (dt.datetime.now(dt.timezone.utc) - dt.timedelta(minutes=6)).isoformat()
        (self.root / "fixture.json").write_text("mutated", encoding="utf-8")
        result = TOOL["declarations"](plan, release, self.root)
        self.assertIn("artifact_metadata_mismatch", result["errors"])
        self.assertFalse(result["writers_stopped_declaration_consistent"])

    def test_scope_and_capture_unknown_fail(self):
        plan, release = self.declaration_fixture()
        plan["release_scope_sha256"] = "0" * 64
        plan["cleanup"]["capture_before_every_delete"] = None
        result = TOOL["declarations"](plan, release, self.root)
        self.assertIn("deployment_scope_mismatch", result["errors"])
        self.assertIn("cleanup_capture_procedure_incomplete", result["errors"])

    def test_history_scope_cannot_be_silently_excluded(self):
        plan, release = self.declaration_fixture()
        release["scope"]["history_local_files"] = "verify"
        plan["release_scope_sha256"] = RELEASE["validate_plan"](release)
        self.assertIn("deployment_boundary_invalid", TOOL["declarations"](plan, release, self.root)["errors"])
        release["scope"]["history_local_files"] = "preserve_paths_only"
        release["scope"].pop("local_path_manifest_sha256")
        with self.assertRaises(ValueError):
            RELEASE["validate_plan"](release)

    def test_inventory_is_bounded_and_never_emits_line_contents(self):
        scripts = self.root / "scripts"
        scripts.mkdir()
        (scripts / "caller.py").write_text(('request("/api/migu/resources/retry", token="fixture-private-marker")\n') * 257, encoding="utf-8")
        (scripts / "boundary-tests.py").write_text('"/api/migu/resources/reset"\n', encoding="utf-8")

        def fixture_native(arguments, *_args, **_kwargs):
            if arguments[0] == "powershell.exe":
                return 0, '{"processes":[],"process_count":0,"services":[],"service_count":0,"scheduled_tasks":[],"scheduled_task_count":0,"truncated":false}'
            return 0, "scripts/boundary-tests.py\0scripts/caller.py\0.env\0"

        with mock.patch.dict(TOOL["inventory"].__globals__, {"native": fixture_native}):
            result = TOOL["inventory"](self.root)
        self.assertEqual(len(result["repository_references"]), 256)
        self.assertFalse(result["repository_scan_complete"])
        self.assertIsNone(result["external_writers"])
        self.assertFalse(result["writers_stopped_proven"])
        self.assertNotIn("fixture-private-marker", json.dumps(result))
        self.assertEqual(result["repository_references"][0]["classification"], "test")

    def test_real_loopback_rc_requires_authentication(self):
        secret = base64.b64encode(b"fixture:fixture-password").decode()

        class Handler(http.server.BaseHTTPRequestHandler):
            """本机协议替身不记录请求头或正文。"""
            protected = True

            def do_POST(self):
                if self.protected and self.headers.get("Authorization") != "Basic " + secret:
                    self.send_response(401)
                    self.end_headers()
                    return
                raw = b'{"version":"v1.75.2"}'
                self.send_response(200)
                self.send_header("Content-Length", str(len(raw)))
                self.end_headers()
                self.wfile.write(raw)

            def log_message(self, *_):
                pass

        server = http.server.ThreadingHTTPServer(("127.0.0.1", 0), Handler)
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            settings = {"RC_URL": "http://127.0.0.1:" + str(server.server_port), "RC_USER": "fixture", "RC_PASSWORD": "fixture-password"}
            self.assertEqual(TOOL["rc_version"](settings), "v1.75.2")
            Handler.protected = False
            with self.assertRaises(ValueError):
                TOOL["rc_version"](settings)
        finally:
            server.shutdown()
            server.server_close()
            thread.join(timeout=2)


if __name__ == "__main__":
    unittest.main()
