# 统一资源上线准备与证据门禁

本工具把[运行与恢复](service-lifecycle.md)、[历史补建](resource-history-discovery.md)和[持久任务](resource-transfer-tasks.md)的上线条件变成可执行检查。当前功能基线为 schema V2026102912、API 1.14.0。它不迁移正式 `crawler` 库、不启动真实采集或网盘传输，不自动宣布已通过生产验收。固定清单第 3 项的交付是可运行的准备、演练和失败关闭门禁；真实环境执行结果必须另行取得。

## 工具与执行顺序

仅依赖 Python 3.10+ 标准库。MySQL 演练另需明确指定 MySQL 8 的 `mysql`、`mysqldump`，两端实例须支持当前 schema。先在现有模块根目录运行：

```powershell
python scripts/resource-release-tests.py
python scripts/resource-release-check.py template --output '<新的计划文件.json>'
```

模板包含未知值，默认无法放行。填写并审阅 `scope`：release_id、源码 revision（40 位提交摘要）、source_manifest_sha256（包含未提交修改和新增文件的源清单摘要）、cli_sha256、server_sha256、schema_version（至少 V2026102912）、数据库身份 SHA-256、基线摘要、local/baidu/gdrive 的三个不同真实指纹及其修订。库身份算法与导入一致，为 `SHA256(server_uuid + ':' + DATABASE())`。单独 HEAD 不能证明当前构建内容；CLI 演练会核对实际文件摘要。计划用于一轮明确范围的验收，换库、换目标、推进修订或变更构建后重建范围，不能复用旧证据。

`limits` 必须在测量前填写：最小数据量、至少 30 个样本、查询/锁等待 p95、最低入队吞吐、RSS/临时盘峰值、恢复时长、停写观测窗口和证据最大年龄。模板数据量为 1000 万行；调整须与部署规模要求一起审阅，不能测完再放宽阈值。`not_before` 带时区，阻止把前阶段日志混成当前结果。

历史本地文件的默认策略为 `scope.history_local_files=verify`，省略字段的旧计划继续按此要求检查，原摘要算法不补默认字段。按本轮用户范围可显式改为 `preserve_paths_only`，但必须填入 `scope.local_path_manifest_sha256`。此时只有 `file_restore` 从必需场景排除，结果单列 `excluded_evidence/status=excluded_not_tested`，不生成通过证据；其余八类继续检查。`clone_upgrade` 必须同时提供 `historical_local_paths_preserved=true`、`local_path_manifest_sha256_before/after` 与 scope 相同，以及非负整数且相等的 `local_paths_count_before/after`。策略与路径摘要进入 scope，变更后不能复用此前证据。该范围不免除数据库还原、新任务恢复或未决对象治理。

创建本轮证据父目录，各场景输出到其中的新子目录。检查支持 `<evidence-dir>/<kind>.json` 或 `<evidence-dir>/<kind>/<kind>.json`，同 kind 同时存在两份会拒绝。证据文件中的 `artifacts` 路径相对于该记录所在目录，禁止越界、链接至外部和覆盖已有文件。

```powershell
python scripts/resource-release-check.py check --plan '<已审阅计划.json>' --evidence-dir '<本轮证据目录>' --output '<新的检查结果.json>' --timeout-seconds 60
```

返回码 0 表示所需证据内部一致且全部条件满足；2 表示缺失或未通过；1 表示输入/输出/依赖错误。即便通过，结果仍为 `production_authorized=false`、`activation_performed=false`、`evidence_authenticity_independently_verified=false`。它检查操作方提供的证据及文件摘要，不能独立证明那些外部操作真的发生，也不替代人工审阅原始执行记录。

## 隔离库演练

