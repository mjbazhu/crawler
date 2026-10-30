#!/usr/bin/env python3
"""发布/收缩工具的离线自测；不连接数据库、音乐源或网盘。"""
import datetime as dt
import hashlib
import io
import io
import json
import os
from pathlib import Path
import runpy
import sys
import tempfile
import time
from types import SimpleNamespace
import unittest
from unittest import mock

HERE = Path(__file__).resolve().parent
CHECK = runpy.run_path(str(HERE / "resource-release-check.py"))
REHEARSAL = runpy.run_path(str(HERE / "resource-release-rehearsal.py"))
RETIREMENT = runpy.run_path(str(HERE / "resource-retirement-audit.py"))
CRASH = runpy.run_path(str(HERE / "resource-crash-rehearsal.py"))
PATHS = runpy.run_path(str(HERE / "resource-local-path-manifest.py"))
NOW = dt.datetime(2026, 10, 30, 4, tzinfo=dt.timezone.utc)


def plan():
    """所有值仅为隔离的合成夹具，不能表示真实验收。"""
    value = CHECK["template"]()
    value["not_before"] = (NOW - dt.timedelta(days=1)).isoformat()
    value["scope"].update({"release_id": "fixture", "revision": "a" * 40, "schema_version": 2026102912,
                           "source_manifest_sha256": "a"*64, "cli_sha256": "b"*64, "server_sha256": "c"*64,
                           "database_identity": "b" * 64, "baseline_sha256": "c" * 64,
                           "target_fingerprints": {"local": "d" * 64, "baidu": "e" * 64, "gdrive": "f" * 64},
                           "target_revisions": {"local": 1, "baidu": 2, "gdrive": 3}})
    value["limits"].update({"minimum_rows": 10000000, "minimum_samples": 30, "query_p95_ms": 100,
                            "lock_wait_p95_ms": 50, "minimum_enqueues_per_second": 5, "peak_rss_bytes": 1024 ** 3,
                            "peak_staging_bytes": 1024 ** 3, "recovery_seconds": 120, "writers_observation_seconds": 60})
    return value


def measurements(kind, value):
    """显式构造各场景要求；失败用例单独破坏一项。"""
    scope = value["scope"]
    return {
        "history_adoption": {"all_targets_reviewed": True, "source_conflicts": 0, "unresolved_decisions": 0, "baseline_sha256": scope["baseline_sha256"]},
        "writer_compatibility": {"unmigrated_writers": 0, "old_clients_verified": True},
        "writers_stopped": {"observation_seconds": 60, "running_claims": 0, "uncertain_tasks": 0, "external_writers": 0, "all_instances_stopped": True},
        "clone_upgrade": {"database": "crawler_ownership_test_release_fixture", "before_schema_version": 2026102901, "schema_version": scope["schema_version"], "migration_checksums_verified": True, "business_rows_before": 7, "business_rows_after": 7, "unexpected_columns_removed": 0, "production_writes": 0},
        "database_restore": {"database": "crawler_ownership_test_release_fixture", "source_server_uuid": "source-fixture", "restore_server_uuid": "restore-fixture", "restore_was_empty": True, "source_data_sha256": "a" * 64, "restored_data_sha256": "a" * 64, "migration_checksums_verified": True, "trigger_definitions_verified": True, "triggers_installed_after_data": True, "production_writes": 0},
        "file_restore": {"source_was_stopped": True, "journal_included": True, "source_manifest_sha256": "a" * 64, "restored_manifest_sha256": "a" * 64, "missing_files": 0, "mismatched_files": 0},
        "multi_instance_crash": {"instances": 2, "claimed_tasks": 2, "forced_termination": True, "duplicate_publications": 0, "stale_commits": 0, "uncertain_overwrites": 0, "post_recovery_completed": 2, "recovery_seconds": 45},
        "scale_workload": {"rows": 10000000, "samples": 30, "failures": 0, "query_p95_ms": 50, "lock_wait_p95_ms": 20, "enqueues_per_second": 20, "peak_rss_bytes": 128 * 1024 ** 2, "peak_staging_bytes": 0},
        "cloud_live": {"clouds_started_before_local_completed": True, "destinations": {name: {"actual_io": True, "target_fingerprint": fingerprint, "published_objects": 1, "verified_objects": 1, "sha256_verified": True, "stop_confirmed": True} for name, fingerprint in scope["target_fingerprints"].items()}}
    }[kind]


