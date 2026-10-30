# 旧调用方升级与云端删除预演

本轮收尾包括明确旧writer的迁移契约，以及删除前证据捕获/删除后只读预演。它不宣称所有旧写接口已经兼容，也没有真实网盘删除能力。

## 旧writer的明确边界

parallel模式继续允许已经接入统一队列的旧歌曲/封面process、进度/停止，以及只读查询/preview和既有登录态刷新。未配置固定legacy view时process仍拒绝；legacy模式的成功响应和维护闸门不变。

已知元数据/补池/批次治理和设置入口已通过明确白名单接入正式持久准入，保留原处理器及响应；全局维护关闭时返回503/resource_maintenance。资源原生治理 SQL 在候选选择及实际更新时同时保护 skip、当前代次排除、三端占用槽和未决云端意图。新任务始终经统一队列受理；未知新增 writer 或缺失准入装配仍返回503/legacy_parallel_unsupported及迁移指导。

| 旧入口 | 原有语义 | 升级要求 |
| --- | --- | --- |
| resources/retry | 把所选失败原生状态重置，原操作不开始传输 | 已兼容原请求/响应；保护skip、排除、占用及未决对象。不会改成v2 retry或创建新任务 |
| resources/reset | 重置原生状态/错误，不开始传输 | 已兼容原请求/响应；保留路径、摘要、历史skip及云端事实，不用refresh_content解除策略 |
| resources/recover-stale、covers/recover-stale | 根据旧过滤器重置过期原生认领 | 已兼容空闲旧认领的恢复，并要求local就绪；不接管三端持久槽/未决意图。统一任务仍走逐端维护CLI recover-tasks |
| maintenance/recover-stale | 同时恢复元数据、补建和资源等混合任务 | 已保留原恢复范围与响应；资源部分只处理受保护条件允许的旧原生认领，不改变统一任务 |
| metadata/backfill/已知批次治理与设置 | 采集或修改元数据/关系/旧任务状态 | 已通过白名单及持久写准入保持原语义；元数据更新由来源/内容代次触发器围栏，不因资源开关而下载。批次clone/rerun仍只选原本支持的元数据item |

迁移提示只包含固定路由、动作和原因，不回显请求中的过滤值、路径或URL。不支持的处理器不会被执行；可兼容入口和legacy分支仍遵循原协议。具体请求/响应主体见[API](../api/mg/migu-api.md)，不要把503当成“任务已受理”。

## 删除前捕获

云端回执与原生资源有级联外键，资源删除后原回执路径可能消失。MySQL外键级联不会调用被级联子表的DELETE触发器，因此只在资源表或回执表挂触发器无法覆盖任意祖先实体删除。应用删除入口在所属事务内自动capture，另保留显式维护capture供只读盘点和人工治理；证据不随资源删除。

```powershell
$tool = './build/msvc-debug/Debug/crawler_cloud_receipts.exe'
& $tool cleanup-capture --database '<明确库名>' --owner song --resource-ids '101,102' --writers-stopped
& $tool cleanup-preview --database '<明确库名>' --provider baidu --after-id 0 --limit 50
& $tool cleanup-status --database '<明确库名>' --provider gdrive --after-id 0 --limit 50
```

capture必须显式停止并排空所有writer，且三端全为maintenance；按排序资源、目标、回执锁序，在同一短事务捕获完整owner/id、资源代次、逐端目标修订/身份、原确认代次、安全精确路径/路径摘要、大小、SHA256、核验等级和任务状态。敏感URL/带query等不安全路径仅保存原路径SHA256，路径列写空，不复制敏感原文；原回执存在attempt或未释放批次身份时，快照按uncertain保守保留阻断。新增证据与治理审计同事务，失败整笔回滚；相同回执与相同事实重放不重复证据或审计。没有下载、云端元信息请求、资源删除或远端删除。

显式CLI的owner可以是song、singer、album、song_artifact，一次最多200个不重复正ID；其范围仍不自动扩大。`resources_missing`与`resources_without_receipts`分别提示资源已丢失、没有可捕获回执，均不能解释为“远端没有对象”。

### 应用删除的自动捕获

