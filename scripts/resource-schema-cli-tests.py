#!/usr/bin/env python3
"""维护CLI的无数据库输入边界回归；只运行必定在连接前拒绝的参数。"""
import argparse
import json
import os
from pathlib import Path
import subprocess
import tempfile


def invoke(cli, arguments, environment, cwd):
    """每个原生进程有界且无可见窗口，不继承真实服务或数据库认证。"""
    value = subprocess.run([str(cli), *arguments], cwd=cwd, env=environment,
                           stdin=subprocess.DEVNULL, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
                           timeout=5, creationflags=subprocess.CREATE_NO_WINDOW if os.name == "nt" else 0)
    if len(value.stdout) + len(value.stderr) > 65536:
        raise ValueError("schema_cli_test_output_unbounded")
    return value


def main():
    """仅接受已构建CLI；成功迁移、UUID核验和SQL执行另由独立库验证。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--cli", type=Path, required=True)
    args = parser.parse_args()
    cli = args.cli.resolve()
    if not cli.is_file():
        raise ValueError("explicit_schema_cli_required")
    environment = {key: value for key, value in os.environ.items() if not key.startswith(("CRAWLER_", "MYSQL_"))}
    # 全部连接信息均为不可用夹具；本测试不尝试连接它们。
    environment.update(CRAWLER_MYSQL_DATABASE="fixture", CRAWLER_MYSQL_HOST="invalid.invalid",
                       CRAWLER_MYSQL_PORT="3306", CRAWLER_MYSQL_USER="fixture",
                       CRAWLER_MYSQL_PASSWORD="fixture-private-do-not-print")
    checks = 0
    with tempfile.TemporaryDirectory(prefix="crawler-schema-cli-tests-") as name:
        root = Path(name)
        environment["CRAWLER_ENV_FILE"] = str(root / "no-dotenv")
        environment["CRAWLER_MYSQL_MIGRATIONS_PATH"] = str(root / "missing-migrations")
        help_ = invoke(cli, ["--help"], environment, root)
        assert help_.returncode == 0
        for command in (b"schema-status", b"schema-migrate", b"schema-check-sql"):
            assert command in help_.stdout
        checks += 1
        cases = [
            ["schema-status"],
            ["schema-migrate", "--database", "fixture"],
            ["schema-status", "--database", "other"],
            ["schema-status", "--database", "fixture;DROP"],
            ["schema-status", "--database", "mysql"],
            ["schema-status", "--database", "MYSQL"],
            ["schema-status", "--database", "fixture", "--database", "fixture"],
            ["schema-status", "--database", "fixture", "--unknown", "fixture"],
            ["schema-migrate", "--database", "fixture", "--expected-server-uuid", "bad-uuid"],
            ["schema-status", "--database", "fixture", "--timeout-seconds", "0"],
            ["schema-status", "--database", "fixture", "--timeout-seconds", "3601"],
            ["schema-status", "--database", "fixture", "--timeout-seconds", "1suffix"],
            ["schema-status", "--database", "fixture"],  # 缺少migration目录，连接前拒绝。
            ["schema-check-sql", "--database", "fixture"],
        ]
        for arguments in cases:
            result = invoke(cli, arguments, environment, root)
            assert result.returncode == 1
            assert b"fixture-private-do-not-print" not in result.stdout + result.stderr
            assert b"[MySqlConnectionPool]" not in result.stdout + result.stderr
            assert b"[MySqlMigrationRunner]" not in result.stdout + result.stderr
            checks += 1
    print(json.dumps({"format": "crawler.resource-schema-cli-tests.v1", "checks": checks,
                      "exit_code": 0, "database_commands_executed": 0, "schema_migration_executed": False}))


if __name__ == "__main__":
    main()
