# Windows 快速启动

适用日期：2026-10-30；当前实施分支 `codex/resource-cloud-receipts`，API/OpenAPI `1.14.0`。目标是用独立 MySQL 库完成构建、启动、只读最小调用和停止；首次演练关闭云端，不触发平台采集或真实下载。配置含义统一见[配置说明](configuration.md)，恢复及备份见[服务运行与恢复](operations/service-lifecycle.md)。

本分支的新库升级至 V2026102913 并保持维护关闭：以下默认 legacy 只读演练仍适用，写请求预期为 `503/resource_maintenance`，正式本地根不会仅因启动而创建。V2913 删除31个旧物理云列及2索引，云读写转到统一回执；旧 HTTP JSON 和三张 legacy 映射表保留。已有库先按[独立数据库迁移](operations/resource-database-migration.md)备份、克隆核对后升级，实际 `crawler` 状态以该记录为准。工具已提供目标探测和基线 review/seal/activate；按[维护说明](operations/resource-cloud-import.md)演练，不能手工改 active/baseline_ready 绕过门槛。parallel 使用 v2 接口；显式固定 legacy view 后可桥接旧歌曲/封面 process，已知元数据、补建和治理入口保留原语义并接入持久准入，未知写入口仍拒绝；retry/reset 不自动入队或解除历史 skip；切换条件见[方案](roadmap/resource-cloud-receipts.md)。

## 准备工具和工作目录

- Windows x64、MSVC C++20 开发环境、Windows SDK、Ninja，以及支持 preset version 6 的 CMake 3.25 或更高版本。
- vcpkg 的 `x64-windows` 依赖可被 CMake 找到：Drogon、Boost.Asio、Boost.MySQL、OpenSSL、Zlib、nlohmann_json、SQLite3。Boost.MySQL 需提供 `pipeline_request/any_connection::async_run_pipeline`，用于入队中的只读查询对；本轮构建验证版本为 Boost 1.90。依赖来源是 [CMakeLists.txt](../CMakeLists.txt)；本仓库没有自动安装依赖的 manifest。
- 可达的 MySQL 8.0，以及能创建独立演练库和执行其 migration 的账号。运行程序不自动创建数据库。

使用 Developer PowerShell for VS，进入 crawler 模块根目录；下面的 `C:/work/crawler`、`D:/crawler-build` 都是需按实际位置替换的示例。不是从 `Debug` 目录切换过去直接运行 exe。

```powershell
Set-Location -LiteralPath 'C:/work/crawler'
git branch --show-current
git status --short
cmake --version
ninja --version
```

源码工作应在功能分支；命令中的 build 目录必须属于本 checkout，不能复用其他 worktree 的 CMake cache。运行时需要整个 `sql/` 及适配的依赖 DLL，单独复制 `demo.exe` 不构成完整部署。

## 配置与构建

[现有 preset](../CMakePresets.json) 固定引用 `D:/curl/vcpkg`。路径相同可直接执行：

```powershell
$taskBuild = 'D:/crawler-build'
cmake --preset msvc-vcpkg -B $taskBuild
if ($LASTEXITCODE -ne 0) { throw 'CMake configure failed' }
cmake --build $taskBuild --target demo MiguApiDocumentTest -j 4
if ($LASTEXITCODE -ne 0) { throw 'Build failed' }
ctest --test-dir $taskBuild -R '^MiguApiDocumentTest$' --output-on-failure
if ($LASTEXITCODE -ne 0) { throw 'Offline API check failed' }
```

vcpkg 路径不同时，在本机 `CMakeUserPresets.json` 建立覆盖，不修改共享 preset。示例：

```json
{
  "version": 6,
  "configurePresets": [{
    "name": "local-msvc",
    "inherits": "msvc-vcpkg",
    "binaryDir": "D:/crawler-build",
    "environment": {
      "VCPKG_ROOT": "D:/tools/vcpkg",
      "VCPKG_INSTALLED_DIR": "D:/tools/vcpkg/installed"
    },
    "cacheVariables": {
      "CMAKE_TOOLCHAIN_FILE": "D:/tools/vcpkg/scripts/buildsystems/vcpkg.cmake",
      "VCPKG_INSTALLED_DIR": "D:/tools/vcpkg/installed"
    }
  }]
}
```

此时用 `cmake --preset local-msvc` 配置，再执行上面的 build/CTest。该文件只用于本机，不随业务提交。输出固定在 `$taskBuild/Debug/`。缺少包时先安装匹配 triplet 的依赖；缺少 DLL 时核对该次构建的 app-local 依赖及 Debug/Release，避免混用其他工具链的文件。

大体积 Debug 链接遇到磁盘/PDB 限制时，先确保空间；必要时仅在本地 cache 添加 `-DCMAKE_EXE_LINKER_FLAGS_DEBUG="/DEBUG:NONE /INCREMENTAL:NO"` 并降低 `-j`，记录调试符号缺失条件，不改共享配置。这是本项目已有环境变通，不是启动必需选项。

## 建立独立数据库和私有配置

在同一个 Developer PowerShell 中选唯一测试库名。以下 MySQL 客户端位置、主机及账号须与私有配置一致，`-p` 交互输入密码：