- `MySqlSongRepository/MySqlSingerRepository/MySqlAlbumRepository::coDeleteById` 在一个事务内锁父实体、全部来源及将被级联删除的资源；歌曲包含其MRC的全部 `song_resource_artifacts`。捕获两端回执及治理审计后才执行原父DELETE，任一步失败共同回滚。普通删除不能绕过本地原生认领、旧封面云端在途/未决、统一slot、活动文件任务或独立producer。文件任务的 `waiting_input` 也属于活动意图；取消会将其终止，不能因其没有slot就忽略，带旧attempt路径仍保留uncertain围栏。
- metadata writer的缺失音质占位资源清理同样自动捕获其双端回执及派生产物；专辑别名归一在原合并事务中先捕获旧资源，再按既有完整参考规则迁入事实并删除别名。别名允许按既有规则废止旧物料本地claim，保留规范资源的本地claim；持久占用和云端未决仍阻止合并。旧 `alias_retired` 归档继续保留。
- 正常应用删除不要求三端全局maintenance。资源锁与目标锁防止认领/目标修订竞态，捕获、旧字段归档、审计和删除共同提交；不会关闭其他资源的正常处理。显式CLI仍要求停写声明及全维护，预演仍仅在维护时给出可审阅候选。
- 尚未转换为统一回执的旧 `cloud_backend/cloud_path`、原校验值和大小另外保存在 `resource_cloud_migration_audit` 的 `resource_deleted` 证据中；不猜测rclone对应平台，不升级弱摘要。敏感/不安全路径只保存摘要。相同事实归档幂等，不修改旧证据时间戳。

单次自动父删除至多10,000条来源和10,000项文件资源（含artifact）；超过上限整笔拒绝，不能截断捕获后继续删除。不存在父实体返回未删除，不新增证据。本轮没有远端删除或文件删除。直接绕过应用的手工SQL/外部脚本不受仓储保护，应使用显式capture或迁到上述入口；MySQL外键级联不能替代该流程。

## 只读预演

preview/status在一个一致快照内按候选ID分页，每页默认50、上限200；支持单一provider筛选。它只读取已捕获证据，没有任意对象删除参数或远端执行端口。

| result_code | 含义 |
| --- | --- |
| source_retained | 原资源尚在或同ID已重建 |
| target_changed | 当前目标修订/指纹不匹配，或目标身份未核实 |
| active_or_uncertain_intent | 同资源/对象有活动槽、传输/producer或未决意图，或捕获时仍在运行/未决 |
| shared_reference | 同平台还有回执引用相同精确路径或相同受控对象键 |
| alias_evidence_requires_review | 别名归档保留了该对象的未决证据，需人工核对 |
| unsafe_or_unresolved_path / unknown_path_layout | 路径敏感/非法，或不能定位受控对象键；敏感旧URL只返回路径摘要 |
| unverified_evidence / confirmation_version_mismatch | 声明、弱证据或旧代次不能成为清理证据 |
| maintenance_required | 仍有端未进入维护，当前不应进入清理流程 |
| candidate_only | 库内引用及证据围栏通过，仅列入下一步人工/线上核对范围 |

共享引用核对使用原始精确路径以及`resources/v1/`后的受控对象键，remote改名不掩盖同对象引用。任意旧路径、仅basename匹配或无法规范化的路径不自动进入candidate_only；SHA256核验以外的证据也保持待审阅。别名迁移审计仍保留，不因原行删除就当对象无主。

所有输出固定`remote_delete_allowed=false`、`remote_deletions=0`、`evidence_complete=false`，每项都标记需要线上核对。candidate_only不表示远端对象当前存在、内容未变化或已经授权删除；未来真实删除必须增加当前对象身份/摘要核对、停止证明、引用复核与独立审阅，本轮不执行这些副作用。预演期间之后发生的数据变化也不能被当作永久保证。

大范围预演中的路径后缀及别名证据核对可能扫描历史数据，当前只有有界页与功能验证，尚未完成千万级索引/耗时验收；不能把单页成功当成全盘无遗漏。

## 数据库与验证

V2912新增resource_cloud_cleanup_candidates；无资源外键，原生/回执级联删除不会删除其证据。V2912扩展时不删除旧列；新增V2913在封存旧值后独立删除31个旧物理云列及2索引，已有44份迁移保持冻结，旧HTTP投影及三张legacy映射表保留，见[收缩说明](resource-schema-retirement.md)。不要将capture的数据库证据写入与preview的只读性质混淆。

`ResourceLegacyUpgradeGuidanceTest`验证拒绝路由、结构化升级要求、零旧handler调用及legacy/维护分支；`ResourceCloudCleanupPolicyTest`离线验证路径及保守分级；`MySqlResourceCloudCleanupTest`要求空的crawler_ownership_test_cleanup_*库，验证capture幂等/审计回滚、级联、四类身份、共享引用/目标变化/活动意图/别名及敏感路径隐藏。这些测试不调用真实网盘，实际执行结果由本轮交接记录给出。
