# 配置来源与生效方式

核对日期：2026-10-29；源码基线 `967bc67`，API/OpenAPI `1.9.0`。本文描述服务可执行文件 `demo` 的配置；测试程序可能自行加载测试配置，条件见[验证要求](agents/validation.md)。环境示例见 [.env.example](../.env.example)，启动步骤见[快速启动](getting-started.md)。

## 读取顺序与路径

1. `main.cpp` 先从进程环境读取 `CRAWLER_ENV_FILE`。非空时只尝试加载指定文件，否则尝试加载 `.env`；不会在两份文件之间叠加或自动回退到 `.env.test`。
2. dotenv 加载默认不覆盖进程中已有的非空环境变量。同一文件重复定义同一键时，第一个非空值优先；空字符串视为未设置。
3. 按环境变量构造启动 `Config`；未设置、为空或无法解析的数值/布尔值回退到代码默认值。合法整数不等于业务范围合法，运行设置 API 另有范围校验。
4. `/api/migu/settings` 只更新进程内允许修改的策略，不写回 env 或数据库；重启恢复启动默认值。歌曲处理请求的 `options` 再覆盖该次任务的默认策略。

优先级为：该次请求支持的选项 → 运行期策略 → 启动默认值；启动变量内部为进程环境 → 选定 dotenv 文件 → 代码默认值。两条规则只应用于实际支持这些输入的字段。

从 crawler 模块根目录运行，保证 `sql/mysql/...`、默认 `data/` 和 `logs/` 的含义明确。dotenv/SQL 加载器对相对路径还会从当前目录向上查找最多 8 层，不按源码目录自动兜底；部署时宜为 `CRAWLER_ENV_FILE`、迁移目录、缓存和日志使用明确路径，避免误取其他工作区文件。配置文件不存在时入口仍继续采用已有环境与默认值；因此启动前自行检查文件存在及数据库名，不能把成功启动当作选定文件已加载。

```powershell
$env:CRAWLER_ENV_FILE = (Resolve-Path -LiteralPath .env.test).Path
$env:CRAWLER_MYSQL_DATABASE = 'crawler_ownership_test_quickstart_20261029'
$env:CRAWLER_RESOURCE_CLOUD_UPLOAD_ENABLED = 'false'
```

布尔输入支持 `true/false`、`1/0`、`yes/no`、`on/off`，不区分大小写。dotenv 支持外层引号及可选 `export ` 前缀，不作 shell 求值或变量展开。不要通过打印整个环境或 settings 以外的私有配置排障。

## 启动变量

以下默认值来自[服务入口](../src/main.cpp)、[应用配置](../include/crawler/infrastructure/app/ServerApplication.h)及各 adapter 配置，表示未设置变量时的行为。`.env.example` 为本机示例，会覆盖部分默认值；例如 MySQL I/O/连接上限示例为 2/4，代码默认是 4/16。下列启动变量均需重启生效。

### 服务、持久化与线程

| 变量 | 默认值 | 含义 |
| --- | --- | --- |
| `CRAWLER_ENV_FILE` | 未设置时加载 `.env` | 需在启动进程前设置，不能靠文件内同名键选择文件 |
| `CRAWLER_HOST` / `CRAWLER_PORT` | `127.0.0.1` / `8080` | `main` 的实际绑定默认值；直接构造 Config 的 host 默认不同 |
| `CRAWLER_PERSISTENCE_BACKEND` | `mysql` | 正式 MySQL；`sqlite-debug` 为同步 DAO 调试兼容模式 |
| `CRAWLER_SQLITE_DB_PATH` | `./data/song.db` | SQLite 调试库；不具备正式 MySQL 队列、catalog 和封面服务的完整能力 |
| `CRAWLER_STORAGE_PATH` | `./data` | 本地文件根；数据库保存相对路径，换根需迁移实际文件并复核 |
| `CRAWLER_HTTP_WORKER_THREADS` | `4` | Drogon I/O worker |
| `CRAWLER_ASYNC_WORKER_THREADS` | `4` | 入站 handler 的受限 worker |
| `CRAWLER_RESOURCE_WORKER_THREADS` | `8` | 歌曲/封面处理共享的执行资源，独立于请求并发数 |
| `CRAWLER_PARSE_WORKER_THREADS` | `2` | 解析 worker |
| `CRAWLER_PERSISTENCE_WORKER_THREADS` | `2` | SQLite 调试持久化 worker，不是 MySQL I/O 数 |

### MySQL

