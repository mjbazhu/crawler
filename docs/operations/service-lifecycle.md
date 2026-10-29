# 服务运行与恢复

核对日期：2026-10-29；源码基线 `967bc67`，API/OpenAPI `1.9.0`。本指南针对单个 crawler 进程和明确的 MySQL/文件根。启动步骤见[快速启动](../getting-started.md)，开关与优先级见[配置说明](../configuration.md)，完整请求见[API 说明](../api/mg/migu-api.md)。

## 启动前与就绪检查

1. 确认运行版本、工作目录、私有 env 的实际来源、数据库名、文件根和云端目标。路径不同不代表文件已迁移；更换网盘账号也不代表旧回执仍有效。
2. 确认目标库已存在。升级前保留可用的库和文件备份；初始化会执行 migration，并可能恢复超时状态，不是只读探测。
3. 首次用前台 `demo.exe` 启动。等待 schema 初始化、文件日志核对、一般 stale 恢复及 HTTP 监听；失败先读该进程日志，不同时反复启动多个实例。
4. 依次检查 `/api/health`、`/api/migu/schema/status`、`/api/migu/settings` 和一项 catalog 查询。schema 应兼容当前版本，settings 的只读后端应符合预期；health 只证明 HTTP 存活。

服务没有持久化的全局“暂停调度”开关。维护前先停止调用方提交新任务；多个实例共享文件根和认领时需要另外的恢复协调，不能仅依赖单实例启动核对。

## 正常停止与任务取消

维护前查询 `GET /api/migu/tasks?state=Running`，保存需要跟踪的 batch_id；对仍在当前进程内的任务请求协作取消：

```powershell
$taskBase = 'http://127.0.0.1:19129'
$taskBatchId = '替换为实际批次ID'
Invoke-RestMethod "$taskBase/api/migu/tasks/stop" -Method Post -ContentType 'application/json' -Body (@{ batch_id = $taskBatchId } | ConvertTo-Json -Compress)
Invoke-RestMethod "$taskBase/api/migu/tasks/$taskBatchId"
Invoke-RestMethod "$taskBase/api/migu/tasks/$taskBatchId/items?page=1&page_size=50"
Invoke-RestMethod "$taskBase/api/migu/tasks/$taskBatchId/events?page=1&page_size=100"
```

取消返回 200 表示已请求，不能据此认定在途网络、文件提交或数据库回写已经结束；继续观察 item/event 与资源状态。封面尚未开始的目标不再认领，在途目标按用例取消检查收尾。任务终态可能先于在途工作完成；维护前还需确认没有活跃 I/O 和认领。

任务静止后在前台终端按 Ctrl+C，等待 HTTP 停止及进程退出，再确认原 PID 和监听端口消失。当前 stop 关闭 HTTP 并取消 MySQL 池，没有“所有在途任务必然排空后再停库”的保证；不要把直接退出当作有任务时的完整优雅排空。`Stop-Process`、关闭窗口或系统断电属于中断路径，恢复按下一节处理。

`tasks/progress` 和 `tasks/stop` 依赖内存任务，重启后可能返回 404；持久化历史通过 tasks 详情、items、events 查询。没有 HTTP 全服务 shutdown 路由。

封面服务释放时请求协作取消，已排队及在途 worker 持有独立的进度、停止标记和所需 port，不再访问已析构的批次管理器。该生命周期保护不构成全服务排空协议，也不保证 MySQL 池取消前所有目标已经完成回写；维护窗口仍按上述顺序等待静止。

## 中断后的状态恢复

先确认旧进程已停止或原认领确已超时，选择大于正常最长任务耗时的阈值。恢复使旧代次失效，但不会撤销正在进行的外部副作用；不应为了快速解锁而把仍正常执行的任务批量置失败。

| 对象 | 恢复入口 | 后续动作 |
| --- | --- | --- |
| 四类元数据目录、歌曲资源及批次 | `POST /api/migu/maintenance/recover-stale` | 选定 types，核对恢复计数；再通过相应入口重试 |
| 歌曲资源局部治理 | `POST /api/migu/resources/recover-stale` | 限定资源范围，按 API 校验条件操作；不拉回永久跳过的 `status=4` |
| 歌手头像/专辑封面本地与当前启用的云端阶段 | `POST /api/migu/covers/recover-stale` | `owner_type` 分别提交；已下载文件默认通过 process 补云端 |

通用 maintenance 的类型只有 `singer-songs/singer-albums/album-details/album-songs/resources/batches`，不包含封面认领。启动时也只执行该通用恢复；阈值为 `max(60, 启动 task_timeout_ms/1000)`，当前默认 600 秒。文件 journal 的启动核对与队列 stale 恢复是两个动作。

先按源及类型缩小一般恢复范围，例如：

```powershell
Invoke-RestMethod "$taskBase/api/migu/maintenance/recover-stale" -Method Post -ContentType 'application/json' -Body '{"source":"migu","stale_after_seconds":3600,"types":["singer-songs","singer-albums","album-details","album-songs","resources","batches"]}'
```

