#!/usr/bin/env python3
"""有界文件/journal 清单与独立目录备份还原；不连接数据库、不清理源文件。"""

from __future__ import annotations

import argparse
import csv
import ctypes
import datetime
import hashlib
import io
import json
import os
from pathlib import Path, PurePosixPath
import stat
import subprocess
import sys
import time


FORMAT = "crawler.resource-file-manifest.v1"


class RehearsalError(Exception):
    """仅向终端返回稳定错误分类，私有路径保留在本地清单。"""


def require(value, code):
    """不把原始 I/O 异常或私有路径写入公共输出。"""
    if not value:
        raise RehearsalError(code)


def canonical(value):
    """同一相对路径、大小、摘要及时间生成固定清单摘要。"""
    return json.dumps(value, ensure_ascii=False, sort_keys=True, separators=(",", ":")).encode("utf-8")


def no_links(path):
    """检查每个现存组件，Windows 同时拒绝 junction 和其他 reparse point。"""
    path = Path(os.path.abspath(path))
    for component in reversed((path, *path.parents)):
        try:
            info = component.lstat()
        except FileNotFoundError:
            continue
        require(not stat.S_ISLNK(info.st_mode), "linked_path_rejected")
        require(not (getattr(info, "st_file_attributes", 0) & 0x400), "reparse_path_rejected")
    return path


def distinct_roots(source, target):
    """源、目标必须完全分离，避免备份递归包含自身或还原覆盖备份。"""
    source, target = no_links(source), no_links(target)
    require(source != target and source not in target.parents and target not in source.parents, "overlapping_roots_rejected")
    return source, target


def private_directory(path):
    """独占创建新目录；复制私有来源前先限制到当前用户与 SYSTEM。"""
    path = no_links(path)
    require(path.parent.is_dir(), "output_parent_required")
    path.mkdir(mode=0o700)
    if os.name == "nt":
        identity = subprocess.run(["whoami.exe", "/user", "/fo", "csv", "/nh"], capture_output=True, check=True, timeout=10, creationflags=subprocess.CREATE_NO_WINDOW)
        sid = list(csv.reader(io.StringIO(identity.stdout.decode("utf-8", errors="replace"))))[0][1]
        require(sid.startswith("S-1-"), "user_sid_invalid")
        result = subprocess.run(["icacls.exe", str(path), "/inheritance:r", "/grant:r", f"*{sid}:(OI)(CI)F", "*S-1-5-18:(OI)(CI)F"], capture_output=True, timeout=10, creationflags=subprocess.CREATE_NO_WINDOW)
        require(result.returncode == 0, "private_directory_acl_failed")
    else:
        path.chmod(0o700)
    return path


def write_json(path, value):
    """元数据同样只能写入新文件，失败现场由操作方保留。"""
    no_links(path)
    with path.open("xb") as output:
        output.write(canonical(value) + b"\n")


class Limits:
    """所有遍历、单文件、总量、深度及执行时间均有上界。"""

    def __init__(self, files=100000, total=1099511627776, single=17179869184, depth=24, seconds=900):
        self.files, self.total, self.single, self.depth = files, total, single, depth
        self.deadline = time.monotonic() + seconds
        require(1 <= files <= 1000000 and 1 <= single <= total <= 1125899906842624 and 1 <= depth <= 64 and 1 <= seconds <= 86400, "limits_invalid")

    def check(self):
        require(time.monotonic() <= self.deadline, "deadline_exceeded")


def opened_path_is_exact(stream, path):
    """Windows 核对已打开句柄的最终路径，拒绝组件切换后越界读取。"""
    if os.name != "nt":
        return
    import msvcrt
    function = ctypes.windll.kernel32.GetFinalPathNameByHandleW
    function.argtypes = [ctypes.c_void_p, ctypes.c_wchar_p, ctypes.c_uint32, ctypes.c_uint32]
    function.restype = ctypes.c_uint32
    buffer = ctypes.create_unicode_buffer(32768)
    length = function(msvcrt.get_osfhandle(stream.fileno()), buffer, len(buffer), 0)
    require(0 < length < len(buffer), "opened_path_unavailable")
    final = buffer.value
    if final.startswith("\\\\?\\UNC\\"):
        final = "\\\\" + final[8:]
    elif final.startswith("\\\\?\\"):
        final = final[4:]
    require(os.path.normcase(os.path.abspath(final)) == os.path.normcase(str(path)), "opened_path_changed")