| 变量 | 默认值 | 含义 |
| --- | --- | --- |
| `CRAWLER_MYSQL_HOST` / `CRAWLER_MYSQL_PORT` | `127.0.0.1` / `3306` | 连接目标；端口无效时回退默认值 |
| `CRAWLER_MYSQL_USER` / `CRAWLER_MYSQL_PASSWORD` | `root` / 空 | 私有连接凭据；部署按实际账号授权 |
| `CRAWLER_MYSQL_DATABASE` | `crawler` | 库需提前创建；演练必须覆盖成独立测试库 |
| `CRAWLER_MYSQL_INIT_SCHEMA` | `true` | 启动执行未应用 migration；关闭不等于 schema 已兼容 |
| `CRAWLER_MYSQL_MIGRATIONS_PATH` | `sql/mysql/migrations` | 版本化迁移目录，不能用表快照代替升级 |
| `CRAWLER_MYSQL_STARTUP_TIMEOUT_MS` | `30000` | 启动等待 schema 初始化的毫秒数，大库升级可按计划增加 |
| `CRAWLER_MYSQL_IO_THREADS` / `CRAWLER_MYSQL_MAX_POOL_SIZE` | `4` / `16` | MySQL I/O 线程和最大连接数 |
| `CRAWLER_MYSQL_CONNECT_TIMEOUT_SECONDS` | `20` | 单次建连超时 |
| `CRAWLER_MYSQL_RETRY_INTERVAL_SECONDS` | `5` | 建连失败后的重试间隔 |
| `CRAWLER_MYSQL_PING_TIMEOUT_SECONDS` | `5` | 空闲连接健康检查超时 |