`resource-release-rehearsal.py` 只接受 `crawler_ownership_test_release_` 加 1..24 个小写字母/数字/下划线的库名，绝不接受 `crawler` 或其他已有业务库名。数据库须已显式创建并初始化；工具不创建、DROP 或覆盖数据库。认证只来自当前进程的四个 `CRAWLER_RELEASE_SOURCE_MYSQL_HOST/PORT/USER/PASSWORD` 环境变量，不接受密码命令参数、不加载或复制 `.env`。不要把这些值写入仓库或命令日志。

开始及结果落盘前通过原生 mysql 再核对实际库身份、schema 最新版本、三端指纹和修订；与计划不一致立即拒绝。查询客户端使用 `--no-defaults` 配合独立不存在的 `MYSQL_TEST_LOGIN_FILE`（兼容 MySQL 8.0；不加载默认 login 文件） 和显式 TCP 参数，CLI 子进程明确选择同一隔离库并关闭 schema 自动初始化，dotenv 指向本次新目录内不存在的文件。原始 native stdout/stderr 不打印；JSON 元数据和输出大小均有上界，备份正文仅写新文件。单次和整轮超时分别限制，Windows 子进程不打开可见窗口。

只读队列查询基准：

```powershell
python scripts/resource-release-rehearsal.py benchmark --database '<白名单隔离库>' --plan '<计划.json>' --cli '<crawler_cloud_receipts.exe>' --mysql '<mysql.exe>' --output-dir '<证据目录>/queue_status_benchmark' --samples 30 --concurrency 2 --timeout-seconds 30 --total-timeout-seconds 600
```

输出完整 `queue-status` CLI 调用的 p50/p95/max、失败数和逐样本日志，样本 5..1000、并发 1..8。它包括进程启动与连接耗时，**不能当作 SQL 延迟、入队吞吐、千万级性能或多实例传输验收**，因此不会自动填满 `scale_workload` 场景。

隔离库显式恢复：

```powershell
python scripts/resource-release-rehearsal.py recover --database '<白名单隔离库>' --plan '<计划.json>' --cli '<crawler_cloud_receipts.exe>' --mysql '<mysql.exe>' --output-dir '<证据目录>/queue_recovery' --destination baidu --stale-seconds 660 --writers-stopped
```

先确认所有实例/脚本已停，再使用大于实际最长认领预算的阈值；`--writers-stopped` 仅为操作者声明，不是停止证明。工具保存恢复前后占用与正式 CLI 返回结果，不下载/上传，不把空队列恢复算成强杀恢复测试。它不自动生成 `multi_instance_crash` 通过证据。

备份还原演练：

```powershell
python scripts/resource-release-rehearsal.py backup-restore --database '<白名单隔离库>' --plan '<计划.json>' --mysql '<mysql.exe>' --mysqldump '<mysqldump.exe>' --output-dir '<证据目录>/database_restore' --timeout-seconds 300 --total-timeout-seconds 1800 --max-backup-bytes 21474836480 --writers-stopped
```

另需四个 `CRAWLER_RELEASE_RESTORE_MYSQL_HOST/PORT/USER/PASSWORD` 环境变量。源与恢复库使用**相同逻辑库名，但必须属于不同 `server_uuid` 的隔离 MySQL 实例**；恢复库必须已经存在且表/视图/存储例程/事件全空。源库含事件时明确拒绝，当前工具不支持恢复事件，不能静默遗漏。这样视图中的原逻辑库限定名也不会指回另一个库。视图若已经引用其他 schema，工具拒绝演练；DDL 预览也检查跨库引用、USE、账号/库和全局设置修改，MySQL 可执行版本注释不会被当成普通注释漏掉。备份前后 DDL 摘要变化会在导入前拒绝。