def signature(info):
    """读取前后比较身份、大小、时间，检测采样期间的替换或写入。"""
    return info.st_dev, info.st_ino, info.st_size, info.st_mtime_ns


def file_digest(path, limits, copy_to=None):
    """按固定缓冲区读取，复制后保留源 mtime；不会覆盖目标。"""
    path = no_links(path)
    before = path.lstat()
    require(stat.S_ISREG(before.st_mode) and before.st_size <= limits.single, "file_type_or_size_rejected")
    flags = os.O_RDONLY | getattr(os, "O_BINARY", 0) | getattr(os, "O_NOFOLLOW", 0)
    digest, count = hashlib.sha256(), 0
    output = None
    try:
        with os.fdopen(os.open(path, flags), "rb") as source:
            opened_path_is_exact(source, path)
            require(signature(os.fstat(source.fileno())) == signature(before), "source_changed_before_read")
            if copy_to is not None:
                no_links(copy_to)
                output = copy_to.open("xb")
            while True:
                limits.check()
                chunk = source.read(1024 * 1024)
                if not chunk:
                    break
                count += len(chunk)
                require(count <= limits.single and count <= before.st_size, "source_grew_during_read")
                digest.update(chunk)
                if output is not None:
                    output.write(chunk)
            require(signature(os.fstat(source.fileno())) == signature(before), "source_changed_during_read")
        require(signature(no_links(path).lstat()) == signature(before) and count == before.st_size, "source_changed_after_read")
    finally:
        if output is not None:
            output.close()
    if copy_to is not None:
        # Python 3.10 Windows 不支持 utime(follow_symlinks=False)，目标属于本轮独占保护根。
        no_links(copy_to)
        os.utime(copy_to, ns=(before.st_atime_ns, before.st_mtime_ns))
        no_links(copy_to)
    return {"size_bytes": count, "sha256": digest.hexdigest(), "mtime_ns": before.st_mtime_ns}


def scan(root, limits):
    """只扫描显式根，隐藏 journal 与临时文件均纳入清单，不跳过异常。"""
    root = no_links(root)
    require(root.is_dir(), "source_root_required")
    files, directories, total = [], [], 0
    pending = [(root, 0)]
    while pending:
        directory, depth = pending.pop()
        limits.check()
        require(depth <= limits.depth, "depth_limit_exceeded")
        with os.scandir(directory) as iterator:
            for entry in iterator:
                limits.check()
                require(len(files) + len(directories) < limits.files, "entry_limit_exceeded")
                path = no_links(Path(entry.path))
                relative = path.relative_to(root).as_posix()
                validate_relative(relative)
                info = path.lstat()
                if stat.S_ISDIR(info.st_mode):
                    directories.append(relative)
                    pending.append((path, depth + 1))
                else:
                    require(stat.S_ISREG(info.st_mode), "special_file_rejected")
                    total += info.st_size
                    require(total <= limits.total, "total_bytes_limit_exceeded")
                    files.append({"path": relative, **file_digest(path, limits)})
    return {"format": FORMAT, "directories": sorted(directories), "files": sorted(files, key=lambda item: item["path"])}


def validate_relative(value):
    """清单不能含绝对路径、ADS、回溯或 Windows 分隔符逃逸。"""
    require(isinstance(value, str) and value and "\\" not in value and ":" not in value and not any(ord(char) < 32 for char in value), "manifest_path_invalid")
    path = PurePosixPath(value)
    require(not path.is_absolute() and all(part not in (".", "..", "") for part in value.split("/")), "manifest_path_invalid")
    return path


def summary(manifest):
    """公共结果只含统计和摘要，源路径与完整清单留在保护目录。"""
    return {"manifest_sha256": hashlib.sha256(canonical(manifest)).hexdigest(), "files": len(manifest["files"]), "bytes": sum(item["size_bytes"] for item in manifest["files"]), "journal_files": sum(item["path"].startswith(".resource-writes/") and item["path"].endswith(".pending") for item in manifest["files"])}


