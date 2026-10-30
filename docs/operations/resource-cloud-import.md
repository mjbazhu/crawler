# 资源云端历史声明导入

工具 `crawler_cloud_receipts` 提供 `preview`、`apply`、`status`、`configure-target`、`baseline-review`、`baseline-seal`、`baseline-activate`、`recover-tasks`。这些命令只处理数据库元信息和清单文件，不读取音乐/图片/歌词、不访问源站或网盘，也不启动 HTTP、迁移或后台 worker。单独的 `target-probe` 使用服务相同配置，只读核实目标身份，不连接数据库、不上传文件。完整目标、生产迁移顺序和未实现部分见[方案](../roadmap/resource-cloud-receipts.md)。

当前 MySQL 服务的歌曲/封面用例已读取新 skip、排除策略和维护状态，歌曲普通/关联认领 SQL 同样保护匹配的策略。旧二进制仍不识别这些规则，已经接收的任务、其他进程和独立脚本必须先停止并排空。只有完成四类范围审查和封存后，`baseline-activate` 才能原子设置就绪与选定目标；禁止通过手工改字段替代封存、版本确认和兼容验证。本文流程先用于独立库演练，生产切换仍须满足方案第 11 节及当次验证要求。

## 前提和构建

使用现有 [MSVC/vcpkg 构建环境](../getting-started.md)，构建 target：

```text
cmake --build <build-directory> --config Debug --target crawler_cloud_receipts -j 4
```

库必须通过正式 migration 升级到当前二进制要求的全部版本，封存要求至少包含 `V2026102907__resource_baseline_sealing.sql`；`schemaStatus().ok=false` 时工具拒绝执行。四条资源外键、唯一索引、身份/来源触发器以及基线表约束均须完整。`create.sql` 是结构快照，不能拼接快照代替 migration。上述历史导入命令设置 `initialize_schema=false`，不创建数据库或自动升级。旧库先用同一工具新增的 `schema-status/schema-migrate/schema-check-sql` 独立完成结构升级，详见[数据库迁移](resource-database-migration.md)；它们不启动业务服务。`schema-migrate` 是明确的结构写入入口，不受旧导入命令“不会迁移”的描述约束。