工具使用固定 mysqldump 选项生成自己的新备份，不接受用户提交的任意 SQL 文件。`backup.sql` 包含表、数据、视图及存储例程，触发器独立保存在 `triggers.sql`；恢复必须先导入完整 backup，再安装触发器，并同时保留这两个备份文件。大表数据按表使用事务恢复，避免逐行自动提交。还原后比较按主键输出的数据摘要、migration/version/checksum 清单和重新导出的触发器定义，记录新库身份及 `trigger_definitions_verified/triggers_installed_after_data`；缺少触发器核验的旧报告不能通过新门禁。源数据表缺少稳定唯一顺序或客户端/服务端输出差异可导致保守失败，须调查，不手工改 JSON 为通过。失败可能留下本次新恢复库的部分结构/数据和 `.sql` 文件；工具不清空重试，需选择另一个明确空恢复实例/库并人工处理失败现场。

此工具只还原数据库，`files_or_journal_restored=false`。默认范围的正式备份还需静止的文件根、journal、版本、配置与网盘未决对象清单，文件恢复结果单独提供 `file_restore` 证据。显式 `preserve_paths_only` 只排除历史本地文件验收，仍须保留当前版本、配置和未决意图；不能借此删除旧文件、journal 或原路径。新实例的库身份不同，原导入/基线范围不能直接当成新实例的有效激活依据；按正式目标流程重新核对。

历史路径的数据库元数据快照使用同一组 `CRAWLER_RELEASE_SOURCE_MYSQL_*` 环境变量，只允许上述白名单隔离库，并要求预期实例 UUID：

```powershell
python scripts/resource-local-path-manifest.py --mysql '<mysql.exe>' --database '<白名单隔离库>' --expected-server-uuid '<明确隔离实例UUID>' --writers-stopped --output '<新的before.json>'
python scripts/resource-local-path-manifest.py --mysql '<mysql.exe>' --database '<同名升级或恢复库>' --expected-server-uuid '<明确目标实例UUID>' --writers-stopped --reference-manifest '<before.json>' --output '<新的after.json>'
```

工具冻结 song/singer/album/song_artifact 四 owner 的原 id 高水位，只从数据库读取 owner/id/路径字节的 SHA-256，输出行数和规范清单摘要；不打开任何历史资源文件。复核沿用原高水位，新增测试资源不会扩展历史范围。路径删除、改变或范围内新增非空路径均改变摘要；原来不存在且高水位为 0 的派生表允许后续创建。返回码 2 表示与参考不一致；元数据验证失败返回 1。`--writers-stopped` 是操作者声明，不能代替实际停写观察。此工具不直接查询正式库；正式迁移还需按批准导出/克隆流程在对应副本取得前后摘要。

云端尚未登录时，可以先做数据库独立演练：只读导出旧库，改写并核对 DDL 的库限定引用，在独立实例的白名单克隆库应用正式 migration，再向另一实例的同名空库恢复。只记录实际库身份、版本、行数、数据及触发器摘要；未知的云端指纹与基线不能用合成值补齐。此阶段记录必须标注 `release_scope_bound=false/release_authorized=false`，不能伪装成已经绑定三端目标的整轮发布证据，正式 `--plan` 门禁保持原要求。源库前后 DDL/行数相同只能证明这些观测一致，不等于所有外部 writer 已停。

## 两进程仓储强杀预演

`MySqlResourceCrashProbe` 是由父进程驱动的探针，不单独注册为完整 CTest。构建此 target 后，使用相同四个 `CRAWLER_RELEASE_SOURCE_MYSQL_*` 变量运行：

```powershell
python scripts/resource-crash-rehearsal.py --database 'crawler_ownership_test_crash_<本轮新后缀>' --probe '<MySqlResourceCrashProbe.exe>' --output-dir '<全新隔离证据目录>'
```

库名严格限定为 `crawler_ownership_test_crash_` 加 1..24 个小写字母、数字或下划线。setup 创建空测试库并应用正式 migration，登记两个歌手封面 fixture；仅 setup 初始化 schema，两个 worker 直接连接，避免并发争用迁移锁。合成目标和测试基线只存在此空库，不代表正式目标身份。父进程不加载 dotenv，不 DROP 或覆盖数据库；失败需换新库/新目录，保留现场。

