# 资源回执数据库迁移

本文区分数据库结构升级、历史回执补建和业务运行。开发中的 `crawler` 库可在业务服务未启动时独立迁移，无需先启动 HTTP 或后台 worker；结构升级完成也不会自动传输资源或激活基线。

## 回执表与旧数据

百度和 Google 使用同一张 `resource_cloud_receipts`，由 `cloud_provider=baidu/gdrive` 区分。唯一键 `(owner_type, resource_id, cloud_provider)` 允许同一资源分别保存两端记录；四类 owner 为 `song/singer/album/song_artifact`，四条资源外键和两条身份触发器保证归属。本地结果继续保存在各自资源表。`baidupcs/rclone` 表示工具，不能直接等同于平台。

`V2026102902` 创建回执结构及代次/策略；至 `V2026102912` 补齐任务、producer、发现、容量、导入控制与清理证据；当前新增 `V2026102913` 封存并删除四资源表31个旧物理云列及2个旧索引。云端事实统一读写新回执，本地路径、状态、大小及原校验值保留。迁移不会据旧 backend 自动生成两端成功回执，也不重算旧摘要或读取历史文件；历史跳过须另走[审阅采信](resource-cloud-import.md)，不能把建表成功当作补建完成。原44份 migration 不修改，本轮只有新增前向 V2913。

从 V2026102601 升级还包含既有 V2702/2801 歌手资料迁移：来源性别按分类归属及旧值规则落入 `singer_source_refs.source_gender`，删除 `singers.gender/status` 和 `singer_source_refs.metadata_status`。这是既有字段归属清理，原值须保留在升级前备份；资源旧云列在 V2912 扩展阶段保留，至 V2913 才单独收缩。

## 独立迁移入口

先构建 `crawler_cloud_receipts`。数据库须已存在；账号需具备本库 migration 所需的 DDL、触发器和读写权限。认证从私有 `CRAWLER_ENV_FILE` 或进程环境中的 `CRAWLER_MYSQL_*` 读取，命令行不接收密码。`--database` 必须与选中的配置一致；不接受系统库。

```powershell
$tool = 'D:/crawler-build/Debug/crawler_cloud_receipts.exe'
$taskDatabase = 'crawler'
& $tool schema-status --database $taskDatabase
# 核对返回的 database/server_uuid；旧结构返回 ok=false、退出码2是待迁移状态。
$taskServerUuid = '<schema-status 返回并核对的 server_uuid>'
& $tool schema-migrate --database $taskDatabase --expected-server-uuid $taskServerUuid --timeout-seconds 900
if ($LASTEXITCODE -ne 0) { throw 'Inspect migration progress before retry' }
& $tool schema-check-sql --database $taskDatabase --expected-server-uuid $taskServerUuid
if ($LASTEXITCODE -ne 0) { throw 'Runtime SQL check failed' }
& $tool schema-status --database $taskDatabase --expected-server-uuid $taskServerUuid
```

`schema-status` 只读结构与迁移元数据；`schema-migrate` 使用正式 Runner 升序执行；`schema-check-sql` 仅 prepare/close 运行期模板，不执行模板内 DML。三项均不启动服务或 worker，不访问音乐源、不上传、不激活基线。正常退出码0；结构未就绪/检查失败为2，参数、连接或执行异常为1。默认等待900秒，可设1..3600秒。

## 执行与中断处理

1. 核对实例 UUID、库名和业务连接，停止并排空其他 writer。V2913 要求三目标均 maintenance，且无原生认领、持久占用、待执行任务、未决回执或旧封面云意图；当前开发库服务未启动，无需为此启动业务。
2. 完整备份数据、结构和触发器，并保存摘要；在隔离实例恢复同一备份，先验证旧数据与待执行迁移。
3. 对隔离副本执行迁移、运行期 SQL prepare、保留字段逐值摘要核对与重复迁移，再执行实际开发库同样的检查。
4. 最终要求组件版本、ledger 和源码最新版本均为 `2026102913`，无 pending/checksum/结构缺失；31个旧物理云列和2索引为零，归档、本地保留字段、现有回执/skip及三张 legacy 映射表核对一致。记录实际执行结果，不将隔离库通过写成实际库完成。

MySQL DDL 隐式提交；超时或进程中断不表示整文件已回滚。Runner 校验已执行版本的 checksum，拒绝未知未来版本，不改历史 SQL。全部已知迁移核实后同步组件版本，修复 ledger 已升级但组件标记滞后的情况；相同版本重跑不改变时间戳。

V2910 的 ALTER 若已提交而 ledger 尚未写入，仅当冻结 checksum 与完整后置条件全部匹配时补登记：包括生成表达式、类型、索引、关联表与约束。缺失或不一致的半成品明确失败，不能忽略 duplicate column 错误或手改 checksum 强行继续。其他中断按具体版本核查，不宣称通用自动恢复。恢复备份另选明确目标，不能直接覆盖当前库。