账号需要目标库内的业务读写权限，以及启用初始化时相应的 DDL/迁移权限。迁移锁名在同一 MySQL 实例内共用；多个测试服务也应串行初始化。失败处理见[迁移与回滚](operations/service-lifecycle.md#迁移失败与版本回退)。

### 出站 HTTP 与日志

| 变量 | 默认值 | 含义 |
| --- | --- | --- |
| `CRAWLER_OUTBOUND_HTTP_IO_THREADS` | `4` | Asio 出站 I/O 线程 |
| `CRAWLER_OUTBOUND_HTTP_TIMEOUT_MS` | `30000` | 通用出站请求超时；具体资源选项可使用自己的预算 |
| `CRAWLER_OUTBOUND_MAX_RESPONSE_BYTES` | `10485760` | 通用响应上限；未设置时兼容读取 `CRAWLER_RESOURCE_MAX_RESPONSE_BYTES`，后者也未设置才用默认值 |
| `CRAWLER_OUTBOUND_VERIFY_TLS` | `true` | HTTPS 证书链与主机名校验；Windows 只读装载系统 ROOT |
| `CRAWLER_LOG_LEVEL` | `info` | `debug/info/warn/warning/error`；未知值回退 `info` |
| `CRAWLER_LOG_CONSOLE` | `true` | 控制台文本日志 |
| `CRAWLER_LOG_TEXT_FILE_ENABLED` / `CRAWLER_LOG_TEXT_FILE` | `true` / `logs/crawler.log` | 文本文件日志 |
| `CRAWLER_LOG_JSON_FILE_ENABLED` / `CRAWLER_LOG_JSON_FILE` | `false` / `logs/crawler.jsonl` | 独立 JSONL 日志 |

`.env.example` 将响应上限设为 `209715200`，用于较大的歌曲资源；这不改变封面保存器独立的 8 MiB 上限。日志字段及脱敏要求见[批次事件](observability/resource-batch-events.md)。

### 存储模式与封面云端

| 配置 | 当前作用 |
| --- | --- |
| `CRAWLER_RESOURCE_STORAGE_BACKEND=filesystem`（默认） | 歌曲保存本地文件，支持本地 MRC 后处理；封面也始终先保存本地 |
| `CRAWLER_RESOURCE_STORAGE_BACKEND=rclone` | 歌曲旧远端 copyurl 模式，不能代替下面的封面上传开关；没有本地 MRC 来源文件 |
| `CRAWLER_RESOURCE_CLOUD_UPLOAD_ENABLED=false`（默认） | 控制两类元数据封面的本地成功后上传；不接回歌曲自动上传 |
| `CRAWLER_RESOURCE_CLOUD_UPLOAD_BACKEND=baidupcs`（默认）或 `rclone` | 选择一个封面上传 adapter；当前不是两端同时双写 |
| `CRAWLER_RESOURCE_CLOUD_TIMEOUT_MS=120000`（默认） | 每次封面云端尝试及核验预算，用例限制在 `100..600000` 毫秒，重试另计；独立于本地下载超时 |

只需封面本地加云端时，保留 `RESOURCE_STORAGE_BACKEND=filesystem`，开启 `RESOURCE_CLOUD_UPLOAD_ENABLED` 并选择上传后端。两个所有者分别提交 `owner_type=singer` 和 `owner_type=album`；开关不主动扫描或启动任务，仍需调用 `/covers/process`。已下载项默认仅补传，云端失败保留本地成功和可读图片。

| 百度变量 | 默认值 | 用途 |
| --- | --- | --- |
| `CRAWLER_RESOURCE_CLOUD_ROOT` | `/Media` | 百度目标根，追加本地相对路径 |
| `CRAWLER_BAIDUPCS_GO_PATH` | 空 | 已登录的 BaiduPCS-Go 可执行文件，启用百度时必须配置可用路径 |
| `CRAWLER_BAIDUPCS_UPLOAD_PARALLEL` / `CRAWLER_BAIDUPCS_UPLOAD_LOAD` | `8` / `8` | 客户端上传线程数/并行文件数 |
| `CRAWLER_BAIDUPCS_UPLOAD_RETRIES` / `CRAWLER_BAIDUPCS_UPLOAD_POLICY` | `3` / `overwrite` | 客户端重试/策略；封面 adapter 仍先核验同名对象，不盲目覆盖不同内容 |

| rclone 变量 | 默认值 | 用途 |
| --- | --- | --- |
| `CRAWLER_RCLONE_RC_URL` | 空 | 可达 RC 服务基础地址，示例 `http://127.0.0.1:5572` |
| `CRAWLER_RCLONE_RC_USER` / `CRAWLER_RCLONE_RC_PASSWORD` | 空 / 空 | RC Basic Auth，由私有环境提供 |
| `CRAWLER_RCLONE_TARGET_FS` | 空 | 已授权 remote 及根，例如 `gcloud:Media`；封面上传不使用百度根变量 |
| `CRAWLER_RCLONE_API_TIMEOUT_MS` / `CRAWLER_RCLONE_API_MAX_RESPONSE_BYTES` | `10000` / `1048576` | RC 客户端请求超时及响应体上限 |
| `CRAWLER_RCLONE_JOB_TIMEOUT_MS` / `CRAWLER_RCLONE_JOB_POLL_INTERVAL_MS` | `600000` / `2000` | 旧歌曲 copyurl job 等待与轮询；封面 multipart 上传不走该 job 编排 |

启用任一 rclone 模式前需提供 RC_URL、TARGET_FS 及相应授权；封面以 multipart 传递本地快照，不要求 RC 与 crawler 共用磁盘。认证服务的部署由运行环境负责，crawler 不自动启动 RC 或登录网盘。

切换后端或目标根需重启，并重新调用 process 补传。当前资源表只保存一份最后确认的 `cloud_*`，切换会替换该回执，不会删除另一端对象，也不能据此同时判断两端是否已存储。`cloud_backend=baidupcs/rclone` 是上传工具身份；多平台回执和 `cloud_provider=baidu/gdrive` 仍是[后续方案](roadmap/resource-cloud-receipts.md)，没有在当前 schema 实施。上传核验和认领规则见[封面云端专项](roadmap/metadata-cover-cloud-storage.md)。

### 歌词与登录态

`CRAWLER_MIGU_MRC_KEY` 默认未设置；在歌曲本地模式下配置后，成功 MRC 文件及原始 DB 回执确认后生成逐字 JSON/增强 LRC，见[歌词说明](migu/lyrics.md)。修改密钥或启用状态需重启；它不是网盘或 Migu 登录凭据。

pacmtoken 经 API 放入进程内存，重启后需由调用方重新提供。项目不生成登录态；不把 Cookie、token、密钥或完整签名 URL 写进文档、批次或日志。

## 运行期设置和请求选项

`GET /api/migu/settings` 返回 `mutable` 与 `readonly`。`PUT`、`PATCH` 均是部分更新，至少包含一个可变字段；下表来自[运行模型](../include/crawler/application/model/CrawlerRuntimeSettings.h)及 [CrawlerService 校验](../src/application/use_cases/CrawlerService.cpp)。

| 字段 | 默认值 | API 范围 |
| --- | --- | --- |
| `max_batch_concurrency` / `max_page_concurrency` | `4` / `4` | 各 `1..128` |
| `max_retries` | `1` | `0..10` |
| `task_timeout_ms` | `600000` | `1000..86400000` |
| `resource_download_concurrency` | `3` | `1..32` |
| `resource_download_retries` | `1` | `0..5` |
| `resource_download_timeout_ms` | `120000` | `10000..600000` |
| `pacmtoken_refresh_interval_seconds` | `300` | `0..3600`；0 关闭定时刷新 |
| `auto_refresh_pacmtoken` / `refresh_pacmtoken_on_440013` | `true` / `true` | 布尔值 |

更新后应以新任务验证，不承诺改写已提交任务的参数；重启不会保留 API 更新。线程池、持久化后端、MySQL 连接、路径、云端选择和认证均为启动配置，不是热更新字段；不要给未在 `main` 映射的策略猜造环境变量名。

封面 process 自行接收 `concurrency/retries/timeout_ms`，默认 3/1/15000，范围及筛选见[封面 API](api/mg/migu-api.md#歌手头像与专辑封面本地下载)。它使用独立 `MetadataCoverService`，不把 settings 中的歌曲下载默认值或热更新的 `task_timeout_ms` 当作封面设置；封面批次超时由装配时参数固定。云端预算另用启动变量 `CRAWLER_RESOURCE_CLOUD_TIMEOUT_MS`。

源码追踪入口：[Environment](../src/infrastructure/platform/Environment.cpp) → [main](../src/main.cpp) → [组合根](../src/infrastructure/app/ServerApplication.cpp) / [云端装配](../src/infrastructure/app/CloudFileStoreComposition.cpp)；运行快照见 [RuntimeSettingsSnapshotFactory](../src/infrastructure/app/RuntimeSettingsSnapshotFactory.cpp)。