父进程启动两个独立 native 进程，由真实仓储分别认领、登记 attempt，写入三字节本地对象；等两份原子就绪记录后，通过所持 Popen 句柄强杀两个进程。等待真实租约过期，再执行 recover，断言两个对象成为 uncertain、独占槽保留且不能自动重传。逐项核对原文件后显式 repair；恢复前的认领在修复前后均不能确认，原文件 SHA-256、mtime 与对象数量保持一致。异常退出时也只终止本轮持有句柄的子进程。

`crash-result.json` 区分 `execution=actual_processes_and_mysql_fixture` 与正式发布范围，并明确 `service_workers_tested=false`、`synthetic_targets=true`、`release_scope_bound=false`。这比在同一进程修改 claimed_at 更直接地验证崩溃后的持久围栏，但**没有运行服务 worker、真实云端上传或生产 journal 恢复**，不能直接填作已绑定三路目标的完整 `multi_instance_crash/cloud_live/file_restore` 放行证据。

## 完整服务和真实云端预演

`ResourceTransferServiceProbe` 只辅助布置/观察空隔离库，实际下载、发布和修复由两个正式 `demo` 进程完成。仍使用上述四个 `CRAWLER_RELEASE_SOURCE_MYSQL_*` 变量，库名必须是新的 `crawler_ownership_test_service_*`：

```powershell
python scripts/resource-service-rehearsal.py --database 'crawler_ownership_test_service_<本轮后缀>' --probe '<ResourceTransferServiceProbe.exe>' --demo '<demo.exe>' --output-dir '<全新证据目录>' --io-root '<全新短文件根>'
```

内容来源仅为本机生成 PNG；测试库专属完成回执屏障使完整文件/journal 已发布而回执尚未确认，再精确终止父进程持有的 demo。两个进程先各自持有认领，发布屏障依次执行，避免同一目标状态行锁使第二个 attempt 无法开始。使用真实过期时间 recover，重启后不得重新获取内容，再经 HTTP 显式 repair 并核对旧认领拒绝、原文件 SHA/mtime 和独占槽释放。Windows 建议短 `--io-root`，避免测试目录深度叠加临时后缀超过文件路径限制；日志仍留在指定证据目录。该场景只验证本地服务，云端结果须另行取得。

真实网盘由 `ResourceCloudLiveProbe` 和显式驱动执行，不注册默认 CTest：

```powershell
python scripts/resource-cloud-live-rehearsal.py --probe '<ResourceCloudLiveProbe.exe>' --rclone '<rclone.exe>' --rclone-config '<已登录rclone.conf>' --output-dir '<全新公开证据目录>' --private-dir '<全新受保护目录>' --allow-cloud-write
```

不带 `--allow-cloud-write` 时只核验目标。驱动只向全新独占前缀写入生成的小文件；使用配置保护副本启动本轮认证 loopback RC，保留原配置，最终仅停止本轮持有句柄的 RC。完整模式执行三路成功、取消百度、取消 Google 三轮；每端独立时间/来源快照/限速，成功对象逐一完整 SHA-256 回读，取消从实际上传正文开始后计时并保留未决意图，不自动删除对象。云端开始须早于本地完成，云端也先完成仅是额外观察，不是并行成立的必要条件。`first_upload_body` 表示发往 RC 的正文起点，不冒称可以直接测量网盘内部 Put 时刻。

一端外部不可写时，可显式 `--only-gdrive` 获取 Google 单端证据；该模式将百度标为未测，`three_way_passed=false`，不能代替完整三轮。可读目标的 mkdir/uploadfile 若返回 403，先修正明确挂载的写权限，不探测/猜测其他私人目录，不绕过权限。失败、中间成功和取消意图均保留在独立证据目录，不能覆盖旧报告为通过。

## 百万行背景库的受控规模预演