V2913 先在一个事务中封存非默认旧云值、按明确平台附加历史证据，并写入 `crawler_cloud_archive/legacy_cloud_evidence_sealed` 标记；提交后重建两类封面来源触发器，再逐表执行完整 DROP。每表只接受全旧列或零旧列；未删表重跑前须与封存逐行一致，少量列已被外部删除等异常结构拒绝继续。分表 DDL 已提交而 V2913 ledger 未写入时，Runner 可凭封存标记重放剩余步骤，不再读取已删表的旧列。旧 HTTP JSON 继续由新回执投影，旧二进制直接访问旧列的能力不保留；回退须前向修复或还原匹配版本的备份。详情见[物理收缩说明](resource-schema-retirement.md)。

## 2026-10-30 实际开发库验证

实际 `crawler` 库从 `2026102601` 升至 `2026102912`，累计44份冻结 migration；业务服务未启动。先从本次735,116,742字节备份恢复隔离副本，再执行实际库升级；两处均通过389份运行期 SQL prepare，重复迁移的版本、checksum、执行记录及时间戳不变。

19张原业务表共4,439,979行，按原列及主键排序核对逐值摘要，除上述3个预期删除字段外全部一致；11,745条来源性别与迁移前计算的预期映射一致。1,505,955条歌曲资源及历史本地路径保留，未读取历史文件。该结构升级阶段建立回执、任务及历史控制表，尚未执行历史采信；其后实际历史补建已单独完成，见下段。

本次证据位于 `D:/codex-runs/crawler-cloud-receipts-20261029/crawler-db-migration-20261030-1437`；完整备份和原始日志置于对应受限私有目录，不入 Git。隔离回归 `MySqlMigrationRecoveryTest`、`MySqlResourceCloudSchemaTest --upgrade-from-legacy`、分句测试和15项 CLI 参数测试通过。结构迁移验证不代表历史回执已经补建或全部业务流程已经验收。

同日后续按用户授权已完成实际开发库[历史双端回执及本地跳过补建](resource-cloud-import.md#2026-10-30-实际开发库历史补建)：45,558条旧完成资源、91,116条回执、全范围审计及重复导入核验。新增运行期SQL后总计411份prepare通过；无新DDL，版本仍V2912。

## V2913 物理云列收缩状态

2026-10-30 按用户授权，先从本轮 V2912 完整备份恢复隔离副本并完成收缩核验，再对实际开发 `crawler` 执行 V2913。当前组件版本、ledger 与源码均为2026102913；总计45份migration，原44份字节与校验值不变。旧协议整体退役的兼容窗口不作为本次保留旧 HTTP 投影的物理删列前提。

| 项目 | 本轮实际结果 |
| --- | --- |
| 备份与恢复 | `backup.sql` 1,401,699,123字节，触发器单独保存；同一备份先恢复数据后恢复触发器，隔离副本含91,116条既有回执 |
| V2912→V2913 回归 | 32项相关MySQL/离线测试最终通过，另34项工具自测通过；覆盖四owner精确归档、未决拒绝、单表DDL后恢复、重复幂等、强回执防降级、旧投影及删除大小捕获 |
| 完整克隆及实际库 | 两处37表6,037,061行保留字段逐值摘要均一致；所有原业务数据、本地路径/旧摘要、skip、当前回执原字段及原证据键保持 |
| 物理清理 | 两处31旧云列/2索引均为零；不新增或删除表，仍39张表；三张legacy映射表和旧HTTP JSON继续保留 |
| 旧云值及审计 | 45,558条非默认旧云值按id/内容代次/来源身份/原JSON精确归档；仅向明确百度端追加evidence.retired_legacy，Google不猜路径；原1,505,955条历史快照逐条差异0，旧审计全量保留，新增审计仅这45,558条 |
| SQL及幂等 | 最终克隆与实际库各422份运行SQL prepare通过；重复迁移版本、checksum、所有组件/ledger时间戳不变；源码物理旧列访问0 |
| 旧清单兼容 | 原实际库审阅清单及原SHA重复apply仍completed，processed=1,505,955、imported=45,558、conflicts=0，回执/审计/run数量及时间戳未变 |

核心证据位于 `D:/codex-runs/crawler-cloud-receipts-20261029/cloud-retirement-20261030`：`backup-before-retirement.json`、`clone-contracted-data-final.json`、`crawler-contracted-data-final.json`、`crawler-schema-ready.json`、`regressions-final-r4.json`、`crawler-old-manifest-replay-final.json`。初次回归暴露数值类型推导为DECIMAL和JSON_TABLE隐式锁问题，已修复并在新隔离库复测；原始失败记录保留。原始备份、清单和日志在受限私有目录，不入Git。

本次没有读取历史音乐文件、请求源站/网盘内容、上传或远端删除；三目标继续maintenance、baseline_ready=0，HTTP/worker未启动，任务/operation保持0。按用户要求仅保留本地路径，不将历史本地文件存在性记作通过。自持6111已核对UUID/PID/datadir后关闭，原3306仍运行，数据与失败现场均保留；未暂存/提交/推送。
