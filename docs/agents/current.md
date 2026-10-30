# 当前交接：四表回执与统一三路执行的精简方案讨论

更新日期：2026-10-30。worktree `C:/Users/nidie/.codex/worktrees/crawler-main-20261029/crawler`，分支`codex/resource-cloud-receipts`。当前检查点`6d59b668919a05af359ea8b179aced3445a5fc75`的10笔本地提交已通过精确force-with-lease推送，两端SHA一致；未创建PR或合并main。旧两笔历史保存在`codex/checkpoint-v2913-before-resplit-20261030`。本轮按用户授权提交并推送5份方案文档，再从包含本交接的最新HEAD新建`codex/resource-receipts-four-tables`及独立worktree，准备新会话；不修改代码、执行迁移、创建PR或合并main。实际路径和分支创建结果以交付记录及git worktree list为准。

## 当前权威方案

后续先读[四类云回执拆分与资源运行精简](../roadmap/resource-receipts-four-tables.md)。旧roadmap保留V2913设计/验收历史，旧基线/发现/兼容要求不自动延续为新目标。

已确认：song/singer/album/song_artifact四类各一张云回执表，表内用baidu/gdrive区分平台；下载、生成和关联资源保持一个统一任务体系，不按资源类型再拆任务表。用户明确保留MRC派生生成和音频关联下载；旧crawl_batches/items/events记录本轮暂留，不迁移、不重放。

实际91,116条回执全部属于song、resource_id=song_resource_id，其余归属键全NULL；百度/Google各45,558条。因此歌曲回执采用原表RENAME及约束收缩，保留原id/数据；另3张新建空表。代码仍须改四表读写/绑定，但不导出重灌、不重新采信或重算旧摘要。命名和直接FK见权威方案。

其余16张表中保留6张正常运行表(destination、operations、tasks、slots、producer、related)，退役10张历史治理/兼容表。历史backfills8行、audit1,551,513行先一次性归档；其余待删表为空。现有skip和来源标记转为当前事实，移除基线准入，不自动把1,460,397个旧excluded排成下载任务。普通旧任务原表有484批/124,523明细/525,022事件，原样保留。

## 本次方案复审补充

已对照6d59b66源码/SQL补齐四表及退役依赖：混合owner批量ensure/PreparedQueries/status映射、普通任务与producer的legacyClaimAllowed共用调用、baseline在claim/current/producer中的全端闸门、capture内的删除guard、MRC/关联子操作完整事务及冻结父子JSON。明确新表真实FK、rename中断状态矩阵、子表先DROP及错误半成品拒绝；详细边界和针对性验收已写入权威方案。

保留受控封面内容读取、目录/只读preview和正常元数据任务查询/停止；只移除旧资源写链及其投影，避免按legacy字样误删共用功能。完整新版本上线前才检查改动相关行为，本次不运行数据库/云端/性能测试、不执行DDL。

## 实施边界

原表更名只省数据搬运，不能省四表SQL/参数/查询/删除合并调整。继续复用ResourceTransferFactory、独立来源/worker、逐端预算/取消及既有校验，移除legacyCloud/legacyProvider、旧单端流程、旧资源API/DTO/映射/observer等第二套逻辑。保留元数据采集、目录、授权等正常业务；资源写入以统一v2服务为唯一链路。旧URL和字段的断约处理见方案，实施前按调用方清单复核。

不再执行旧全历史preview/apply/review/seal/activate。目标状态使用现有destination行和最小维护CLI，单端暂停不阻断其他端；先在克隆按新前向迁移验证再处理实际库，已有45份migration冻结，V2911换行例外不变。完成新结构后才落实配置、必要启用检查和少量真实写入。MRC/关联表不删，正常并发/恢复不重写为另一套引擎。

## 已完成事实与证据

实际crawler仍V2026102913/API1.14.0、39张表，三端maintenance、baseline_ready=0，统一operation/tasks等当前为0；业务服务未启动。歌曲1,505,955条，本地路径/skip各45,558，回执91,116；历史本地文件存在性按用户要求不验，路径和旧校验值保留。Google未解析路径仍45,558，不伪造SHA/上传成功。

此前克隆和实际库37表6,037,061行保留字段逐值一致、原1,505,955条历史快照差异0、31旧列/2索引已删；32项相关回归、34项工具自测、422份SQL prepare通过。这些是此前日期/代码的实测，本轮只读核对和文档检查，不重复实测或冒称四表版本已通过。

公开证据`D:/codex-runs/crawler-cloud-receipts-20261029/cloud-retirement-20261030`；本轮只读方案证据`D:/codex-runs/crawler-cloud-receipts-20261029/four-table-plan-20261030/plan-inspection.json`。备份及原始日志在E盘受限目录。重分批与推送记录在`resplit-20261030`；这轮之前的交接已备份为`four-table-plan-20261030/handoff-before-plan.md`。原3306运行，自持6111已关闭，数据与失败现场保留。

## 新会话接续顺序

1. 选择新worktree，确认分支为`codex/resource-receipts-four-tables`、工作区干净，起点是包含这5份方案文档的共同HEAD。源分支`codex/resource-cloud-receipts`保留方案检查点，不在原目录继续四表实现。
2. 先读AGENTS.md、docs/agents/README.md、任务分类和本交接，再读权威四表方案。方案已经过源码/SQL复审，目标是原位更名歌曲回执、另建3表、保留6运行表、退役10治理/兼容表，MRC及关联下载继续共用任务系统。
3. 本轮仅准备新分支/worktree和交接，没有实施代码或新DDL。新会话先核对现状与方案，收到实施指令后才开始改代码；实际库清理、业务启用仍按用户授权范围和克隆先行步骤处理。
4. 不重新导入1.5m历史，不将旧crawl任务迁移/重放，不直接DROP全部16表，不启动旧二进制或按旧基线封存/激活流程继续开发。保留原业务、本地路径/旧摘要、91,116回执和45,558跳过事实。
5. 独立worktree不复制忽略的.env、数据库、构建缓存或网盘认证。本次不要求重新登录；后续显式选择既有私有配置和独立验证库，避免dotenv默认回退到实际crawler。不得在源码或日志写入认证。
6. 先前32项回归、34项工具自测、422份SQL及并发/云端实测属于6d59b66实现的历史证据。单纯Git与文档交接不重跑；实现变更后只验证受影响行为，尤其混合owner批量原子性、MRC/关联事务、单端隔离、删除guard和新迁移数据保全。
