# 资源旧云物理列收缩与旧协议退役

本轮已经实现 V2913 前向收缩与新回执读写，不删除旧 HTTP JSON 协议。实际开发 `crawler` 已在同备份克隆完整核验后完成旧云物理列清理；本轮状态与证据见[数据库迁移记录](resource-database-migration.md#v2913-物理云列收缩状态)。旧协议、兼容批次和映射表的整体退役属于后续范围，其兼容窗口门禁不阻碍已经保留这些接口的本次物理收缩。

## V2913 的固定范围

新增 `V2026102913__retire_resource_cloud_columns.sql`，不新增表，不修改原44份冻结 migration。

| 对象 | 本次处理 |
| --- | --- |
| `song_resources` | 删除 `cloud_backend/cloud_path/cloud_uploaded_at` 3列 |
| `singer_resources`、`album_resources` | 各删除13列：`cloud_backend/path/size_bytes/checksum/status/uploaded_at/claim_generation/claimed_at/running_batch_id/error_category/result_code/last_error/attempt`；均带 `cloud_` 前缀 |
| `song_resource_artifacts` | 删除 `cloud_backend/cloud_path` 2列 |
| 旧云队列索引 | 删除 `idx_singer_resources_cloud_queue`、`idx_album_resources_cloud_queue` |
| 本地与业务数据 | 保留四资源表的归属、路径、大小、旧摘要、状态、内容/来源/目标代次及原业务数据 |
| 当前云事实 | 歌曲、封面、派生产物及兼容查询统一读写 `resource_cloud_receipts`；不将弱旧工具结果提升为 SHA 确认 |
| 旧接口与映射 | HTTP `cloud_*` 字段仍按新表投影；`resource_legacy_profiles/batches/operations` 三表保留 |

旧歌曲单云视图优先固定 `migu` profile；尚未固定时只在唯一一端有路径时显示，多端有路径不猜平台。backend 来自该回执真实 `source_adapter`，为空时不从当前配置补造。百度可用 baidupcs 或显式映射的 rclone，rclone 不等于 Google。旧协议写回同样使用新表，并保护历史 skip、强回执、未决意图和持久占用。

V2913 在三目标 maintenance 且没有待执行/在途/未决任务或原生认领时执行。非默认旧云值先逐行写入现有 `resource_cloud_migration_audit`，固定 run `schema-retirement-v2026102913`、`disposition=archived`，`old_values` 顶层保存原物理值，SQL NULL 保存为 JSON null。未识别平台只归档；明确百度旧值可附加到该端 `evidence.retired_legacy`，不覆盖已有证据键，也不改正式上传时间、核验、confirmed 或 skip。

封存与 `crawler_cloud_archive` 标记同事务提交，随后重建两类封面来源触发器、逐表删除旧列。每表 DROP 原子提交；中断重跑接受每表全列或零列，尚未删除的原值须仍与档案一致，半列结构或源值变化拒绝继续。历史导入、删除和别名归一从不可变档案读取旧值，从新表读取当前云事实，不依赖已删物理列。完整恢复顺序见[数据库迁移](resource-database-migration.md)。

## 只读依赖盘点

```powershell
python scripts/resource-retirement-audit.py inventory --repo '<crawler 工作目录>' --output '<新的依赖报告.json>'
```

脚本合并四资源表当前快照与 V2913 固定 DROP 定义，删列后仍能盘点31列及2索引；同时将 `resource_legacy_profiles/batches/operations` 作为未来旧投影退役候选。它限定扫描 include、src、sql/mysql 和 docs/api/mg，输出文件/行号/符号及文件摘要；不输出原始行，不扫描私有 env、媒体正文或运行数据库。

报告把生产代码、运行 SQL、公开契约、schema 检查、结构快照和冻结迁移历史分开。历史 migration 的引用继续保留，不能因为盘点里有旧字段就反向改 V2902..11 或后续已执行版本。本地回执、内容/来源/目标版本、skip、统一云回执、审计和未决意图不是删除候选。

两种依赖指标解决不同问题：

| 指标 | 范围与用途 |
| --- | --- |
| `physical_sql_dependencies` | 运行 SQL 对四资源表旧列的实际限定、INSERT/UPDATE 等访问；排除迁移历史、create 快照、JSON 键、回执列和 DTO 别名；本轮需为0，并在无旧列隔离库 prepare 验证 |
| `runtime_dependencies` | 原有广义旧协议符号依赖，包含仍合法的兼容 DTO/投影等；仅用于未来全部旧协议退役，不要求本次清理同时归零 |

物理扫描是固定 SQL 的定向词法检查，不能代替删列后实际 prepare、触发器和动态 SQL 审查。2026-10-30 只读扫描430份非迁移/非create SQL，物理旧列访问0；兼容接口继续存在，广义 runtime 命中保留。盘点工具不执行 DROP、不删除源码、不修改数据库。

## 未来全部旧协议退役的门禁

下面保留原 `check` 的发布与批准语义，只用于将来删除旧接口、旧 workflow 与三张 legacy 映射表。它不批准 V2913，也不作为本次保留兼容投影的物理删列步骤；不能为了让该检查通过而删除仍在使用的 JSON 字段。届时先完成[上线证据检查](resource-release-readiness.md)，由操作方提供一份审阅过的 attestation：

```json
{
  "format": "crawler.resource-retirement-attestation.v1",
  "inventory_sha256": "<当前完整盘点摘要>",
  "release_scope_sha256": "<本轮上线范围摘要>",
  "compatibility_started_at": "<带时区的兼容期开始时刻>",
  "compatibility_ends_at": "<带时区的兼容期结束时刻>",
  "last_legacy_request_at": "<带时区的最后旧写调用时刻>",
  "minimum_compatibility_days": 14,
  "old_clients_remaining": 0,
  "legacy_writers_remaining": 0,
  "unresolved_remote_objects": 0,
  "history_adoption_complete": true,
  "backup_restore_verified": true,
  "legacy_projection_export_verified": true,
  "legacy_projection_export_sha256": "<旧投影归档摘要>"
}
```

示例 14 天只展示字段，兼容窗口须由项目实际部署计划明确，不是工具替项目规定的时长。字段应来自执行记录和观测，不能把未知值填写成 0。明确停用的旧路由必须有调用方升级/停用记录；仍保持 503 的写入口本身不证明客户端已经退出。

```powershell
python scripts/resource-retirement-audit.py check --repo '<crawler 工作目录>' --attestation '<已审阅声明.json>' --release-check '<上线检查结果.json>' --output '<新的收缩检查结果.json>'
```

检查重新扫描当前源码，旧盘点摘要不匹配即拒绝；要求生产引用清零、上线证据完整、窗口至少达到明确天数且已经结束，最后旧写调用不晚于窗口开始，剩余旧客户端/writer/未决对象均为零，历史采信、备份恢复和旧投影导出均有确认。返回码 2 表示尚不满足；通过仍为 `drop_authorized=false`，只能作为单独收缩评审的输入。

## 本次物理收缩的执行与验证顺序

1. 停止写入并核对三目标 maintenance、原生认领、持久槽、待执行任务及未决意图；实际开发库服务未运行，无需先启动业务。
2. 备份当前 V2912 数据、结构和触发器，记录摘要，在独立实例从同一备份先恢复数据、后恢复触发器。
3. 克隆库执行 V2913，检查31旧列/2索引消失、档案与原值一致；原业务字段、本地路径/旧摘要/skip和现有强回执/策略保持一致。历史清单摘要及完成 run 重放不应改变。
4. 验证全部运行 SQL prepare、歌曲/封面条件写回、历史导入/基线、别名/删除及旧 HTTP 投影；验证旧意图拒绝、分表 DDL 中断续跑与完整重跑。SQL/二进制必须匹配 V2913，旧二进制不能再直接访问旧列。
5. 克隆检查通过后执行实际 `crawler` 同样的迁移与前后数据核对；保留原始失败和最终结果。迁移不激活基线、不启动 HTTP/worker、不产生内容 I/O 或远端删除。

当前本轮隔离 V2912→V2913、旧意图拒绝/归档/单表 DDL 恢复测试及422份运行 SQL prepare已有通过记录；34项离线工具自测通过，覆盖实际物理访问与合法投影的区别，原 `check` 逻辑未放宽。全量克隆与实际库均已完成收缩，37表6,037,061行保留字段逐值一致、45,558条旧云值精确归档、1,505,955条历史快照差异0；32项相关回归及旧清单重放通过。实际记录见[迁移状态表](resource-database-migration.md#v2913-物理云列收缩状态)。