def evidence(directory, value):
    """合成日志仅用于验证门禁算法，不持久保存为部署证据。"""
    for kind in CHECK["KINDS"]:
        log = directory / (kind + ".log")
        log.write_text("synthetic unit-test artifact\n", encoding="utf-8")
        record = {"format": "crawler.resource-release-evidence.v1", "kind": kind,
                  "scope_sha256": CHECK["validate_plan"](value), "execution": "actual",
                  "started_at": (NOW - dt.timedelta(seconds=90)).isoformat(), "finished_at": NOW.isoformat(),
                  "measurements": measurements(kind, value), "artifacts": [{"path": log.name, "bytes": log.stat().st_size, "sha256": hashlib.sha256(log.read_bytes()).hexdigest()}]}
        (directory / (kind + ".json")).write_text(json.dumps(record), encoding="utf-8")


class ReleaseEvidenceTests(unittest.TestCase):
    """覆盖证据绑定、未知值、过期及篡改失败关闭。"""
    def test_local_exemption_requires_bound_path_proof(self):
        for mutation in ("valid", "missing_proof", "changed_path", "bool_count", "missing_writer"):
            with self.subTest(mutation=mutation), tempfile.TemporaryDirectory() as name:
                root = Path(name); value = plan()
                value["scope"].update(history_local_files="preserve_paths_only", local_path_manifest_sha256="a"*64)
                evidence(root, value); (root/"file_restore.json").unlink()
                record_path = root/"clone_upgrade.json"; record = json.loads(record_path.read_text())
                proof = {"historical_local_paths_preserved": True, "local_path_manifest_sha256_before": "a"*64,
                         "local_path_manifest_sha256_after": "a"*64, "local_paths_count_before": 7, "local_paths_count_after": 7}
                if mutation == "changed_path": proof["local_path_manifest_sha256_after"] = "b"*64
                if mutation == "bool_count": proof["local_paths_count_after"] = True
                if mutation != "missing_proof": record["measurements"].update(proof)
                record_path.write_text(json.dumps(record))
                if mutation == "missing_writer": (root/"writers_stopped.json").unlink()
                result = CHECK["check"](value, root, NOW)
                self.assertEqual(result["evidence_complete"], mutation == "valid")
                self.assertEqual(len(result["checks"]), 8)
                self.assertEqual(result["excluded_evidence"][0]["status"], "excluded_not_tested")
                self.assertFalse(result["production_authorized"])

    def test_local_policy_legacy_hash_and_invalid_values(self):
        value = plan(); value["scope"].pop("history_local_files", None)
        self.assertEqual(CHECK["validate_plan"](value), CHECK["canonical"]({key: value[key] for key in ("scope", "limits", "not_before")}))
        old_hash = CHECK["validate_plan"](value)
        value["scope"].update(history_local_files="preserve_paths_only", local_path_manifest_sha256="a"*64)
        self.assertNotEqual(CHECK["validate_plan"](value), old_hash)
        for policy in (None, True, [], "skip", "preserve_paths_only"):
            bad = plan(); bad["scope"]["history_local_files"] = policy
            with self.assertRaises(ValueError): CHECK["validate_plan"](bad)

    def test_local_policy_cannot_reuse_previous_evidence(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name); value = plan(); evidence(root, value)
            value["scope"].update(history_local_files="preserve_paths_only", local_path_manifest_sha256="a"*64)
            self.assertFalse(CHECK["check"](value, root, NOW)["evidence_complete"])

    def test_complete_evidence_is_not_authorization(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name); value = plan(); evidence(root, value)
            result = CHECK["check"](value, root, NOW)
            self.assertTrue(result["evidence_complete"])
            self.assertFalse(result["production_authorized"])
            self.assertFalse(result["evidence_authenticity_independently_verified"])

    def test_missing_fixture_stale_scope_and_tamper_fail(self):
        for mutation in ("missing", "fixture", "stale", "scope", "tamper", "escape", "unknown", "bool", "infinity", "invalid_measurements", "invalid_artifact", "invalid_time"):
            with self.subTest(mutation=mutation), tempfile.TemporaryDirectory() as name:
                root = Path(name); value = plan(); evidence(root, value)
                path = root / "writers_stopped.json"; record = json.loads(path.read_text())
                if mutation == "missing": path.unlink()
                elif mutation == "tamper": (root / "writers_stopped.log").write_text("modified\n")
                else:
                    if mutation == "fixture": record["execution"] = "fixture"
                    if mutation == "stale": record["finished_at"] = (NOW - dt.timedelta(minutes=20)).isoformat(); record["started_at"] = record["finished_at"]
                    if mutation == "scope": record["scope_sha256"] = "0" * 64
                    if mutation == "escape": record["artifacts"][0]["path"] = "../private.env"
                    if mutation == "unknown": record["measurements"]["uncertain_tasks"] = None
                    if mutation == "bool": record["measurements"]["uncertain_tasks"] = False
                    if mutation == "infinity": record["measurements"]["observation_seconds"] = float("inf")
                    if mutation == "invalid_measurements": record["measurements"] = None
                    if mutation == "invalid_artifact": record["artifacts"] = [None]
                    if mutation == "invalid_time": record["finished_at"] = None
                    path.write_text(json.dumps(record))
                self.assertFalse(CHECK["check"](value, root, NOW)["evidence_complete"])

    def test_template_unknown_limits_and_target_alias_rejected(self):
        with self.assertRaises((ValueError, TypeError)): CHECK["validate_plan"](CHECK["template"]())
        value = plan(); value["limits"]["query_p95_ms"] = None
        with self.assertRaises(ValueError): CHECK["validate_plan"](value)

    def test_late_threshold_change_invalidates_prior_evidence(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name); value = plan(); evidence(root, value)
            value["limits"]["query_p95_ms"] *= 2
            self.assertFalse(CHECK["check"](value, root, NOW)["evidence_complete"])
        value = plan(); value["scope"]["target_fingerprints"]["baidu"] = value["scope"]["target_fingerprints"]["gdrive"]
        with self.assertRaises(ValueError): CHECK["validate_plan"](value)

    def test_output_never_overwrites(self):
        with tempfile.TemporaryDirectory() as name:
            path = Path(name) / "result.json"; CHECK["write_json"](path, {})
            with self.assertRaises(FileExistsError): CHECK["write_json"](path, {"changed": True})

    def test_nested_evidence_and_ambiguous_duplicate(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name); value = plan(); evidence(root, value)
            nested = root/"database_restore"; nested.mkdir()
            (root/"database_restore.log").rename(nested/"database_restore.log")
            record = root/"database_restore.json"; record.rename(nested/record.name)
            self.assertTrue(CHECK["check"](value, root, NOW)["evidence_complete"])
            record.write_bytes((nested/record.name).read_bytes())
            self.assertFalse(CHECK["check"](value, root, NOW)["evidence_complete"])


