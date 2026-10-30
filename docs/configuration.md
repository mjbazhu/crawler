# 配置来源与生效方式

核对日期：2026-10-30；源码基线 `83e72e9`，API/OpenAPI `1.10.0`。本文描述服务可执行文件 `demo` 的配置；测试程序可能自行加载测试配置，条件见[验证要求](agents/validation.md)。环境示例见 [.env.example](../.env.example)，启动步骤见[快速启动](getting-started.md)。

当前开发分支 `codex/resource-cloud-receipts` 基于 main `83e72e9`，已接入显式 `parallel` 模式、持久任务、逐端来源和传输 worker；默认仍为 `legacy`，原有单端变量保持原语义。parallel 的本地、百度和 Google 独立认领任务、取得来源并发布；云端不等待本地成功。维护状态、baseline、真实目标身份和来源版本仍是执行前提，开启配置不会自动激活目标，也不会自动扫描全库。具体迁移与未覆盖项见[方案](roadmap/resource-cloud-receipts.md)和[执行说明](operations/resource-transfer-tasks.md)。

## 读取顺序与路径

1. `main.cpp` 先从进程环境读取 `CRAWLER_ENV_FILE`。非空时只尝试加载指定文件，否则尝试加载 `.env`；不会在两份文件之间叠加或自动回退到 `.env.test`。
2. dotenv 加载默认不覆盖进程中已有的非空环境变量。同一文件重复定义同一键时，第一个非空值优先；空字符串视为未设置。
3. 按环境变量构造启动 `Config`；旧变量无法解析时通常回退默认值。parallel 新增预算变量使用严格解析，非法数值、越界、非法开关或平台列表直接拒绝启动，不能把误写的限速静默当成无限速。
4. `/api/migu/settings` 只更新进程内允许修改的策略，不写回 env 或数据库；重启恢复启动默认值。歌曲处理请求的 `options` 再覆盖该次任务的默认策略。

优先级为：该次请求支持的选项 → 运行期策略 → 启动默认值；启动变量内部为进程环境 → 选定 dotenv 文件 → 代码默认值。两条规则只应用于实际支持这些输入的字段。

从 crawler 模块根目录运行，保证 `sql/mysql/...`、默认 `data/` 和 `logs/` 的含义明确。dotenv/SQL 加载器对相对路径还会从当前目录向上查找最多 8 层，不按源码目录自动兜底；部署时宜为 `CRAWLER_ENV_FILE`、迁移目录、缓存和日志使用明确路径，避免误取其他工作区文件。配置文件不存在时入口仍继续采用已有环境与默认值；因此启动前自行检查文件存在及数据库名，不能把成功启动当作选定文件已加载。

```powershell
$env:CRAWLER_ENV_FILE = (Resolve-Path -LiteralPath .env.test).Path
$env:CRAWLER_MYSQL_DATABASE = 'crawler_ownership_test_quickstart_20261029'
$env:CRAWLER_RESOURCE_CLOUD_UPLOAD_ENABLED = 'false'
```

旧布尔输入支持 `true/false`、`1/0`、`yes/no`、`on/off`，不区分大小写；parallel 新开关仅接受小写 `true/false` 或 `1/0`。dotenv 支持外层引号及可选 `export ` 前缀，不作 shell 求值或变量展开。不要通过打印整个环境或 settings 以外的私有配置排障。

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

### legacy 存储模式与封面云端

| 配置 | 当前作用 |
| --- | --- |
| `CRAWLER_RESOURCE_STORAGE_BACKEND=filesystem`（默认） | 歌曲保存本地文件，支持本地 MRC 后处理；封面也始终先保存本地 |
| `CRAWLER_RESOURCE_STORAGE_BACKEND=rclone` | 歌曲旧远端 copyurl 模式，不能代替下面的封面上传开关；没有本地 MRC 来源文件 |
| `CRAWLER_RESOURCE_CLOUD_UPLOAD_ENABLED=false`（默认） | 控制两类元数据封面的本地成功后上传；不接回歌曲自动上传 |
| `CRAWLER_RESOURCE_CLOUD_UPLOAD_BACKEND=baidupcs`（默认）或 `rclone` | 选择一个封面上传 adapter；当前不是两端同时双写 |
| `CRAWLER_RESOURCE_CLOUD_PROVIDER`（默认空） | 声明实际平台 `baidu/gdrive`。baidupcs 可省略，确定为百度；rclone 不能由工具名或 remote 别名推断平台 |
| `CRAWLER_RESOURCE_CLOUD_TIMEOUT_MS=120000`（默认） | 每次封面云端尝试及核验预算，用例限制在 `100..600000` 毫秒，重试另计；独立于本地下载超时 |