封面示例须将资源 ID 替换为 queue 返回的真实 ID；歌手和专辑的相同数字 ID 不能互换：

```json
{
  "owner_type": "singer",
  "resource_ids": [123],
  "stale_after_seconds": 3600
}
```

将上述 JSON 提交到 `/api/migu/covers/recover-stale`；专辑使用 `owner_type=album`。本地恢复推进代次并置失败，保留旧成功文件；云端恢复独立推进代次并置待核对，保留最后确认回执。云端关闭时该入口只处理本地认领；需要恢复云端阶段时恢复相应配置后重启再操作。`recovered_count` 是两个启用阶段的合计，不承诺等于去重后的资源数。

恢复接口本身不重新下载或上传。确认计数与队列结果符合目标后，再有界重试。旧批次历史保留；封面和资料等任务的重试不要假设所有通用 `rerun-failed` 都支持其 item 类型，应使用对应业务入口。

## 文件及云端异常的处理顺序

| 现象 | 处理 |
| --- | --- |
| 歌曲本地待处理/失败 | 解决原因后按原条件再次 `/resources/process`；音频重试重新换临时 URL，版权等永久跳过继续保护 |
| 封面本地失败或尚未下载 | 限定 owner/ID 调用 `/covers/process`，默认后台批次，查看 task/item 的错误分类 |
| 封面本地 `status=2`，云端失败或待核对 | 保持相同目标配置，普通 process 即可补传或核验；不要用 refresh 强制重下 |
| 封面文件丢失、损坏或明确需要换图 | 显式 resource_ids 加 `refresh=true`；失败刷新保留旧回执，真实可用性由文件读取核验 |
| `writeback_uncertain` / `cloud_writeback_uncertain` | 先重读资源与批次、检查 DB 和日志；保留文件及 journal，避免删除可能已提交的对象 |
| 云端 EOF、超时、未知元信息或摘要缺失 | 保持待核对/失败，重试先核验确切对象；不能当作远端不存在或已成功 |

同名云端对象内容不符时不盲目覆盖。`cloud.path` 是最后确认路径，判断当前结果须结合 `enabled/status/current` 和文件快照；关闭云端时 `current` 不能证明某个启用目标仍在线。切换目标后只保留一份最后回执，无法由它判断百度与 Google 两端同时存在。

本地资源使用独占尝试文件和日志：明确拒绝回写才回收本次文件；提交响应不确定时保留，启动时按所有者、代次及引用核对。专辑别名合并后的引用同样受保护。不要手工清理 staging、journal 或“看似孤立”的文件来处理模糊提交。具体不变量见[资源流程](../migu/resources.md)与[封面云端专项](../roadmap/metadata-cover-cloud-storage.md)。

## 迁移失败与版本回退

1. 暂停新任务，确认目标库及第一条实际错误，区分连接/权限、路径、迁移锁、超时和 checksum 不匹配。
2. 同一 MySQL 实例共用 `crawler_mysql_migrations` 锁；顺序初始化。大库升级可在计划内增加 `CRAWLER_MYSQL_STARTUP_TIMEOUT_MS`；超时取消客户端不代表服务器端 DDL 已立即终止，重试前核对连接和结构。
3. checksum 不符时恢复与已执行记录匹配的原迁移文件；不修改 `schema_migrations` 伪造通过。需要修复结构时新增前向 migration，并验证旧数据和重复执行。
4. MySQL DDL 可能隐式提交，中断不保证回滚整个文件。核对已执行语句与当前结构，修正环境后使用原 migration 的幂等路径重试；不跳过失败版本或混用 `create.sql` 快照补升级。
5. `/schema/status` 兼容、必要 SQL 查询和受影响数据核对通过后，才恢复任务。`CRAWLER_MYSQL_INIT_SCHEMA=false` 只能用于已经确认兼容的库。

回退可执行文件不回退数据库。存在删列或其他不兼容变更时，旧程序可能仍引用已删除字段；应选择适配当前 schema 的修复版本，或按确认的备份恢复方案回退库和资源。禁止直接反向编辑历史 migration。当前结构及版本登记见[SQL 管理说明](../../sql/mysql/README.md)。

## 备份与恢复演练

维护窗口先停止新提交并让任务静止，再停止服务；同一窗口保存数据库、实际文件根、未决 journal、源码/迁移版本和备份校验清单。仅数据库事务快照不能保证同时复制的活动文件一致。网盘外部对象及登录配置另按环境备份，密钥不进入业务仓库。

MySQL 客户端示例在 PowerShell 中使用 `--result-file`，避免重定向造成 SQL 编码变化；库名、账号和路径必须按实际目标替换：