class LocalPathManifestTests(unittest.TestCase):
    """路径摘要只验证数据库字节保留，拒绝伪造参考及越界行。"""
    def reference(self):
        value = {"format": PATHS["FORMAT"], "owner_highwaters": {key: 10 for key in PATHS["OWNERS"]},
                 "paths_count": 1, "rows_sha256": "a"*64}
        value["path_manifest_sha256"] = CHECK["canonical"](PATHS["content"](value))
        return value

    def test_reference_digest_and_highwaters_fail_closed(self):
        for mutation in ("valid", "digest", "bool", "missing_owner", "negative", "format"):
            value = self.reference()
            if mutation == "digest": value["rows_sha256"] = "b"*64
            if mutation == "bool": value["owner_highwaters"]["song_resources"] = True
            if mutation == "missing_owner": value["owner_highwaters"].pop("album_resources")
            if mutation == "negative": value["paths_count"] = -1
            if mutation == "format": value["format"] = "other"
            if mutation == "valid": self.assertEqual(PATHS["validate_reference"](value), value["owner_highwaters"])
            else:
                with self.assertRaises(ValueError): PATHS["validate_reference"](value)

    def test_rows_are_sorted_scoped_hashes(self):
        waters = self.reference()["owner_highwaters"]
        row = ("song_resources\t1\t" + "a"*64 + "\n").encode()
        count, digest = PATHS["hash_rows"](io.BytesIO(row), waters)
        self.assertEqual(count, 1); self.assertEqual(digest, hashlib.sha256(row).hexdigest())
        for invalid in (row+row, row.replace(b"\t1\t", b"\t11\t"), b"song_resources\t1\tprivate/path\n", b"unknown\t1\t"+b"a"*64+b"\n"):
            with self.assertRaises(ValueError): PATHS["hash_rows"](io.BytesIO(invalid), waters)

    def test_snapshot_requires_stopped_test_database_and_matching_uuid(self):
        function = PATHS["snapshot"]
        for database, stopped in (("crawler", True), ("crawler_ownership_test_release_fixture", False)):
            with self.assertRaises(ValueError): function(SimpleNamespace(database=database, writers_stopped=stopped))
        args = SimpleNamespace(database="crawler_ownership_test_release_fixture", writers_stopped=True,
                               timeout_seconds=5, reference_manifest=None, expected_server_uuid="expected", mysql=Path("fixture"))
        native = dict(PATHS["NATIVE"], private_environment=lambda prefix: ({}, {}), query=lambda *a: "other")
        with mock.patch.dict(function.__globals__, {"NATIVE": native}):
            with self.assertRaisesRegex(ValueError, "server_mismatch"): function(args)

    def test_reference_freezes_highwaters_without_file_access(self):
        function = PATHS["snapshot"]
        with tempfile.TemporaryDirectory() as name:
            root = Path(name); reference = self.reference(); row = ("song_resources\t1\t"+"a"*64+"\n").encode()
            reference["rows_sha256"] = hashlib.sha256(row).hexdigest()
            reference["path_manifest_sha256"] = CHECK["canonical"](PATHS["content"](reference))
            ref = root/"before.json"; ref.write_text(json.dumps(reference))
            args = SimpleNamespace(database="crawler_ownership_test_release_fixture", writers_stopped=True,
                                   timeout_seconds=5, reference_manifest=ref, expected_server_uuid="expected", mysql=Path("fixture"), output=root/"after.json")
            queries = []
            def query(*arguments):
                queries.append(arguments[-2]); return "expected" if "server_uuid" in arguments[-2] else "1"
            def run(arguments, environment, timeout, **kwargs):
                self.assertIn("id<=10", arguments[-1]); self.assertIn("READ ONLY", arguments[-1])
                self.assertIn("SHA2(CAST(local_path AS BINARY)", arguments[-1]); kwargs["output"].write(row)
            native = dict(PATHS["NATIVE"], private_environment=lambda prefix: ({}, {}), query=query, run=run, mysql_arguments=lambda *a: [])
            with mock.patch.dict(function.__globals__, {"NATIVE": native}): result = function(args)
            self.assertTrue(result["matches_reference"]); self.assertFalse(result["filesystem_checked"])
            self.assertFalse(any("MAX(id)" in sql for sql in queries))