只需封面本地加云端时，保留 `RESOURCE_STORAGE_BACKEND=filesystem`，开启 `RESOURCE_CLOUD_UPLOAD_ENABLED` 并选择上传后端。两个所有者分别提交 `owner_type=singer` 和 `owner_type=album`；开关不主动扫描或启动任务，仍需调用 `/covers/process`。已下载项默认仅补传，云端失败保留本地成功和可读图片。

| 百度变量 | 默认值 | 用途 |
| --- | --- | --- |
| `CRAWLER_RESOURCE_CLOUD_ROOT` | `/Media` | 百度目标根，追加本地相对路径 |
| `CRAWLER_BAIDUPCS_GO_PATH` | 空 | 已登录的 BaiduPCS-Go 可执行文件，选择 baidupcs 工具时必须配置；百度选择 rclone 不需要 |
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

已有 rclone 的 RC 地址、认证、TARGET_FS 和超时配置可直接复用，无需另建 remote 或重新登录。例如原 remote 实际存储在百度时，选择 `CRAWLER_RESOURCE_CLOUD_UPLOAD_BACKEND=rclone`，补充 `CRAWLER_RESOURCE_CLOUD_PROVIDER=baidu`，保留原有 `CRAWLER_RCLONE_*`。Google 则声明 `gdrive`。该声明不验证真实账号/网盘身份，部署前仍须核对实际目标；remote 名称不是平台证据。

当前 legacy rclone 存储必须声明 `CRAWLER_RESOURCE_CLOUD_PROVIDER=baidu/gdrive`；缺失或无效时启动装配拒绝，先于歌曲 copyurl 或云端上传，避免对象已写入却无法归属逐端回执。baidupcs 固有归属百度；rclone 不根据工具名猜 Google。变量属于启动配置，需重启生效；不是 settings 热更新字段。V2913 的物理删列与旧 HTTP 字段投影分别处理，见[收缩说明](operations/resource-schema-retirement.md)。

旧 API 的 `cloud_*` 仍只投影一份固定结果，不能据此判断两端是否已保存。parallel 默认继续拒绝旧写入口；显式设置 `CRAWLER_RESOURCE_LEGACY_VIEW_PROVIDER=local|baidu|gdrive` 后，已支持的歌曲/封面处理入口才经统一持久任务执行，并把原客户端等待范围持久投影到旧批次。首次写入将固定视图存入数据库，后续不同配置被拒绝，不能通过重启悄悄改目标。旧查询、停止、只读 preview 和 pacmtoken 设置保留；尚未支持的旧写入口仍返回 `legacy_parallel_unsupported`。legacy 模式的旧合同不变，具体兼容范围以 API 文档为准。

### parallel 三路运行

仅 `CRAWLER_PERSISTENCE_BACKEND=mysql` 与 `CRAWLER_RESOURCE_STORAGE_BACKEND=filesystem` 可以启用。后者选择工厂的正式本地能力；是否实际保存本地由 `LOCAL_ENABLED` 决定。云端使用本端私有临时快照，临时文件不登记为正式 local，失败与取消不影响另一端的快照或任务。

| 变量 | 默认值 | 有效范围与作用 |
| --- | --- | --- |
| `CRAWLER_RESOURCE_PIPELINE_MODE` | `legacy` | 显式 `parallel` 启动独立三路 worker，切换需重启 |
| `CRAWLER_RESOURCE_LOCAL_ENABLED` | `true` | 只控制正式本地任务；关闭后仍可单独启用任一云端 |
| `CRAWLER_RESOURCE_CLOUD_PROVIDERS` | 空 | `baidu`、`gdrive` 或 `baidu,gdrive`；不接受重复、空项或空格 |
| `CRAWLER_RESOURCE_BAIDU_ADAPTER` / `CRAWLER_RESOURCE_GDRIVE_ADAPTER` | `baidupcs` / `rclone` | 百度支持 `baidupcs/rclone`；Google 支持 `rclone` |
| `CRAWLER_RESOURCE_SOURCE_CONCURRENCY` | `6` | `1..32`；来源的全局配额 |
| `CRAWLER_RESOURCE_TRANSFER_CONCURRENCY` | `9` | `1..96`；发布/核验的全局配额 |
| `CRAWLER_RESOURCE_SHUTDOWN_GRACE_MS` | `30000` | `0..600000`；停止新认领后，在途任务完成的宽限 |
| `CRAWLER_RESOURCE_CLOUD_VERIFICATION_POLICY` | `hash_match` | `strict_sha256` 要求真实 SHA-256 核验；不能把远端 MD5 提升为 SHA-256 |

下面 `<TARGET>` 分别为 `LOCAL`、`BAIDU`、`GDRIVE`，各端独立覆盖。