当前源码要求 V2913。该版本先将非默认旧云值精确归档到 `resource_cloud_migration_audit` 的 `schema-retirement-v2026102913` run，再删除四资源表31个旧物理云列及2索引；不新增表、不重写已有双端 skip/确认，也不修改44份冻结 migration。收缩后历史 preview/apply 的旧云证据读取该不可变档案，本地及来源快照仍实时读取资源表；新回执后来变化不会改写旧清单来源。原 JSON NULL/空值和旧值摘要语义保留，已完成 run 的重放不因删列重新采信。尚未升级的其他库先按[收缩状态](resource-database-migration.md#v2913-物理云列收缩状态)完成升级，不能混用旧二进制与当前 SQL。

从进程环境或 `CRAWLER_ENV_FILE` 指定的私有文件读取既有 `CRAWLER_MYSQL_*`；进程环境优先。`--database` 必填且必须与选中的私有配置一致。命令行不接受密码；不要把私有文件或清单放入 Git。清单包含资源路径和旧值，应按数据库备份权限管理；控制台只输出统计和摘要。

以下 PowerShell 示例假设已准备好一个独立、升级后的测试库，`$tool` 和 `$manifest` 使用实际绝对路径；数据库名仅为示例：

```powershell
$tool = '<build-directory>/Debug/crawler_cloud_receipts.exe'
$taskDatabase = 'crawler_ownership_test_receipts_demo'
$manifest = '<private-evidence-directory>/songs.jsonl'
$env:CRAWLER_ENV_FILE = '<private-env-file>'
$env:CRAWLER_MYSQL_DATABASE = $taskDatabase
```

## 核实目标身份

三个目标初始均为 maintenance、revision=1、空 fingerprint、baseline 未就绪。先按[配置说明](../configuration.md)明确平台、工具、账号对应的 remote/root，再用 `target-probe` 取得服务实际使用的目标指纹。指纹区分账号/drive、存储根及布局规则；不能只对 `baidupcs` 或 `rclone` 求摘要，也不能把短期 token 纳入身份。`configure-target` 只登记指纹，不将人工输入视为在线身份核验；正式 worker 仍须将实时核实的身份与持久记录比较，未知或不同身份不放行。

`target-probe` 只接受 `--destination`，无需数据库参数，要求私有环境已显式配置 parallel 且该端 enabled。WebDAV 返回的身份只能证明当前挂载的 URL/user/root，不能证明其底层百度账号；同一挂载背后换账号仍须显式推进目标 revision 并重审基线，不能假定指纹变化一定能发现 remount。

```powershell
& $tool target-probe --destination local
& $tool target-probe --destination baidu
& $tool target-probe --destination gdrive
& $tool configure-target --database $taskDatabase --destination local --expected-revision 1 --fingerprint '<64-hex-sha256>'
& $tool configure-target --database $taskDatabase --destination baidu --expected-revision 1 --fingerprint '<64-hex-sha256>'
& $tool configure-target --database $taskDatabase --destination gdrive --expected-revision 1 --fingerprint '<64-hex-sha256>'
```

首次登记空指纹保持 revision=1；相同指纹幂等返回原修订；更换已有目标时修订加一并撤销基线就绪。比较条件同时绑定旧修订及旧指纹，避免两个首次登记覆盖彼此。必须处于 maintenance，且该目标没有持久占用槽、running 或 uncertain 云端对象，也不能有 pending/retry/running/waiting_input 的生成任务；先取消尚未执行任务、核对未决对象，不能用换目标抹掉旧副作用。已存在目标需先查询其修订，不能总传 1。指纹或修订变化后，旧清单、封存摘要均不能继续使用，需重新预览和审查；旧回执的目标修订保持原值。

## 预览和固定清单

支持 owner `song/singer/album/song_artifact`，resource_id 始终是对应资源表主键。每次导入只包含一个 owner，其两端和本地策略在同一页事务中建立。

```powershell
& $tool preview --database $taskDatabase --owner song --output $manifest --run-id 'songs-baseline-001' --page-size 500 --attest-both --legacy-rclone-provider gdrive
```

`--attest-both` 表示操作者明确确认该清单中符合条件的历史资源两端均已保存。这里用于用户声明的既有 `crawler` 歌曲范围，不能无依据套用到封面或派生产物。未确认的整个 owner 范围使用 `--exclude-all`：即使旧状态完成，也只记录 excluded，不创建 skip 或云端回执；封存时阻止该内容代次隐式补传。两选项互斥，均不提供时可导出普通预览，但 apply 拒绝执行。旧清单没有 exclusion 字段时保持原语义。

封存必须覆盖 song、singer、album、song_artifact 四类；空表也须有显式完成的空清单。对未声明双端的三类可分别执行下列示例，再审阅、apply 各自文件：

```powershell
& $tool preview --database $taskDatabase --owner singer --output '<private-directory>/singers.jsonl' --run-id 'singers-excluded-001' --exclude-all
& $tool preview --database $taskDatabase --owner album --output '<private-directory>/albums.jsonl' --run-id 'albums-excluded-001' --exclude-all
& $tool preview --database $taskDatabase --owner song_artifact --output '<private-directory>/artifacts.jsonl' --run-id 'artifacts-excluded-001' --exclude-all
```

`--legacy-rclone-provider` 只说明旧单份 rclone 路径属于哪端，不决定是否导入另一端；不确定时省略，路径保持待核对。

预览只读数据库，按主键分页固定高水位、数据库实例/库身份、三个目标修订/指纹，以及每行来源身份、内容代次、旧值摘要和白名单证据。输出 `resources`、`eligible_completed`、`manifest_sha256` 和零传输计数；精确逐端冲突/复用统计尚未提供，需复核清单并结合 apply 的审计结果。`page-size` 为 1..5000，默认 500；单行 JSONL 上限 16 MiB，路径较大时减小页大小。

输出文件或 `.part` 已存在时拒绝覆盖；导出失败保留 `.part` 供核对，重新导出使用新文件。完整文件审阅后固定其 SHA-256，后续不要再编辑或扩展范围。更换 run-id、目标映射、声明或行内容都会改变摘要；重用同一 run-id 导入不同摘要会被拒绝。

## 导入、续跑和核对

```powershell
& $tool apply --database $taskDatabase --manifest $manifest --sha256 '<reviewed-manifest-sha256>'
& $tool status --database $taskDatabase --manifest $manifest --sha256 '<reviewed-manifest-sha256>'
```

apply/status 先建立源清单旁的私有只读工作副本，再流式核对整个文件摘要，之后才连接明确目标库。预留一份额外清单大小的磁盘空间；当前执行不受原清单后续编辑影响，正常退出清理精确工作副本，强杀后的残留副本需人工核对后处理。

每页重新锁定并比较来源身份、内容代次、旧值摘要和目标修订。仅旧 `status=2`、快照未变化且无冲突认领的资源建立本地和双端 `skip_existing`；原 `cloud_backend` 无论 baidupcs/rclone 均不限制双端声明。不存在本地文件不会触发重下；工具根本没有内容读取/下载端口。非完成行 excluded；已删除、变化、活跃云端认领或更新代次进入逐 ID conflict，不强行覆盖。

`operator_attested` 与 `sha256_verified` 分开：不伪造 checksum、uploaded_at 或 verified_at，不把 `rclone:size:*` 当成内容摘要；更强的既有路径、摘要和核验事实保留。已知旧路径只映射至其明确所属端，另一端可 `path_unresolved=true`，仍可持久跳过。

V2913 的 `evidence.retired_legacy` 只追加可明确归属的旧值，不把原 `cloud_uploaded_at` 填成新回执的上传确认；旧歌曲 JSON 可在本端正式上传时间缺失时显示该档案时间。平台不明的 rclone 旧路径只保留档案，继续由明确的 `--legacy-rclone-provider` 映射，不猜 Google。旧 HTTP `cloud_*` 字段和 `resource_legacy_profiles/batches/operations` 映射继续保留，物理删列不等同于停止旧接口。

两端回执、本地策略、逐 ID 审计和两个 provider run 的 cursor/counters 在同一事务中提交。某页 SQL 失败整页回滚；COMMIT 响应丢失或写入等待超时报告未决，先以 status 核对，继续使用相同文件和摘要 apply，不猜测已回滚。中断后再次 apply 自动跳过已提交页；不同清单不能接管同一 run。已完成运行重复 apply 只读已提交进度，不重新扫描旧资源。完成遍历输出 completed；有冲突仍保留冲突计数，不代表可封存。

退出码 0 表示本次命令成功且已处理范围无冲突，2 表示存在已审计冲突，1 表示参数、依赖、SQL 或文件失败。apply/status 包含 cursor、processed_resources、imported_resources、conflicts；双端计数按逻辑资源统计，不能再相加当作资源总数。这两命令的 `baseline_activated=false`，content_requests/uploads 为 0。

审计位于 `resource_cloud_migration_audit`，控制行位于 `resource_cloud_backfills`，两个 provider run ID 为 `<run-id>:baidu/gdrive`。修复冲突后需重新预览形成新 run，原审计保留。停止导入后仅从已提交页续跑；现已提供独立import-control，见下文。

## 审查、封存与激活

`baseline-review` 只读核对四个不同 owner run 的双端 completed、高水位、游标、处理/导入计数、零冲突、逐 ID 审计、来源/旧值/代次和当前 skip；任何新增、删除、变化或遗漏均拒绝。三个目标必须 maintenance 且身份完整；持久槽、待执行/在途/未决任务、原生运行态或未决回执也会阻止审查。它输出新的 `baseline_sha256`，与单个导入文件的 SHA-256 不同。

```powershell
$scope = @('--database', $taskDatabase, '--baseline-id', 'crawler-baseline-001',
    '--song-run', 'songs-baseline-001', '--singer-run', 'singers-excluded-001',
    '--album-run', 'albums-excluded-001', '--artifact-run', 'artifacts-excluded-001')
& $tool baseline-review @scope
& $tool baseline-seal @scope --sha256 '<reviewed-baseline-sha256>' --writers-stopped
& $tool baseline-activate --database $taskDatabase --baseline-id 'crawler-baseline-001' --sha256 '<reviewed-baseline-sha256>' --destinations local,baidu,gdrive --writers-stopped
```

`--writers-stopped` 是操作者对旧进程、定时任务和脚本已停止排空的确认，命令本身不会替你停止它们。seal 在事务中重新核对全部条件、匹配审阅摘要，保存 `resource_cloud_baselines` 的固定范围，并将 excluded 写入 `resource_baseline_blocks`；仍保持 maintenance。activate 再核对封存范围，原子将选定目标置 active、其余置 paused、三个目标标记相同基线就绪。没有明确选择的端不会启动；同一参数重跑幂等，改变目标或封存输入拒绝复用旧摘要。

封存/激活失败整笔回滚；提交结果未知时用相同 baseline ID 与摘要重跑核对，不改数据库状态绕过。导入后资源发生变化须生成新 run；封存后目标变化须生成新基线。排除策略绑定内容代次，普通 process/retry 不能解除；明确 `refresh_content` 推进新代次才能按新请求重新处理。历史有效 skip 仍先于文件、hash、token 和云端探测。

活动目标可通过 baseline-maintenance 原子退回维护，再复用预览/导入/审阅/封存/激活命令调整范围。可先用[历史自动发现](resource-history-discovery.md)按明确路径核对新平台已有对象；发现报告须审阅，不能直接当作双端声明 apply。

维护前停止所有新旧writer，记录当前baseline_id和baseline_sha256，再执行：

```powershell
& './build/msvc-debug/Debug/crawler_cloud_receipts.exe' baseline-maintenance --database '<明确库名>' --baseline-id '<当前基线ID>' --sha256 '<当前基线摘要>' --writers-stopped
```

命令核对摘要、当前目标修订/指纹与激活标识，原子关闭三端准入、将原基线退回sealed，并保存治理审计。保留当前回执、历史skip、任务、槽和未决意图；重复执行不会重复审计。任何审计/状态写入失败整笔回滚。不一致或陈旧状态拒绝，不用该命令接管其他基线。

随后核对/取消未结束任务，按需要探测及configure-target；换目标须无占用，旧版本事实不提升为新目标成功。新增或变化范围须新run及新基线ID；完整执行preview/apply、baseline-review/seal/activate。范围和目标均未变化时，可显式重新activate原sealed基线，仍会重核四类范围、来源/策略和所有占用。维护命令的writers-stopped是操作方声明，不证明远端工作已停止；它不替代停写、排空和未决对象核对。不要把 `target-probe`（目标身份）当作历史对象存在性证明；没有确认缺失的历史对象不能由此自动补传。

## 崩溃后的显式认领恢复

以下入口恢复本地/云端传输认领。服务崩溃后，先停止并排空所有 writer，包括其他服务实例、旧二进制及独立脚本，再按具体目标运行：

```powershell
& $tool recover-tasks --database $taskDatabase --destination baidu --stale-seconds 600 --writers-stopped
```

`--stale-seconds` 必须为 1..604800；`--writers-stopped` 是操作者的明确确认，不是命令自动停止服务。每次只处理选定端至多 200 条过期 running 认领，输出 `recovered_tasks` 和 `may_have_more`；可用相同参数重复执行，直到本轮无可恢复项。数据库/schema 校验复用其他维护命令，不进行 HTTP、文件或云端操作，也不自动开启目标。

已有对象意图的任务进入 uncertain 并保留路径、证据和独占槽，后续须显式 repair；没有意图的任务按冻结重试预算进入 retry、failed 或 cancelled，并推进认领代次，使崩溃前 worker 不能晚到回写。命令成功只说明认领状态已处理，不代表文件已核验，也不证明旧远端上传已停止。部分页事务或提交结果未知时保留原结果，以相同命令重读续跑；不能清空槽或直接补传未决对象。

## 持久歌词生成任务

启用生成后，每个可执行 MRC 目标任务在接收事务内登记一条 `resource_producer_tasks`；普通 process 已命中历史 skip 或被其他任务占用时不登记。生成任务独立获取受控 MRC 输入，不等待正式本地下载或原始 MRC 云端保存完成；缺少解码密钥时持久保留 waiting_input，不消耗重试次数。冻结处理器/结构版本及独立时间、重试、输入大小预算，当前输入上限为 1 MiB。原始 MRC 传输已成功或失败都不能替代生成任务结果。

历史 MRC 已 skip 但尚无派生产物时，向 `POST /api/v2/migu/resources/process` 显式传入该 MRC 的 Song 资源主键、目标和 `action=produce_artifacts`，并使用稳定 `idempotency_key` 防止重复受理。该动作登记 `skipped/producer_only` 的空传输父任务，仅启动生成，不建立 MRC 传输槽、不解除或重写 MRC 的本地/双端 skip 与状态。生成会重新读取合法 MRC 输入；普通 process 仍保持历史 skip 零内容访问。非 MRC 输入拒绝；版权限制、范围排除、维护状态、目标或生成器未启用时也不能放行。缺密钥时先登记 waiting_input，补齐输入后由独立 worker 继续。

若此次合法输入请求收到明确的永久版权限制，则按当前来源/版本围栏保存 `status=4` 和生成失败诊断，后续重试不能再次请求该来源；这是新获得的来源限制事实，不会清除历史 skip 或已有保存回执。暂态网络或解码错误不改变 MRC 原始状态。

通过 [v2 任务接口](../api/mg/migu-api.md) 查询父操作的生成任务及其 `state/result_code/child_operation_id`。有输入或暂态错误时按冻结预算恢复，永久失败须显式重试；重试保留原处理器、来源、目标及预算。换处理器或目标后不能用重试把旧计划改成新计划。取消父操作时可只取消所选目标，并递归取消相关资源及派生产物操作；即使原始 MRC 已传完，仍会取消未结束的生成任务。

生成成功在同一数据库事务内确认 MRC 参考摘要、登记两类产物的真实摘要与大小、创建目标子操作和标记生成成功，任一步失败整笔回滚。该事务不填写正式本地文件路径或云端成功事实；后续子任务完成各自保存与核验后再写这些事实。两端读到的 MRC 内容不一致、来源/目标修订变化、父任务取消或资源删除时拒绝晚到提交，不覆盖已登记产物。提交结果未知时按生成任务和固定子操作关系核对，不能手工补一份子操作。

生成 worker 启动及运行期间按 660 秒过期窗口回收自身任务，每次至多 200 条；数据库还验证认领已超过冻结生成预算及收尾窗口，较小的人工参数不能抢占活跃任务。回收推进认领代次并受冻结重试次数限制，旧 worker 晚到不能提交。此机制不替代上面的传输 `recover-tasks`，也不证明远端对象可重新上传。导入、基线封存和目标切换仍须先处理所有未结束的生成计划，包括 waiting_input。

## 别名合并与保留边界

Migu 专辑物料别名归一在同一 metadata 事务内锁定两侧资源；任一侧有持久占用或运行/未决云端回执时回滚，先完成取消或核对。只有两侧完整参考 SHA-256 与大小一致，才将缺少的逐端事实和本地 skip 迁到规范资源。保留原 confirmed/policy target_revision、实际工具与核验时间，不将旧目标事实提升为新目标成功，也不覆盖规范资源已有回执。

别名资源被级联删除前，将原路径、版本、逐端事实、策略及排除记录归档到 migration audit，相关旧文件不会立即清理。参考不一致或不完整时保留规范资源事实，并对当前代次设置 `alias_identity_unverified` 阻断；旧范围排除同样迁到规范资源。审计不随资源删除级联清空。通用删除及远端孤儿对象回收仍须后续独立审核流程，本实现不自动删除远端文件。

## 验证范围

`SqlMigrationStatementsTest`、`ResourceTransferPolicyTest`、`ResourceCloudImportManifestTest` 为离线/本机文件测试。`MySqlResourceCloudSchemaTest`、`MySqlLegacyResourceImportTest`、`MySqlResourceBaselineTest`、`MySqlResourceLifecycleTest` 分别要求空的 `crawler_ownership_test_receipts_*`、`crawler_ownership_test_receipt_import_*`、`crawler_ownership_test_baseline_*`、`crawler_ownership_test_lifecycle_*` 库。覆盖四类清单遗漏/变化、排除已完成资源、封存回滚与激活幂等、两端别名事实、冲突保留及有占用时禁止目标切换。`MySqlResourceProducerTasksTest` 要求空的 `crawler_ownership_test_producer_*` 库，验证父传输终态后继续生成、子操作插入失败整笔回滚、缺密钥等待、有限重试/恢复及取消、来源/目标/处理器围栏和双端内容冲突。它们使用离线元数据与不存在的内容路径，不接触真实网盘。实际当次结果及证据位置见[当前交接](../agents/current.md)，运行条件见[验证要求](../agents/validation.md)。

显式导入控制已补 `import-control --manifest ... --sha256 ... --action pause|resume|cancel --expected-generation N`，在既有明确数据库选择下调用。control_state与每页paused进度独立；同一次apply固定控制代次，pause→resume后的旧执行不能自动追随新代次，需重新启动apply。取消不回滚已提交的历史策略，终态不能恢复。单端发现报告须先生成独立审阅清单，再以目标声明采信；missing可明确选择本端normal且不入队，详见[单端采信](resource-history-adoption.md)。

## 单端补充的基线参数

原四类 `--song-run/--singer-run/--album-run/--artifact-run` 继续作为本地及历史范围锚点。新目标上的完整单端采信，可在 review/seal 时追加 `--<song|singer|album|artifact>-<baidu|gdrive>-run <单端采信run-id>`；例如 `--singer-baidu-run baidu-reviewed-001`，其它参数和审阅SHA流程不变。activate从sealed快照重建这些选择，不接受临时扩大范围。

补充run须当前库/owner/目标修订、完整高水位与所有资源审计覆盖、completed/active且零冲突；`imported`必须当前本端skip，`allow_transfer`必须明确本端normal。封存固定补充清单摘要、控制代次及两类决策计数，激活再次核对。部分发现报告可导入，但不能代替完整范围封存；原全局excluded不会被单端normal解除，仍遵循明确refresh_content语义。严格空表可复用旧的空范围声明，当前MAX(id)仍须为0；任一新增资源或非空旧范围都不能借此跨目标。

没有补充且控制代次为0时保持旧sealed快照格式/摘要，兼容原封存数据。新的控制或目标变化会改变审核事实，需重新审阅、使用新基线标识；不要修改历史迁移或篡改旧摘要。
## 2026-10-30 实际开发库历史补建

本轮按用户授权对实际 `crawler` 完成历史补建：三端只读目标身份核实后，固定本库与隔离副本各自清单，完整覆盖1,505,955条歌曲资源。仅45,558条旧 `status=2` 资源按既有双端保存声明导入；其余1,460,397条记为 excluded，不扩大已保存范围。歌手/专辑封面和artifact当前为空，分别建立显式空范围完成记录。

实际新增91,116条云端回执（百度、Google各45,558），以及45,558条本地skip。1,505,955条逐ID审计、8个逐端run全部完成，冲突0；完整清单重复apply后记录数量、游标和时间戳一致。两端均为operator_attested/skip，不伪造SHA-256、内容确认代次、上传或核验时间。旧百度路径有依据时保留；Google旧路径未推断，以path_unresolved保存，声明跳过仍有效。

19张原业务表4,439,979行的保留字段逐值摘要与结构迁移前一致；45,558条本地路径及旧校验值保留，不读取历史资源文件。目标仍maintenance、baseline_ready=0；未启动HTTP/worker、未建传输任务、未上传下载或远端删除。基线封存/激活属于后续明确运行步骤，不能手改状态代替。

隔离导入在346,000行时显式pause，原清单/游标保留，批量审计后resume代次推进到2并继续完成；旧进程未继续跨代执行。审计现以JSON_TABLE有界批量写入，超过4MiB同页分块，与本地skip、双端回执和游标共同提交。跨chunk末块故障回归确认整页回滚，随后重试与重放通过。

本轮证据：`D:/codex-runs/crawler-cloud-receipts-20261029/history-compatibility-20261030-1510`；完整备份、清单及原始日志在对应受限私有目录，不入Git。13项针对性测试最终通过，实际库411份运行期SQL prepare通过，schema仍V2912/API1.14.0，44份冻结migration未改。自动删除捕获与旧接口语义见[兼容和删除说明](resource-compatibility-cleanup.md)。

同日后续已完成 V2913 旧物理云列收缩：91,116条原回执的字段和原证据键、45,558条本地路径/skip均逐值保持；仅按明确百度归属追加精确旧值证据。全范围1,505,955条历史快照差异0，原审阅清单按原SHA重复apply仍completed、冲突0，数量及时间戳未变。未重取资源或重新核验旧SHA，当前结构与422份SQL检查见[收缩记录](resource-database-migration.md#v2913-物理云列收缩状态)。