class RehearsalTests(unittest.TestCase):
    """native/mysql均用替身；本机只启动自己可控的Python超时夹具。"""
    def test_database_whitelist(self):
        for value in ("crawler", "mysql", "crawler_ownership_test_capacity_a", "crawler_ownership_test_release_x;DROP"):
            self.assertFalse(CHECK["DATABASE"].fullmatch(value))
        self.assertTrue(CHECK["DATABASE"].fullmatch("crawler_ownership_test_release_fixture"))

    def test_private_environment_never_uses_dotenv(self):
        fixture = {"CRAWLER_RELEASE_SOURCE_MYSQL_HOST": "127.0.0.1", "CRAWLER_RELEASE_SOURCE_MYSQL_PORT": "3306", "CRAWLER_RELEASE_SOURCE_MYSQL_USER": "fixture", "CRAWLER_RELEASE_SOURCE_MYSQL_PASSWORD": "fixture-only", "CRAWLER_ENV_FILE": "never-read.env", "CRAWLER_MYSQL_DATABASE": "crawler", "MYSQL_PWD": "other"}
        with mock.patch.dict(os.environ, fixture, clear=True):
            values, child = REHEARSAL["private_environment"]("CRAWLER_RELEASE_SOURCE_MYSQL_")
        self.assertNotIn("CRAWLER_ENV_FILE", child); self.assertNotIn("CRAWLER_MYSQL_DATABASE", child)
        self.assertEqual(child["MYSQL_PWD"], values["PASSWORD"])
        self.assertFalse(Path(child["MYSQL_TEST_LOGIN_FILE"]).exists())
        self.assertIn("crawler-release-no-login-", child["MYSQL_TEST_LOGIN_FILE"])

    def test_native_deadline_and_output_bound(self):
        with self.assertRaises(ValueError): REHEARSAL["run"]([sys.executable, "-c", "import time; time.sleep(2)"], dict(os.environ), 0.05)
        with self.assertRaises(ValueError): REHEARSAL["run"]([sys.executable, "-c", "print('x'*10000)"], dict(os.environ), 5, max_bytes=100)

    def test_restore_refuses_same_server_or_existing_tables(self):
        function = REHEARSAL["restore"]; globals_ = function.__globals__
        for same, occupied in ((True, False), (False, True)):
            with self.subTest(same=same, occupied=occupied), tempfile.TemporaryDirectory() as name:
                args = SimpleNamespace(writers_stopped=True, mysql=Path("mysql-fixture"), mysqldump=Path("dump-fixture"), database="crawler_ownership_test_release_fixture", timeout_seconds=5, deadline=time.monotonic()+5, output_dir=Path(name))
                def query(exe, values, env, database, sql, timeout):
                    if "server_uuid" in sql: return "same" if same else values["HOST"]
                    return "1" if occupied else "0"
                with mock.patch.dict(globals_, {"query": query, "private_environment": lambda prefix: ({"HOST": "restore"}, {}), "dump": mock.Mock(side_effect=AssertionError("dump must not start"))}):
                    with self.assertRaises(ValueError): function(args, {"HOST": "source"}, {}, NOW.isoformat())

    def test_restore_uses_only_new_empty_distinct_instance(self):
        function = REHEARSAL["restore"]; globals_ = function.__globals__
        with tempfile.TemporaryDirectory() as name:
            root = Path(name); value = plan(); plan_path = root / "plan.json"; plan_path.write_text(json.dumps(value))
            args = SimpleNamespace(writers_stopped=True, mysql=Path("mysql-fixture"), mysqldump=Path("dump-fixture"), database="crawler_ownership_test_release_fixture", timeout_seconds=5, deadline=time.monotonic()+5, output_dir=root, plan=plan_path, scope_hash=CHECK["validate_plan"](value), verify_scope=lambda: None)
            def query(exe, values, env, database, sql, timeout):
                if "server_uuid" in sql: return values["HOST"]
                if "schema_migrations" in sql: return "2026102912\tfixture-checksum"
                return "0"
            def dump(args, executable, values, environment, target, data_only=False, schema_only=False, triggers_only=False, omit_triggers=False):
                if target.name == "backup.sql": self.assertTrue(omit_triggers)
                target.write_bytes(b"CREATE TRIGGER fixture_trigger BEFORE INSERT ON fixture FOR EACH ROW SET @fixture=1;\n" if triggers_only else b"INSERT INTO fixture VALUES(1);\n" if data_only else b"CREATE TABLE fixture(id INT);\n")
            invoked = []
            def execute(arguments, environment, timeout, **kwargs): invoked.append(Path(kwargs["input_file"].name).name); self.assertIn("--database="+args.database, arguments); self.assertNotIn("DROP", kwargs["input_file"].read().decode()); return ""
            with mock.patch.dict(globals_, {"query": query, "private_environment": lambda prefix: ({"HOST": "restore", "PORT": "3306", "USER": "fixture"}, {}), "dump": dump, "run": execute}):
                self.assertEqual(function(args, {"HOST": "source"}, {}, NOW.isoformat()), 0)
            self.assertEqual(invoked, ["backup.sql", "triggers.sql"])
            report = json.loads((root/"database_restore.json").read_text()); self.assertFalse(report["measurements"]["files_or_journal_restored"])
            self.assertTrue(report["measurements"]["trigger_definitions_verified"])

    def test_dump_separates_triggers_from_bulk_data(self):
        function = REHEARSAL["dump"]; commands = []
        with tempfile.TemporaryDirectory() as name:
            args = SimpleNamespace(database="crawler_ownership_test_release_fixture", deadline=time.monotonic()+5, timeout_seconds=5, max_backup_bytes=1024)
            config = {"HOST": "fixture", "PORT": "3306", "USER": "fixture"}
            with mock.patch.dict(function.__globals__, {"run": lambda command, *a, **k: commands.append(command)}):
                function(args, Path("dump-fixture"), config, {}, Path(name)/"backup.sql", omit_triggers=True)
                function(args, Path("dump-fixture"), config, {}, Path(name)/"triggers.sql", triggers_only=True)
            self.assertIn("--skip-triggers", commands[0]); self.assertIn("--no-autocommit", commands[0])
            self.assertIn("--triggers", commands[1]); self.assertIn("--no-data", commands[1]); self.assertIn("--no-create-info", commands[1])

    def test_restore_refuses_scheduled_events_in_either_database(self):
        function = REHEARSAL["restore"]
        for owner in ("source", "restore"):
            args = SimpleNamespace(writers_stopped=True, mysql=Path("mysql-fixture"), mysqldump=Path("dump-fixture"), database="crawler_ownership_test_release_fixture", timeout_seconds=5, deadline=time.monotonic()+5)
            def query(exe, values, env, database, sql, timeout):
                if "server_uuid" in sql: return values["HOST"]
                return "1" if "information_schema.events" in sql and values["HOST"] == owner else "0"
            with self.subTest(owner=owner), mock.patch.dict(function.__globals__, {"query": query, "private_environment": lambda prefix: ({"HOST": "restore"}, {})}):
                with self.assertRaises(ValueError): function(args, {"HOST": "source"}, {}, NOW.isoformat())

    def test_restore_evidence_requires_trigger_verification(self):
        value = plan()
        for key in ("trigger_definitions_verified", "triggers_installed_after_data"):
            record = measurements("database_restore", value); del record[key]
            self.assertTrue(CHECK["scenario_errors"]("database_restore", record, value["limits"], value["scope"]))

    def test_schema_preview_rejects_cross_database_and_version_comments(self):
        with tempfile.TemporaryDirectory() as name:
            path = Path(name)/"schema.sql"; database = "crawler_ownership_test_release_fixture"
            for sql in ("CREATE VIEW v AS SELECT x FROM `crawler`.`songs`;", "/*!50000 CREATE VIEW v AS SELECT x FROM `crawler`.`songs` */;", "CREATE TRIGGER t AFTER INSERT ON f FOR EACH ROW INSERT INTO `crawler`.`songs` VALUES(1);", "USE crawler;", "CREATE DATABASE crawler;"):
                path.write_text(sql)
                with self.assertRaises(ValueError): REHEARSAL["validate_schema"](path, database)
            path.write_text("CREATE TABLE fixture(id INT); INSERT INTO fixture VALUES('USE crawler; FROM crawler.songs'); CREATE VIEW v AS SELECT f.id FROM `"+database+"`.`fixture` f;")
            REHEARSAL["validate_schema"](path, database)

    def test_scope_fence_detects_actual_database_or_target_drift(self):
        function = REHEARSAL["verify_scope"]; globals_ = function.__globals__; value = plan()
        args = SimpleNamespace(mysql=Path("mysql-fixture"), database="crawler_ownership_test_release_fixture", timeout_seconds=5, deadline=time.monotonic()+5)
        for drift in ("database", "schema", "target", "none"):
            def query(exe, config, env, database, sql, timeout):
                if "SHA2" in sql: return "0"*64 if drift=="database" else value["scope"]["database_identity"]
                if "MAX(version)" in sql: return "1" if drift=="schema" else str(value["scope"]["schema_version"])
                return "\n".join(name+"\t"+("0"*64 if drift=="target" else hash_)+"\t"+str(value["scope"]["target_revisions"][name]) for name, hash_ in value["scope"]["target_fingerprints"].items())
            with self.subTest(drift=drift), mock.patch.dict(globals_, {"query": query}):
                if drift=="none": function(args, {}, {}, value)
                else:
                    with self.assertRaises(ValueError): function(args, {}, {}, value)