`ResourceScaleProbe` 是手动 target；由 `scripts/resource-scale-rehearsal.py` 分为 prepare 和 measure 两阶段。当前工具故意限定本轮已核验的 6111 实例 UUID 与原库、scale4/scale4b/scale4c 四个精确库名，不是通用生产压测入口。prepare 在任何受理测量前封存负载、阈值、探针二进制与三份工具源码摘要，确认没有其他客户端、新治理表无已有任务后，仅追加专属 Singer fixture。原 4,439,979 行背景数据（其中 1,505,955 条歌曲资源）、旧路径及不可变备份保持保留；不扫描历史文件，不调用音乐源或真实网盘。

```powershell
python scripts/resource-scale-rehearsal.py self-test
python scripts/resource-scale-rehearsal.py prepare --database 'crawler_ownership_test_release_1030_331763' --run-id '<8..24位小写字母数字>' --probe '<ResourceScaleProbe.exe>' --output-dir '<全新prepare证据目录>' --private-dir '<全新受保护prepare日志目录>' --writers-stopped
python scripts/resource-scale-rehearsal.py measure --prepared-dir '<已封存prepare目录>' --probe '<同一ResourceScaleProbe.exe>' --output-dir '<全新measure证据目录>' --private-dir '<全新受保护measure日志目录>' --exclusive-window --writers-stopped
```

连接只接受显式 `CRAWLER_SCALE_MYSQL_HOST/PORT/USER/PASSWORD`，不加载 dotenv。measure 前停止其他构建、测试和探针，由主执行者给出独占窗口；准备阶段重 IO 不计入测量。首轮工程基线固定为并发 1/2/4、每 worker 30 次，共 210 次真实 `MySqlResourceTransferJobs.enqueue`，每次 10 个新资源×3 端，形成每端 2,100 个未消费任务。随后测 4 个请求争 1 个剩余容量、满队幂等重放、三端超限整笔回滚；这不表示默认 10,000 容量附近的完整持续负载已经验证。

每档分别记录 enqueue、真实 jobs.find、新 Singer 状态及原歌曲首/中/尾 ID 状态的 p50/p95/max。测前门槛：查询各类 p95≤250ms、enqueue p95≤1,000ms、热点行锁获取总耗时 p95≤500ms、混合场景每档≥2受理请求/秒、非预期失败0、native 探针 OS 峰 RSS≤512MiB、实测 staging 峰≤64MiB。吞吐包含实际 FileStorage 新样本 staging、三类查询及热点锁探测开销；锁指标包括 SQL/网络，只能称锁获取时延上界，不能称纯 InnoDB 等待分布。每 worker 持有三端各 1MiB 未提交样本，边界及 5ms 周期扫描真实文件大小；结束显式 abort，不发布历史文件。RSS 是 OS 工作集高水位，仅涵盖 native 探针，不包含 MySQL 或驱动。

结果为 `controlled_fixture/release_scope_bound=false`，不得直接改名填作正式 `scale_workload`。这不是千万行、完整服务 worker 或真实云端吞吐证明，也不是生产 SLA。实际结果与局限见本页阶段记录；不在测后放宽封存阈值。部署准备、调用方清单和实际停写窗口见[生产部署说明](resource-production-deployment.md)。

2026-10-30 首次唯一测量保留为失败：210 个受理样本和三档均完成，积压 900→2,700→6,300；下表统计来自原始 native 样本，单位 ms。随后 staging 观测断言中止，最终 OS RSS、staging 数值与容量场景未完成，不能填零或声称整轮通过。证据 `D:/codex-runs/crawler-cloud-receipts-20261029/readiness-20261030-131531/scale-measure/{scale-result,failure-analysis}.json`。

| 并发 | 请求/s | enqueue p95 | find p95 | 新 Singer status p95 | 原 Song status p95 | 锁获取总耗时 p95 |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | 5.445 | 176.964 | 7.542 | 16.086 | 7.146 | 0.460 |
| 2 | 4.412 | 247.217 | 7.871 | 36.917 | 12.826 | 193.897 |
| 4 | 3.269 | 965.665 | 6.880 | 82.947 | 26.503 | **866.805（超过500）** |