def journal_summary(root, manifest, limits):
    """只统计 journal 所有者和引用存在性；缺失 staging 可能表示对象已经提交。"""
    result = {"owner_counts": {owner: 0 for owner in ("song", "singer", "album", "song_artifact")}, "malformed": 0, "unsafe_references": 0, "current_present": 0, "current_missing": 0, "staging_present": 0, "staging_missing": 0, "previous_present": 0, "previous_missing": 0}
    for item in manifest["files"]:
        if not (item["path"].startswith(".resource-writes/") and item["path"].endswith(".pending")):
            continue
        limits.check()
        if item["size_bytes"] > 16384:
            result["malformed"] += 1
            continue
        path = no_links(root / item["path"])
        with path.open("rb") as source:
            opened_path_is_exact(source, path)
            raw = source.read(16385)
        try:
            lines = raw.decode("utf-8").splitlines()
            require(len(lines) in (5, 6) and all(value.isascii() and value.isdecimal() for value in lines[:2]), "journal_shape_invalid")
            owner = lines[5] if len(lines) == 6 and lines[5] else "song"
            require(owner in result["owner_counts"], "journal_owner_invalid")
        except (RehearsalError, UnicodeError):
            result["malformed"] += 1
            continue
        result["owner_counts"][owner] += 1
        for label, value in zip(("current", "staging", "previous"), lines[2:5]):
            if label == "previous" and not value:
                continue
            try:
                validate_relative(value)
                reference = no_links(root / value)
                result[label + ("_present" if reference.is_file() else "_missing")] += 1
            except RehearsalError:
                result["unsafe_references"] += 1
    return result


def copy_manifest(source, destination, manifest, limits):
    """按固定清单复制，目录、文件均独占创建，永不删除源或覆盖原文件。"""
    for directory in sorted(manifest["directories"], key=lambda value: (value.count("/"), value)):
        validate_relative(directory)
        (destination / directory).mkdir()
    for item in manifest["files"]:
        relative = str(validate_relative(item["path"]))
        actual = file_digest(source / relative, limits, destination / relative)
        require(actual == {key: item[key] for key in ("size_bytes", "sha256", "mtime_ns")}, "copy_source_manifest_mismatch")


def backup(root, output, limits, stopped, scope):
    """需显式停写声明；前后清单一致仍不能独立证明外部 writer 停止。"""
    require(stopped, "writers_stopped_declaration_required")
    source, output = distinct_roots(root, output)
    require(not output.exists(), "existing_output_rejected")
    manifest = scan(source, limits)
    output = private_directory(output)
    payload = private_directory(output / "files")
    copy_manifest(source, payload, manifest, limits)
    require(scan(source, limits) == manifest, "source_manifest_changed")
    require(scan(payload, limits) == manifest, "backup_manifest_mismatch")
    write_json(output / "manifest.json", manifest)
    write_json(output / "provenance.json", {"source_root": str(source), "backup_root": str(output), "finished_at": datetime.datetime.now(datetime.timezone.utc).isoformat(), "scope": scope, "source_observed_unchanged": True, "operator_declared_writers_stopped": True, "external_writers_independently_verified": False})
    result = {"ok": True, "operation": "backup", "scope": scope, "operator_declared_writers_stopped": True, "external_writers_independently_verified": False, "release_authorized": False, "journal": journal_summary(payload, manifest, limits), **summary(manifest)}
    write_json(output / "result.json", result)
    return result


def restore(backup_root, output, limits):
    """还原到全新保护根，核对数据和 journal 的完整清单。"""
    source, output = distinct_roots(backup_root, output)
    require(not output.exists(), "existing_output_rejected")
    manifest_file = no_links(source / "manifest.json")
    require(manifest_file.stat().st_size <= 67108864, "manifest_size_limit_exceeded")
    manifest = json.loads(manifest_file.read_text(encoding="utf-8"))
    require(manifest.get("format") == FORMAT, "manifest_format_invalid")
    actual = scan(source / "files", limits)
    require(actual == manifest, "backup_manifest_mismatch")
    output = private_directory(output)
    copy_manifest(source / "files", output, manifest, limits)
    require(scan(output, limits) == manifest, "restore_manifest_mismatch")
    require(scan(source / "files", limits) == manifest, "backup_changed_during_restore")
    return {"ok": True, "operation": "restore", "release_authorized": False, "missing_files": 0, "mismatched_files": 0, "journal_directory_present": ".resource-writes" in manifest["directories"], "journal": journal_summary(output, manifest, limits), **summary(manifest)}