class CrashProcessTests(unittest.TestCase):
    """多进程演练的隔离范围、独占输出和异常清理边界。"""
    def test_production_database_refused_before_spawn(self):
        with tempfile.TemporaryDirectory() as name:
            output = Path(name)/"new"
            args = ["crash", "--database", "crawler", "--probe", sys.executable, "--output-dir", str(output)]
            with mock.patch.object(sys, "argv", args), mock.patch("subprocess.Popen") as spawn, mock.patch("sys.stdout", new_callable=io.StringIO):
                self.assertEqual(CRASH["main"](), 1)
            spawn.assert_not_called(); self.assertFalse(output.exists())

    def test_existing_output_unchanged(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name); marker = root/"keep.json"; marker.write_text("keep")
            args = ["crash", "--database", "crawler_ownership_test_crash_fixture", "--probe", sys.executable, "--output-dir", str(root)]
            tools = dict(CRASH["TOOLS"], private_environment=lambda prefix: ({}, {}))
            with mock.patch.object(sys, "argv", args), mock.patch.dict(CRASH["main"].__globals__, {"TOOLS": tools}), mock.patch("sys.stdout", new_callable=io.StringIO):
                self.assertEqual(CRASH["main"](), 1)
            self.assertEqual(list(root.iterdir()), [marker]); self.assertEqual(marker.read_text(), "keep")

    def test_second_spawn_failure_terminates_only_owned_first_child(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)/"new"
            args = ["crash", "--database", "crawler_ownership_test_crash_fixture", "--probe", sys.executable, "--output-dir", str(root)]
            def run(command, env, timeout, output): output.write(b'{"seeded":2}\n')
            tools = dict(CRASH["TOOLS"], private_environment=lambda prefix: ({}, {}), run=run)
            child = mock.Mock(); child.poll.return_value = None
            with mock.patch.object(sys, "argv", args), mock.patch.dict(CRASH["main"].__globals__, {"TOOLS": tools}), mock.patch("subprocess.Popen", side_effect=[child, OSError("fixture")]), mock.patch("sys.stdout", new_callable=io.StringIO):
                self.assertEqual(CRASH["main"](), 1)
            child.kill.assert_called_once(); child.wait.assert_called_once_with(timeout=10)
            self.assertEqual(json.loads((root/"crash-result.json").read_text())["exit_code"], 1)