并发 4 的锁指标已实测不达首轮工程门槛；修正 staging 统计也不能消除该事实。后续单独的本机统计自检或容量补测必须绑定新工具及原失败记录，明确 supplement-only，不改原阈值、不覆盖原结果、不重新解释为整体通过。当前数据不足以放行并发 4 或给出完整 RSS/staging 峰值保证。

已完成的补充验证分别保留在 `scale-staging-final`、`scale-capacity-supplement`，均 `supplement_only=true/original_scale_passed=false`。Windows 的 `directory_entry.file_size` 会复用目录枚举缓存，仍打开的已写文件可返回0；探针改用重新打开 `ifstream(ate)` 的真实 EOF，不改生产 FileStorage。最终本机自检持有12个新样本，实测峰12MiB、14次会话边界扫描、结束0字节，OS峰8,716,288字节；没有5ms周期线程，其RSS也不回填原压测。

容量补测先核对原210操作、6,300个pending任务/槽、4,200云回执、三端各2,100任务及全部fixture归属，再执行4争1、满队重放和三端超限拒绝：新增1个local任务，3个竞争拒绝+1个三端原子拒绝，重放不增行；最终211操作、6,301任务/槽，云回执仍4,200。原plan/prepared/失败结果字节SHA前后相同，未重跑210请求或清空现场。补充命令只适用于这样的原失败记录，不是第二次规模测量：

```powershell
python scripts/resource-scale-rehearsal.py staging-check --prepared-dir '<原prepare目录>' --failure-result '<原scale-result.json>' --probe '<最终探针>' --output-dir '<新本机自检目录>' --private-dir '<新受保护日志目录>'
python scripts/resource-scale-rehearsal.py supplement --prepared-dir '<原prepare目录>' --failure-result '<原scale-result.json>' --probe '<同一最终探针>' --output-dir '<新容量补测目录>' --private-dir '<新受保护日志目录>' --exclusive-window --writers-stopped
```

本轮最终63项工具自测（门禁/路径29、部署18、规模16）及手动探针构建通过；完整规模首测仍失败，不把补测相加改判。历史路径最终45,558条摘要一致，未检查文件；所属6111测试实例已验证UUID/PID/datadir后正常关闭，原3306服务保持。最终汇总：`D:/codex-runs/crawler-cloud-receipts-20261029/readiness-20261030-131531/readiness-verification-final.json`。

## 并发 4 修复后的完整规模复测（2026-10-30）