| 变量后缀（前缀 `CRAWLER_RESOURCE_<TARGET>_`） | 默认值 | 范围与作用 |
| --- | --- | --- |
| `CONCURRENCY` | `3` | `1..32`；本端 worker 数 |
| `TIMEOUT_MS` | `120000` | `100..600000`；单次任务预算，含来源描述、listen/token、正文、发布及核验，排队不计时 |
| `RETRIES` | `1` | `0..5`；可恢复错误的额外尝试，结果未决先核对旧意图 |
| `MAX_BYTES` | `536870912` | `1..4294967296`；本次内容上限 |
| `MAX_BYTES_PER_SECOND` | `0` | `0..1099511627776`；本端发布额外限速；BaiduPCS 非零值因客户端不可精确按任务限速而不可用 |
| `STAGING_PATH` | local 为存储根下 `.resource-transfer-staging/local`；云端为空 | 云端必须显式配置私有临时根；建议每端单独目录，不能用另一端正式成功作为来源前置条件 |
| `STAGING_MAX_BYTES` | `8589934592` | `1..1099511627776`；本端来源保留配额，至少容纳一个 `MAX_BYTES` |

来源与传输全局配额均须不少于启用目标数；每个启用端保留一槽，其余按等待端轮转，慢端不能占完所有槽。持久队列容量由数据库三端独立控制，文件及 producer 默认各 10000，使用 queue-status/queue-limits 查看和调整；含 running/waiting_input/uncertain 的占用及满队原子拒绝见[持久队列容量](operations/resource-transfer-tasks.md#持久队列容量)，不新增每实例 `MAX_PENDING` 环境开关。worker 并发和目录需要重启修改；v2 settings 只更新随后新受理任务的超时、重试和字节策略，已受理任务保持冻结值，严格核验策略只能由启动配置设定。

每端 rclone 先完整复制既有 `CRAWLER_RCLONE_*`，再应用可选 `CRAWLER_RESOURCE_<BAIDU|GDRIVE>_RCLONE_RC_URL/RC_USER/RC_PASSWORD/TARGET_FS`，因此已有 WebDAV 登录、原生 Google Drive OAuth remote 和 RC 服务可直接复用。例如百度保留原连接与凭据，仅指定 `BAIDU_ADAPTER=rclone`；Google 若使用 `gdrive:`，仅覆盖 `GDRIVE_RCLONE_TARGET_FS=gdrive:`。两端指向同一实际挂载会同时停用，不允许靠两个 remote 别名伪装双份保存。

目标核验读取实际身份：本地目录对象；BaiduPCS 登录账号；rclone WebDAV 的真实 endpoint/user/root/layout；Google 原生 Drive 的官方账号 permissionId、实际目录 ID 和共享盘 ID。WebDAV 指纹表示网关挂载身份，标准 WebDAV 无法证明底层百度账号；声明 `baidu` 不会被当作账号证明。密码轮换、OAuth token 正常刷新和 remote 别名不改变对应稳定身份。Google 会先经 RC 执行后端只读请求以刷新凭据，再仅在内存读取当前 access_token，通过固定官方 HTTPS 接口前后核验账号及目录；强制证书验证，保留既有出站代理，拒绝重定向，不将认证写入日志。当前仅支持交互 OAuth，服务账号、身份委派及自定义认证端点失败关闭。挂载根缺实际 ID 时只查精确目录；仅缺省 My Drive 空根可解析官方符号根，配置 root_folder_id/team_drive 时不能误用该回退。

启动时核验不可用只关闭对应端的执行能力，历史 skip 仍可返回；该端恢复在线后需重启重新装配。取得来源前、准备/发布前及发布后会复核指纹；发布后的变化或不可确认保留意图进入待核对，不能假称回滚或成功。身份核验成功只证明连接和目标身份，不代表有写权限；例如 WebDAV 可以读取但对 mkdir/uploadfile 返回 403，须提供可写目标或修正挂载权限，不能改用旧路径成功状态放行。

原生 Drive 身份字段依据 [Google User.permissionId](https://developers.google.com/workspace/drive/api/reference/rest/v3/User)、[about.get](https://developers.google.com/workspace/drive/api/reference/rest/v3/about/get) 与 [files.get](https://developers.google.com/workspace/drive/api/reference/rest/v3/files/get)；RC 操作依据 [rclone RC](https://rclone.org/rc/)。登录配置文件存在不等于 RC 已运行，生产必须配置实际可达且已认证的 RC 服务。

取消通过持久任务状态传播到本端等待、listen/token 和正文请求；HTTP 分片只读缓存取消标记，数据库检查不占用网络 I/O strand。关闭先停止受理与新认领，宽限到期请求在途取消；无法及时退出的依赖保持有效，任务意图留待 recover/repair，日志明确报告未完全排空。维护和目标状态不是 settings 开关，必须先完成部署流程再受理写入。

### 歌词与登录态

`CRAWLER_MIGU_MRC_KEY` 默认未设置；legacy 本地模式在 MRC 本地回执确认后生成逐字 JSON/增强 LRC。parallel 默认开启独立持久 producer：受理 MRC 的事务只为本次真实待执行目标登记生成任务，历史 skip/busy 不隐式创建可执行生成计划。生成任务与父 MRC 下载分别认领和计时，不等待正式 local 或父下载成功；父下载已结束仍可以完成生成。

历史 MRC 已跳过而尚无产物时，可对明确的 MRC 资源 ID 提交 `action=produce_artifacts`，只登记生成计划。原 MRC 目标项保持 `skipped/producer_only`，不创建传输占用、不移除历史跳过、不向目标重传 MRC；独立 producer 会读取合法输入并只安排新产物的所选目标。该动作不适用于音频、封面或已被永久限制的来源，普通 `process` 仍遵守原有跳过规则。

| 变量 | 默认值 | 范围与作用 |
| --- | --- | --- |
| `CRAWLER_RESOURCE_PRODUCER_ENABLED` | `true` | parallel 是否登记和执行新的 MRC 生成计划；关闭不删除旧计划 |
| `CRAWLER_RESOURCE_PRODUCER_CONCURRENCY` | `1` | 每目标 `1..4` 个独立 worker，不占父传输 worker |
| `CRAWLER_RESOURCE_PRODUCER_TIMEOUT_MS` | `120000` | `100..600000`；独立输入取得、生成、提交预算 |
| `CRAWLER_RESOURCE_PRODUCER_RETRIES` | `2` | `0..5`；可恢复来源错误的额外尝试，采用持久退避 |

producer 与普通传输共用 `SOURCE_CONCURRENCY` 全局来源配额和每端 `STAGING_MAX_BYTES` 来源快照配额，只独立占用 worker 和自身执行预算。该值不是全部临时文件的总磁盘硬上限：baidupcs 另有同额的私有回读预留，来源快照与回读可同时占用；rclone 没有这份原生工具回读预留。每项 MRC 输入限 1 MiB。缺少有效密钥时持久状态为 `waiting_input`，不会请求来源；补齐配置并重启后可继续认领。解码失败保留 `failed` 和稳定结果码，操作方可明确重试；不会把缺输入或失败当作父 MRC 的错误。处理器版本和导出协议在受理时冻结，升级不能静默改变旧任务的生成规则。

独立 worker 从冻结资源身份重新取得受控 MRC，临时 URL 不入库，不依赖父下载的 workspace、缓存或正式 local 路径；取得后与参考 SHA-256、来源及内容代次核对。完成事务同时核对 producer 自身认领、父目标取消、目标修订和来源版本，再绑定 MRC 参考、登记两产物、创建当前目标子操作及确认 producer 完成，崩溃不会留下已生成但漏排的中间提交。提交响应不确定时保留持久状态供恢复核对，不能用失败回写覆盖可能已提交的结果。

每 30 秒进行一次有界过期认领恢复，当前统一阈值为 660 秒，超过允许的最大执行预算 600 秒并预留收尾时间；重新认领推进 producer 代次，迟到完成被拒绝。生成任务和子操作可按父操作查询与取消，取消只影响选定目标。服务关闭统一停止新认领并应用宽限，producer 与普通传输分别收尾。

三端共用纯内容缓存（最多 16 份，每份最多 4 MiB，总正文最多 64 MiB，另有在途有界副本）；缓存只优化解码，不承担持久性。每个持久 producer 可以独立请求 MRC，淘汰或重启后可以重新取得并再生，不能保证整个历史批次只读取一次来源。已登记产物的本地文件存在时优先核对和读取；缺文件时按原 MRC SHA-256、处理版本及产物参考重建，不伪造正式 local 成功。原 MRC 成功不代表 producer 或派生传输已经完成。默认 producer 开启且缺 key 时，直接 MRC 任务可成功，但 v2 父操作仍为 running；可补齐密钥重启，或按选定目标取消后续工作。旧兼容批次不因首次缺 key 的计划而等待。修改密钥或启用状态需重启；密钥不进入任务、日志或接口。
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

历史发现后的逐端人工采信、missing明确正常策略及双端导入控制见[单端采信](operations/resource-history-adoption.md)；旧writer的非等价迁移指导及删除前capture/只读预演见[兼容与清理](operations/resource-compatibility-cleanup.md)。本轮不新增云端认证环境变量，不自动删除远端对象。