```powershell
$taskDatabase = 'crawler_ownership_test_quickstart_' + (Get-Date -Format 'yyyyMMdd_HHmmss')
& 'C:/Program Files/MySQL/MySQL Server 8.0/bin/mysql.exe' --host=127.0.0.1 --port=3306 --user=root -p "--execute=CREATE DATABASE $taskDatabase CHARACTER SET utf8mb4;"
if ($LASTEXITCODE -ne 0) { throw 'Create isolated database failed' }
if (-not (Test-Path -LiteralPath .env.test)) {
    Copy-Item -LiteralPath .env.example -Destination .env.test
}
```

编辑本机 `.env.test`，填入正确的 MySQL 主机、端口、账号和密码；不提交该文件。不要沿用示例中的运行库 `crawler`。启动前通过进程变量明确覆盖库名及本次操作边界：

```powershell
$env:CRAWLER_ENV_FILE = (Resolve-Path -LiteralPath .env.test).Path
$env:CRAWLER_MYSQL_DATABASE = $taskDatabase
$env:CRAWLER_MYSQL_INIT_SCHEMA = 'true'
$env:CRAWLER_MYSQL_STARTUP_TIMEOUT_MS = '600000'
$env:CRAWLER_PERSISTENCE_BACKEND = 'mysql'
$env:CRAWLER_HOST = '127.0.0.1'
$env:CRAWLER_PORT = '19129'
$env:CRAWLER_STORAGE_PATH = "D:/crawler-runs/$taskDatabase/images"
$env:CRAWLER_RESOURCE_STORAGE_BACKEND = 'filesystem'
$env:CRAWLER_RESOURCE_CLOUD_UPLOAD_ENABLED = 'false'
$env:CRAWLER_OUTBOUND_VERIFY_TLS = 'true'
$env:CRAWLER_LOG_TEXT_FILE = "D:/crawler-runs/$taskDatabase/service.log"
```

当前进程内已有的其他 MySQL 连接变量也优先于文件；核对它们的来源，避免连错实例。端口须未被占用。首次启动按升序执行未应用 migration；同一 MySQL 实例上的其他初始化任务应等待本次完成。迁移已执行的文件不能改字节，结构查询以迁移和[SQL 说明](../sql/mysql/README.md)为准。

仅完善数据库时，到这里即可改走[独立数据库迁移](operations/resource-database-migration.md)的 `schema-status/schema-migrate/schema-check-sql`，无需启动下述服务或 worker。

## 启动和最小调用

在原终端前台启动，保留它用于 Ctrl+C 停止：

```powershell
& "$taskBuild/Debug/demo.exe"
```

等待初始化完成及监听日志后，在第二个 PowerShell 中执行：

```powershell
$taskBase = 'http://127.0.0.1:19129'
Invoke-RestMethod "$taskBase/api/health"
Invoke-RestMethod "$taskBase/api/migu/schema/status"
Invoke-RestMethod "$taskBase/api/migu/settings"
Invoke-RestMethod "$taskBase/api/migu/singers?page=1&page_size=1"
Invoke-RestMethod "$taskBase/api/migu/covers/queue?owner_type=singer&page_size=1"
(Invoke-RestMethod "$taskBase/api/openapi.json").info.version
```

预期：接口为 HTTP 200，health 表示存活，schema 返回 `ok=true` 且最新可用版本为 `2026102913`，settings 包含 `mutable/readonly`，新库的歌手和封面列表为空，OpenAPI 版本为 `1.14.0`。health 不能替代数据库/schema 检查。没有预置数据时空列表是正常结果，不需要触发真实采集才能验收启动。

需要核对热更新时，先保存原值，再更新并恢复：

```powershell
$taskOriginal = (Invoke-RestMethod "$taskBase/api/migu/settings").mutable.max_retries
Invoke-RestMethod "$taskBase/api/migu/settings" -Method Patch -ContentType 'application/json' -Body '{"max_retries":0}'
Invoke-RestMethod "$taskBase/api/migu/settings" -Method Patch -ContentType 'application/json' -Body (@{ max_retries = $taskOriginal } | ConvertTo-Json -Compress)
```

回到运行终端按 Ctrl+C，等待进程退出并确认该端口不再监听。再用同一命令启动，可验证 migration 重跑跳过及同一库可读；不应重新生成业务数据。停止和遗留任务处理见[服务生命周期](operations/service-lifecycle.md#正常停止与任务取消)。

## 接续真实业务

本机闭环通过后，按[元数据流程](migu/metadata.md#当前采集入口)逐阶段采集，等待批次结束。歌曲资源由 `/resources/process` 处理，调用方另行提供 pacmtoken；封面从已落库 URL 经 `/covers/process` 下载，两个所有者分别提交。云端配置和已下载文件补传见[配置说明](configuration.md#存储模式与封面云端)及[封面云端专项](roadmap/metadata-cover-cloud-storage.md)。

这些调用会请求外部平台并写资源，不属于前述最小启动演练。不要在未确认数据范围时自动执行采集或把真实平台 CTest 当作离线检查。

## 本次演练范围

本指南命令来源已逐项对照源码；本轮实际演练结果、独立库名及日志在[文档校准与指南验收](operations/service-lifecycle.md#文档校准与指南验收2026-10-29)记录。已有工具链上的构建、配置与本机运行演练不等于从空白机器安装依赖或全部恢复场景验收。