def self_test(output):
    """离线验证拒绝覆盖、越界、篡改、链接和容量上限；保留全部测试现场。"""
    output = private_directory(output)
    source = private_directory(output / "source")
    (source / ".resource-writes").mkdir()
    (source / ".resource-writes/test.pending").write_bytes(b"1\n7\nobject.bin\nobject.bin.tmp\n\nsong\n")
    (source / ".resource-writes/unsafe.pending").write_bytes(b"2\n7\n../escape\nobject.bin.tmp\n\nalbum\n")
    (source / "object.bin").write_bytes(b"controlled-object")
    (source / "object.bin.tmp").write_bytes(b"controlled-partial-staging")
    (source / "empty-directory").mkdir()
    passed = 0

    def rejected(action):
        nonlocal passed
        try:
            action()
        except (RehearsalError, FileExistsError):
            passed += 1
        else:
            raise RehearsalError("self_test_expected_rejection")

    original = scan(source, Limits())
    backed = backup(source, output / "backup", Limits(), True, "controlled_fixture")
    restored = restore(output / "backup", output / "restored", Limits())
    require(backed["manifest_sha256"] == restored["manifest_sha256"] and restored["journal_files"] == 2, "self_test_roundtrip_failed")
    require(restored["journal"]["owner_counts"]["song"] == 1 and restored["journal"]["unsafe_references"] == 1 and restored["journal"]["current_present"] == 1 and restored["journal"]["staging_present"] == 2, "self_test_journal_statistics_failed")
    passed += 1
    rejected(lambda: backup(source, output / "backup", Limits(), True, "controlled_fixture"))
    rejected(lambda: restore(output / "backup", source, Limits()))
    rejected(lambda: backup(source, source / "nested", Limits(), True, "controlled_fixture"))
    rejected(lambda: backup(source, output / "not-stopped", Limits(), False, "controlled_fixture"))
    rejected(lambda: scan(source, Limits(total=1, single=1)))
    rejected(lambda: validate_relative("../escape"))
    rejected(lambda: validate_relative("object:stream"))
    (output / "backup/files/object.bin").write_bytes(b"tampered")
    rejected(lambda: restore(output / "backup", output / "tampered-restore", Limits()))
    # Windows junction 不要求 symlink 开发者权限；测试链接仍保留在专属现场。
    if os.name == "nt":
        link = output / "junction"
        result = subprocess.run(["cmd.exe", "/d", "/c", "mklink", "/J", str(link), str(source)], capture_output=True, timeout=10, creationflags=subprocess.CREATE_NO_WINDOW)
        require(result.returncode == 0, "self_test_junction_setup_failed")
    else:
        link = output / "link"
        link.symlink_to(source, target_is_directory=True)
    rejected(lambda: scan(link, Limits()))
    require(scan(source, Limits()) == original, "self_test_source_modified")
    return {"ok": True, "operation": "self-test", "tests_passed": passed, "production_files_tested": False}


def main():
    """命令只接收明确路径和上界，不加载 env、访问云端或尝试自动发现磁盘。"""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("operation", choices=("inventory", "backup", "restore", "self-test"))
    parser.add_argument("--root", type=Path)
    parser.add_argument("--output-dir", type=Path, required=True)
    parser.add_argument("--writers-stopped", action="store_true")
    parser.add_argument("--scope", choices=("controlled_fixture", "production_candidate"), default="production_candidate")
    parser.add_argument("--max-files", type=int, default=100000)
    parser.add_argument("--max-total-bytes", type=int, default=1099511627776)
    parser.add_argument("--max-file-bytes", type=int, default=17179869184)
    parser.add_argument("--max-depth", type=int, default=24)
    parser.add_argument("--timeout-seconds", type=int, default=900)
    arguments = parser.parse_args()
    try:
        limits = Limits(arguments.max_files, arguments.max_total_bytes, arguments.max_file_bytes, arguments.max_depth, arguments.timeout_seconds)
        if arguments.operation == "self-test":
            result = self_test(arguments.output_dir)
        else:
            require(arguments.root is not None, "source_root_required")
            if arguments.operation == "backup":
                result = backup(arguments.root, arguments.output_dir, limits, arguments.writers_stopped, arguments.scope)
            elif arguments.operation == "restore":
                result = restore(arguments.root, arguments.output_dir, limits)
            else:
                source, output = distinct_roots(arguments.root, arguments.output_dir)
                require(not output.exists(), "existing_output_rejected")
                manifest = scan(source, limits)
                output = private_directory(output)
                write_json(output / "manifest.json", manifest)
                result = {"ok": True, "operation": "inventory", "scope": arguments.scope, "release_authorized": False, "journal": journal_summary(source, manifest, limits), **summary(manifest)}
                write_json(output / "result.json", result)
        print(json.dumps(result, sort_keys=True))
        return 0
    except RehearsalError as error:
        print(json.dumps({"ok": False, "error": str(error)}))
    except Exception:
        print(json.dumps({"ok": False, "error": "file_rehearsal_failed_private_site_preserved"}))
    return 1


if __name__ == "__main__":
    sys.exit(main())