```powershell
& 'C:/Program Files/MySQL/MySQL Server 8.0/bin/mysqldump.exe' --host=127.0.0.1 --port=3306 --user=root -p --single-transaction --routines --events --triggers --hex-blob --set-gtid-purged=OFF '--result-file=D:/crawler-backup/music.sql' crawler_ownership_test_quickstart_20261029
if ($LASTEXITCODE -ne 0) { throw 'Database backup failed' }
Get-FileHash -Algorithm SHA256 -LiteralPath 'D:/crawler-backup/music.sql'
```

备份目录应提前存在，并另复制已经静止的文件根和 journal，记录相对路径/大小/摘要。该示例不加 `--databases`，方便恢复到独立演练库；导入前检查 SQL 中的 `USE` 和跨库限定引用，尤其是视图。若存在原库限定名，优先在隔离 MySQL 实例保留原逻辑库名恢复，不能只改命令行数据库名就认定引用已隔离。

恢复步骤：

1. 校验备份摘要和版本清单；创建独立恢复库或隔离实例。先确认导入不会写回原库。
2. 使用 MySQL 客户端在目标库执行备份文件，例如 `--database=<恢复库> --execute="SOURCE D:/crawler-backup/music.sql"`；核对全部表、视图、关系及 migration 记录，没有跨回原库的引用。
3. 将配套文件恢复到独立根，保留原相对路径和 journal；用匹配版本启动，首次关闭云端并控制调用方，避免演练产生外部副作用。
4. 检查 schema、catalog、文件大小/摘要、本地封面 content，以及任务/资源状态。启动恢复可能改变遗留运行态，应与备份前快照分别记录。
5. 云端回执表示上次确认，不能证明远端对象现在仍存在；需要时独立核验对象与文件快照。演练达到预先约定的结果后，另行安排切换；不覆盖原库或删除旧备份。

本轮未执行备份导入、强杀采集进程、系统重启或多实例恢复；上述步骤为运维流程，相关场景仍待独立演练，不表述为灾难恢复已验收。

## 文档校准与指南验收（2026-10-29）

本轮更新 Markdown 与静态/运行期 OpenAPI 的 description，运行期工厂只改说明字符串，不改业务逻辑、协议字段结构或 SQL。源码核对包括入口读取、默认值、运行设置校验、存储装配、停止与恢复路径；修正架构/路线状态及歌曲自动上传描述，保持后续统一回执表尚未实施。

本轮实际检查与演练如下，不替代原[封面组合验收](../roadmap/metadata-cover-cloud-storage.md#pr-7-与-main-的组合验收2026-10-29)：

- 在已有 Windows/MSVC、Ninja、vcpkg 和 CMake `4.1.1-msvc1` 环境按 preset 重新 configure，构建目录沿用仓库外本地链接参数 `/DEBUG:NONE /INCREMENTAL:NO`；最终说明字符串校准后重新编译工厂、链接 demo 和三项 API target 通过。
- API 文案提交 `a9d4127` 的最终 `MiguApiDocumentTest`、`MiguCatalogApiSmokeTest`、`MiguHttpRouteSmokeTest` 共 3 项 CTest 通过。运行期文档前后只有 6 处 description 不同，其余 JSON 值不变；静态 YAML/工厂差异也仅为对应说明行。全量路由仍为 54 个注册业务操作/展开 66 个，API 清单及两份 OpenAPI 缺失和多余均为 0，静态 85 个组件引用有效。
- 启动演练使用 `967bc67` 的业务代码，在后续仅改 OpenAPI 说明前执行。新建独立库 `crawler_ownership_test_quickstart_20261029_102348`，首次服务启动执行 33 份 migration，schema `ok=true`、最新版本 `2026102901`。私有连接参数只读自既有配置，经子进程环境传递，库名/端口/文件根显式覆盖，未修改原私有文件。
- 本机 `127.0.0.1:19129` 两轮共 22 次 HTTP 检查通过：health、schema、settings、空歌手/两类封面/任务查询、OpenAPI、PATCH 后恢复原设置、非法设置 400，以及一般/两类封面 stale 空队列恢复计数为 0。没有造运行中任务，不把空队列结果算作故障恢复验收。
- 同库再次启动不重执行 migration；业务实体和批次总行数始终为 0。两次 Ctrl+C 后均核对自建 PID 已退出且端口释放；终端因中断返回 1，原生进程退出码未单独采集，不将其表述为 exit=0 或在途任务排空证明。
- 本轮关闭云端，不请求音乐平台、不下载/上传资源；未写运行库及原 main20 来源库。已有工具环境上的演练不等于空白机器安装、备份导入、强杀采集、系统重启或多实例验收。

仓库外证据根为 `D:/codex-runs/crawler-cover-local/20261028/`：`guide-configure.txt`、最终 `guide-description-build.txt` / `guide-description-api-tests.txt`、`guide-openapi-description-check.json`、`guide-docs-check.json`，以及 `guide-startup-20261029_102348/` 中的 manifest、两轮 HTTP 结果、迁移输出和停止结果 `result.json`。Markdown 链接/章节锚点、UTF-8 无 BOM、PowerShell 及 JSON 示例均经过静态检查；实际数量以最终检查文件为准。