class RetirementTests(unittest.TestCase):
    """现存生产引用和未结束兼容期都必须阻止收缩。"""
    def test_current_source_has_live_dependencies(self):
        value = RETIREMENT["audit"](HERE.parent)
        self.assertTrue(value["scan_complete"]); self.assertGreater(value["runtime_dependencies"], 0)
        self.assertIn("cloud_path", value["candidate_columns"]["song_resources"])
        self.assertNotIn("local_path", value["candidate_columns"]["song_resources"])
        self.assertFalse(value["drop_authorized"])

    def test_contract_candidates_survive_clean_structure_snapshots(self):
        with tempfile.TemporaryDirectory() as name:
            root = Path(name)
            for table in RETIREMENT["TABLES"]:
                directory = root/"sql/mysql"/table
                directory.mkdir(parents=True)
                (directory/"create.sql").write_text("CREATE TABLE fixture(id BIGINT,local_path TEXT);", encoding="utf-8")
            directory = root/"sql/mysql/migrations"
            directory.mkdir()
            source = HERE.parent/"sql/mysql/migrations/V2026102913__retire_resource_cloud_columns.sql"
            (directory/source.name).write_text(source.read_text(encoding="utf-8"), encoding="utf-8")
            columns, indexes = RETIREMENT["candidates"](root)
            self.assertEqual({key: len(value) for key, value in columns.items()}, {"song_resources": 3, "singer_resources": 13, "album_resources": 13, "song_resource_artifacts": 2})
            self.assertEqual(sum(map(len, indexes.values())), 2)
            self.assertFalse(any("local_path" in value for value in columns.values()))

    def test_physical_resource_sql_is_detected(self):
        columns = {"song_resources": ["cloud_backend", "cloud_path"], "singer_resources": ["cloud_status"]}
        cases = [("SELECT r.cloud_path FROM song_resources r", "song_resources", "cloud_path", "qualified_column"),
                 ("INSERT INTO song_resources(id,`cloud_backend`) VALUES(1,'rclone')", "song_resources", "cloud_backend", "insert_column"),
                 ("UPDATE crawler.singer_resources SET cloud_status=1 WHERE id=1", "singer_resources", "cloud_status", "update_column"),
                 ("INSERT INTO song_resources(id) VALUES(1) ON DUPLICATE KEY UPDATE cloud_path='legacy'", "song_resources", "cloud_path", "update_column"),
                 ("SELECT cloud_path FROM song_resources", "song_resources", "cloud_path", "unqualified_column")]
        for sql, table, column, kind in cases:
            with self.subTest(sql=sql):
                rows = RETIREMENT["PHYSICAL"]["physical_references"](sql, columns)
                self.assertIn({"line": 1, "table": table, "column": column, "kind": kind}, rows)

    def test_receipt_json_and_dto_projection_are_not_old_physical_columns(self):
        columns = {"song_resources": ["cloud_backend", "cloud_path"]}
        cases = ["SELECT c.cloud_path FROM resource_cloud_receipts c JOIN song_resources r ON r.id=c.resource_id",
                 "SELECT JSON_OBJECT('cloud_backend',c.source_adapter,'cloud_path',c.cloud_path) FROM resource_cloud_receipts c",
                 "SELECT r.local_path AS cloud_path FROM song_resources r ORDER BY cloud_path",
                 "INSERT INTO resource_cloud_receipts(owner_type,resource_id,cloud_path) SELECT 'song',r.id,r.local_path FROM song_resources r ON DUPLICATE KEY UPDATE cloud_path=VALUES(cloud_path)",
                 "UPDATE resource_cloud_receipts c JOIN song_resources r ON r.id=c.resource_id SET c.cloud_path='known/path'",
                 "-- r.cloud_path is a comment\nSELECT JSON_UNQUOTE(JSON_EXTRACT(a.old_values,'$.cloud_path')) AS cloud_path FROM resource_cloud_migration_audit a",
                 "/* song_resources.cloud_path */ SELECT 'r.cloud_path',\"cloud_backend\" FROM song_resources r"]
        for sql in cases:
            with self.subTest(sql=sql):
                self.assertEqual(RETIREMENT["PHYSICAL"]["physical_references"](sql, columns), [])

    def test_physical_sql_alias_scope_is_resolved(self):
        columns = {"song_resources": ["cloud_path"]}
        scan = RETIREMENT["PHYSICAL"]["physical_references"]
        self.assertEqual(scan("SELECT (SELECT r.cloud_path FROM resource_cloud_receipts r) AS cloud_path FROM song_resources r", columns), [])
        outer = scan("SELECT (SELECT s.cloud_path FROM resource_cloud_receipts c) FROM song_resources s", columns)
        self.assertEqual(len(outer), 1)
        union = scan("SELECT r.cloud_path FROM song_resources r UNION ALL SELECT r.cloud_path FROM resource_cloud_receipts r", columns)
        self.assertEqual(len(union), 1)
        line = scan("/* legacy comment */\nSELECT `r`.`cloud_path` FROM `song_resources` AS `r`", columns)
        self.assertEqual(line[0]["line"], 2)

    def test_physical_metric_does_not_replace_protocol_retirement_gate(self):
        inventory = {"scan_complete": True, "runtime_dependencies": 1, "physical_sql_dependencies": 0, "inventory_sha256": "a"*64}
        result = RETIREMENT["retirement_check"](inventory, {}, {}, NOW)
        self.assertIn("live_dependencies_remain_or_scan_incomplete", result["errors"])
        self.assertFalse(result["drop_authorized"])

    def test_no_attestation_cannot_retire(self):
        inventory = {"scan_complete": True, "runtime_dependencies": 1, "inventory_sha256": "a"*64}
        result = RETIREMENT["retirement_check"](inventory, {}, {}, NOW)
        self.assertFalse(result["preconditions_complete"]); self.assertFalse(result["drop_authorized"])

    def test_ended_window_still_does_not_authorize_drop(self):
        inventory = {"scan_complete": True, "runtime_dependencies": 0, "inventory_sha256": "a"*64}
        attestation = {"format": "crawler.resource-retirement-attestation.v1", "inventory_sha256": "a"*64, "release_scope_sha256": "b"*64,
                       "compatibility_started_at": (NOW-dt.timedelta(days=10)).isoformat(), "compatibility_ends_at": (NOW-dt.timedelta(days=1)).isoformat(),
                       "last_legacy_request_at": (NOW-dt.timedelta(days=11)).isoformat(), "minimum_compatibility_days": 7,
                       "old_clients_remaining": 0, "legacy_writers_remaining": 0, "unresolved_remote_objects": 0,
                       "history_adoption_complete": True, "backup_restore_verified": True, "legacy_projection_export_verified": True, "legacy_projection_export_sha256": "c"*64}
        release = {"format": "crawler.resource-release-check.v1", "evidence_complete": True, "scope_sha256": "b"*64}
        result = RETIREMENT["retirement_check"](inventory, attestation, release, NOW)
        self.assertTrue(result["preconditions_complete"]); self.assertFalse(result["drop_authorized"])
        attestation["compatibility_ends_at"] = (NOW+dt.timedelta(days=1)).isoformat()
        self.assertFalse(RETIREMENT["retirement_check"](inventory, attestation, release, NOW)["preconditions_complete"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