本轮沿用原负载、阈值和锁观测定义，不降低并发、批量大小或 500ms 门槛；原失败及中间两轮仍保留。请求内同连接缓存将常规 10资源×3端的 190 次重复 prepare 移到热点锁前；锁定容量计数在内层保留原索引、状态集合和 `LIMIT maximum+1 FOR UPDATE`，外层只回传整数，普通 status 查询保持原样。内层锁读与 LIMIT 的依据分别见 [MySQL 锁定读](https://dev.mysql.com/doc/refman/8.0/en/innodb-locking-reads.html)及[派生表优化](https://dev.mysql.com/doc/refman/8.0/en/derived-table-optimization.html)，另由真实 RR/NOWAIT 回归验证，不仅依赖文档推断。

这些改动仍不足以稳定达到门槛：缓存轮 p95=527.453ms，标量计数轮=747.722ms，虽然吞吐改善，尾部仍失败；不能把差异归因于已证实的 CATS 调度。最终只为普通 `Process` 分出批量持久化：复用原逐项政策/锁读取，任务 JSON_TABLE INSERT、pending 槽 INSERT SELECT、pending 云回执 ensure 三条 SQL 严格顺序 await，任一错误立即停止并回滚。任务按 canonical ordinal 排序，槽与回执读取真实 task_id，不假定自增连续；四 owner、NULLIF、uint64、历史 skip/busy/unavailable 等字段保持。Retry/Repair/RefreshContent/ProduceArtifacts 保持逐项写入。pipeline 只用于原分支必执行的 slot 锁读→latest SELECT，绝不流水线发送多个 DML：前段死锁可能自动回滚事务，继续发送的 DML 在 autocommit 下存在单独提交风险。

最终在全新 `crawler_ownership_test_release_scale4c_1030` 副本完成同一轮 210 请求/6,300 工作任务，非预期失败0，完整性及所有原阈值通过：

| 并发 | 混合受理请求/s | enqueue p95 ms | find p95 ms | 新 Singer status p95 ms | 原 Song status p95 ms | 锁获取上界 p95 ms |
| --- | --- | --- | --- | --- | --- | --- |
| 1 | 6.272 | 143.956 | 7.712 | 25.476 | 6.891 | 0.443 |
| 2 | 5.624 | 240.020 | 10.615 | 37.654 | 13.107 | 147.944 |
| 4 | 4.407 | 530.679 | 13.889 | 82.601 | 26.859 | 426.890 |

同一 native 进程 OS PeakWorkingSetSize=17,575,936字节（约16.76MiB），终态仍存活时读取并握手确认；不包含 MySQL 服务及 Python 驱动。staging 为实际 FileStorage 新样本的 EOF，峰12,582,912字节/12文件、周期扫描2,494次（配置间隔5ms）、总扫描3,125次，结束0字节。本轮终于同时覆盖完整受理、查询、锁、RSS、staging 和容量边界，不用先前单独本机自检替代。4争1只接收1，3竞争拒绝+1三端原子拒绝，满队幂等重放不增行；最终211操作/6,301任务和槽/4,200云回执，三端2,101/2,100/2,100。

主agent与三个子agent实际同为 gpt-6.1-sol/xhigh；四项受影响 MySQL 回归在最终代码上均通过，含最后任务与后段云回执故障整组回滚、参数/顺序、大于2^63的uint64、RR旧快照仍0但当前计数2/3、两连接真实3572锁冲突和回滚后释放；23项规模工具自测、构建和clangd通过。早期一次默认30秒建库超时未进入业务断言，保留现场并换新空库以120秒初始化预算重试，规模门槛从未更改。

副本均从原不可变 backup.sql/triggers.sql 顺序恢复，首个新副本完整数据导出SHA与原源库一致（4,439,979背景行/1,505,955歌曲资源），后续各副本同源并再次精确计数。45,558条历史路径最终摘要保持 `2a7e2bf8e4bbc88cd85ddc2a10f22ace4b4fc7f36f684581639f8e04edaa2606`，不读取历史文件。工具白名单只显式允许原库及本轮 scale4/scale4b/scale4c；每次保留原库与证据，不清表重测。后续重测须另行准备明确授权的副本和范围。

证据：`D:/codex-runs/crawler-cloud-receipts-20261029/lock-scale-20261030-134522/lock-scale-verification-final.json`，最终原始报告 `scale-measure-r3/scale-result.json`。这是约定负载下的一轮工程基线，仍 `controlled_fixture/release_scope_bound=false`，不是千万行或生产SLA/发布放行。测试实例已按UUID/PID/datadir核对后正常关闭，原3306服务未动；无生产写入、云端写入、历史文件读取或生产切换，未新增migration。

## 按范围检查的九类证据

2026-10-30 本轮用户明确：**历史本地文件不做验收，数据库原路径保留即可**。因此不再追查旧本地根、清空原路径或因文件缺失触发补下载；该范围不阻断本轮云端测试。已完成的文件/journal fixture 备份还原仅作为开发回归，不把范围排除填成生产全量还原通过。本轮分层验收未绑定完整生产发布计划，`release_scope_bound=false/production_activation_performed=false`。

每条 JSON 使用 `format=crawler.resource-release-evidence.v1`、正确 `kind`、计划的规范 JSON SHA-256 `scope_sha256`、`execution=actual`、带时区的 `started_at/finished_at`、`measurements` 及 1..16 个带相对路径/字节数/SHA-256 的执行文件。`scope_sha256` 的实现为脚本 `validate_plan(plan)`，计算 `canonical({"scope": scope, "limits": limits, "not_before": not_before})`；不是整个计划文件的字节摘要。阈值或起始时间改变也使旧证据失效，不能测后放宽而继续复用旧 scope。模板/fixture/unknown 不满足真实证据条件；即使字段自称 actual，也仍须人工确认对应执行日志，门禁不认证真实性。

| kind | measurements 的主要硬性条件 |
| --- | --- |
| history_adoption | all_targets_reviewed=true，source_conflicts=0，unresolved_decisions=0，baseline_sha256 等于范围 |
| writer_compatibility | unmigrated_writers=0，old_clients_verified=true；指部署调用方逐项已升级或明确停用，不代表所有旧 API 都已迁接 |
| writers_stopped | observation_seconds 达阈值，running_claims/uncertain_tasks/external_writers 均 0，all_instances_stopped=true；结束时刻距离检查不超过 5 分钟 |
| clone_upgrade | 白名单克隆库，before_schema_version 小于目标，schema_version 等于目标，migration_checksums_verified=true，业务总行数前后一致，unexpected_columns_removed/production_writes 均 0 |
| database_restore | 白名单同名库、不同 source_server_uuid/restore_server_uuid，restore_was_empty=true，源/恢复数据 SHA-256 一致，migration_checksums_verified/trigger_definitions_verified/triggers_installed_after_data=true，production_writes=0 |
| file_restore | source_was_stopped/journal_included=true，源/恢复清单 SHA-256 一致，missing_files/mismatched_files 均 0 |
| multi_instance_crash | instances 至少 2、claimed_tasks 大于 0、forced_termination=true；重复发布/迟到提交/未决覆盖均 0，恢复后实际完成且 recovery_seconds 达阈值 |
| scale_workload | rows/samples 达范围，failures=0，query_p95_ms/lock_wait_p95_ms/入队每秒/RSS/临时盘峰值均达预先约定阈值 |
| cloud_live | 三端 actual_io、指纹、对象数量、实际完整 SHA-256 核验和 stop_confirmed；两端开始时间早于本地完成，不能只比总耗时 |

普通证据不能早于本计划 not_before，不能超过 max_evidence_age_hours，不能来自未来 5 分钟以外。计数不能用布尔值，非有限数、缺失、未知、摘要不符、跨范围、陈旧或重复记录均不通过。

## 当前上线阻断条件

剩余旧 retry/reset 等语义不能简单替换成启动 v2 传输；结构化迁移指导及 503 保护不等于部署调用方已经升级。必须列出每个调用方实际使用的写路由、升级后的动作或明确停用决定，核对旧二进制、定时任务和独立脚本均停写。metadata/backfill 等未迁接入口仍按实际保护状态处理。

删除清理的 capture 是显式 CLI 时，部署必须安排删除前捕获、人工审阅及后续核对；不能假设所有删除入口已自动留清理证据。跨账号/根切换、未决对象及共享引用均须保留明确决定。

2026-10-30 已完成真实旧库的独立克隆升级、另一实例备份恢复及两个真实 demo 本地 worker 的强杀/恢复/HTTP repair；云端实际目标核验、写入和分端取消的最终范围见[方案阶段记录](../roadmap/resource-cloud-receipts.md)及[当前交接](../agents/current.md)。历史本地文件按上述用户范围排除，45,558 条路径数据库摘要实测保持一致。原规模失败与本轮修复后的完整通过结果均如上保留；本轮并发4锁获取p95=426.890ms，RSS/staging及容量完整通过。部署配置与盘点工具完成，外部调用方退出与实际生产停写窗口仍待明确。本轮未激活正式 `crawler` 或导入生产历史策略。兼容期后的收缩另见[旧结构退役](resource-schema-retirement.md)。
