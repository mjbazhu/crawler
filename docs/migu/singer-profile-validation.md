# 歌手资料完善与验收记录

验收时间：2026-10-28 至 2026-10-29，时区 Asia/Shanghai。本文固定记录歌手资料完善的交付、实测结果及剩余缺口，供后续任务引用；数据数量是当次测试库快照，不代表持续更新的库状态。

13,119 条歌手来源已完成资料刷新，平台声明的歌曲、专辑总数全部成为已知值。修复默认头像覆盖后，原受影响的 689 条来源复测全部成功，已有头像 URL 全部保留。仍缺拼音 10,800 条、来源类型和地区各 1,367 条，另有 336 条已确认使用平台默认头像；这些保留为资料缺口，不推断填充。

## 交付版本与适用范围

| 项目 | 记录 |
| --- | --- |
| 功能 PR | [PR #5 完善歌手资料刷新并清理冗余字段](https://github.com/mjbazhu/crawler/pull/5)，已合并 |
| 实现提交 | `7917c79cd18fca26066afde3590791d5b812f92b` |
| 合并提交 | `02445add461945e29b9c9640c2e67196b771532a`，2026-10-29 合入 `main` |
| 分支基线 | `21b19a545d58d4299f65e31c765463f1a6837aba` |
| 测试库 | `crawler_ownership_test_main20_20261028_154114` |
| 来源构成 | 分类索引来源 11,752 条，歌曲或专辑带入的关联来源 1,367 条，共 13,119 条 |
| 公共名称池 | `singers` 13,091 行；名称复用不等于来源身份合并 |
| 测试库迁移 | 从 `2026102703` 升级到 `2026102801`，三个无用列均已删除 |

功能与表结构的长期规则分别见 [API 说明](../api/mg/migu-api.md)、[元数据模型](metadata.md)、[MySQL 表职责](../../sql/mysql/README.md) 和 [持久化流程](../architecture/persistence-flow.md)。本次交付恢复独立资料刷新，删除公共名称池的 `gender/status` 和来源表的 `metadata_status`，增加默认歌手头像保护及有限重试；来源分类和目录生命周期保持独立。

刷新默认选择任一声明总数为 NULL 的来源，0 是有效已知值。拼音随有效响应自然写入；未知的 `source_gender=0`、`source_country=NULL` 保留原样。索引归属以 `singer_index_memberships` 为依据，来源身份以 `source + source_singer_id` 为依据，不使用自增 ID 分界或名称判断来源。

运行库 `crawler` 未参与迁移或资料写入。本次记录确认的是测试库升级和验证，以及代码已合入 `main`；不代表运行库已部署该版本。

## 实现阶段验证

全量实测前只读抽样 14 条来源：6 条索引来源缺总数、6 条关联来源缺总数、2 条已有资料作为对照。14 条响应均为 HTTP 200、业务码正常且身份匹配；12 条缺口均返回两类声明总数，5 条返回原来缺少的头像 URL。14 条均未提供有效类型、地区或拼音，实际 C++ parser 与保存响应的总数一致。

独立测试库验证了保存响应回放、已知零总数、旧结构升级及重复迁移、来源身份与分类筛选、SQL prepare。四项额外集成回归覆盖列表/筛选、目录认领、超时恢复和歌曲关系，均在新建独立库通过；首轮测试造数仍写入已删列的失败记录完整保留，修正测试后再验证通过。相关 target 为 `MiguCatalogQueryIntegrationTest`、`MiguBackfillClaimIntegrationTest`、`MiguMetadataRecoveryIntegrationTest`、`MiguSongRelationIntegrationTest`。

这一阶段未写入 main20 测试库；后续全量阶段按用户授权迁移并写入该库。各阶段的“未写入”结论只适用于各自执行范围。

## 全量刷新与数据恢复

全量阶段发生在默认头像保护和自动重试修复前。正式入口使用 `force=true,limit=0`，覆盖全部 13,119 条来源，没有重复或遗漏。

| 批次 | 目标数 | 成功数 | 失败数 | 说明 |
| --- | ---: | ---: | ---: | --- |
| `batch_1793200870850_0` | 13,119 | 12,789 | 330 | 328 次未解析到有效实体，2 次请求失败；该历史批次保留 `Failed` |
| `batch_1793203433251_1` | 330 | 330 | 0 | 同一入口按失败来源 ID 显式重试，全部恢复 |

两轮合计使全部来源至少完成一次成功资料刷新。字段缺失数量如下；“刷新后”包含随后对 689 条旧头像地址的恢复。

| 字段或检查项 | 刷新前缺失 | 刷新后缺失 | 解释 |
| --- | ---: | ---: | --- |
| 名称 | 0 | 0 | 公共名称完整 |
| 标准化名称 | 0 | 0 | 名称池去重字段完整 |
| 平台声明歌曲总数 | 13,099 | 0 | 500 条为有效零总数 |
| 平台声明专辑总数 | 13,099 | 0 | 1,306 条为有效零总数 |
| 头像 URL | 1,199 | 0 | 非空不代表真实头像完整，仍有 336 条已确认默认图 |
| 来源拼音 | 10,800 | 10,800 | 索引来源 10,726 条，关联来源 74 条 |
| 来源类型 | 1,367 | 1,367 | 全部为关联来源，保留未知 |
| 来源地区 | 1,367 | 1,367 | 全部为关联来源，保留未知 |

头像质量核查发现 1,025 条来源使用相同地址，查看该资源后确认为平台灰色通用人像。其中 689 条原有非空 URL 被刷新覆盖、335 条原先无 URL、1 条原来已使用默认图。恢复脚本按来源/资源主键和当前默认 URL 限定目标，在事务内恢复那 689 条旧地址；恢复后已确认默认图为 336 条，其中索引来源 1 条、关联来源 335 条。

新增的 1,199 条 URL 中有 864 条不是该默认地址，尚未逐一下载核验图像内容。仅为识别通用人像保存了一张诊断图片，采集流程没有下载头像或修改资源下载字段。

来源身份、类型/地区以及全部目录状态、计数、时间和认领字段保持一致；其他 14 张受保护表的全行指纹一致。两类目录均完成的来源仍为 20 条，资料刷新不证明全库目录已采集完成。原始全量、重试后和头像恢复后的快照分别保留，没有用修正后的数据覆盖历史证据。

## 默认头像修复后的回归

修复在 Migu 解析边界统一过滤已核验的默认头像资源路径，复用空值不覆盖旧地址的现有写入策略。资料刷新对临时请求故障、5xx、无效 JSON 或空实体提供有限重试，429 单独分类；身份错配、明确业务拒绝、其他 4xx、解析器异常和持久化错误不重试。完整接口与退避规则以 API 正文为准。

回归目标严格来自前述恢复记录中的 689 个来源主键，再映射为平台 ID；没有根据 ID 阈值或姓名选择目标。正式路由按显式 ID、`force=true,limit=0` 分两批执行。

| 批次 | 目标数 | 成功数 | 失败数 |
| --- | ---: | ---: | ---: |
| `batch_1793205875244_0` | 500 | 500 | 0 |
| `batch_1793205936576_1` | 189 | 189 | 0 |

689 条头像 URL 全部保留，没有变成空值或默认图；`singer_resources` 整表指纹一致。声明总数没有改变或缺失，来源身份、类型、地区、拼音及目录状态/计数/时间/认领字段逐行一致。未选择的歌手来源与资源整行一致，其他 16 张表指纹一致，头像下载数为 0。

这两批实际没有触发追加重试。重试成功、上限、禁用、取消、超时和不可重试错误由离线及独立 MySQL 故障用例验证；不能把这 689 次正常响应作为真实平台退避的验证结果。最终来源仍为 13,119 条，默认头像仍为 336 条；默认补缺请求返回 HTTP 200、`batch_id=null`、选择 0 条。

修复后真实平台回归范围为上述 689 条，没有再次执行全库刷新。全量目录中修复待办标记反映的是修复前阶段，最终保护逻辑与回归以本节及 `fix689` 证据为准。

## 构建与集成验证

- MSVC 构建服务及相关测试 target 通过。最终 10 项离线回归全部通过：`MiguApiDocumentTest`、`MiguCatalogApiSmokeTest`、`MiguHttpRouteSmokeTest`、`MetadataOwnershipTest`、`MetadataIngestionWorkflowTest`、`CatalogMetadataCompletionTest`、`SingerProfileRefreshTest`、`MiguSingerIndexParserTest`、`MiguSongSourceParserTest`、`CrawlerServiceResourceBatchEventTest`。
- 独立库 `crawler_ownership_test_singer_profile_fix_20261029_004236` 中，`MySqlSingerProfileRefreshTest` 通过，覆盖旧结构升级、默认头像保留、503/空实体重试、上限、401/业务拒绝/身份错配不重试、成功清理旧错误和目录隔离；同库 `MySqlMetadataOwnershipTest --check-sql-only` 成功 prepare 160 份运行期 SQL。
- schema 检查中的 31 份 migration 与清单一致；既有 `V2026102602` 仅纠正清单中的过期 checksum，没有改写旧迁移文件。
- 六个实现文件的 clangd 语义检查为 0 错误，检查时关闭 IDE tweak 自测。早期仅由 tweak 自测产生的错误、测试造数失败及后续通过日志均保留。
- 本机构建遇到共享 PDB 冲突与部分测试的 PDB 容量限制，采用串行构建，并通过仓库外的 `local-link-options.cmake` 仅为 7 个 MySQL 测试关闭 PDB 链接；未改动全局工具链。689 条实测之后的源码变更仅为标准库直接 include，最终再次完成构建和 10 项离线回归。
- 差异、UTF-8 无 BOM 和 Markdown 链接检查通过。PR 合并前 GitHub 没有 CI 检查记录，以上属于本地验证；本次文档整理也不代表重新执行平台或数据库测试。

测试选择、环境条件与两个新 target 的覆盖范围统一维护在 [验证指南](../agents/validation.md#按任务选择验证)。

## 剩余缺口与后续边界

拼音继续随歌曲、专辑及资料的有效响应自然补入。关联歌手的类型和地区目前没有可靠返回值；歌曲或专辑采集不会自动推导这些分类，需要分类索引或其他明确来源才可补充。不得依据姓名、语言或专辑署名猜测。

336 条平台默认头像仍属于真实图片缺口；默认图保护只识别本次已核验的资源路径。保留的 689 条 URL 及其余非默认地址没有逐图验证可用性，本次没有接入歌手或专辑图片下载。

资料刷新已支持有限自动重试，超过上限的失败可以按来源 ID 再次提交到同一路由。本次没有将通用失败批次重跑扩展为新的 item 类型，也没有因为可选字段缺失增加单字段查询或无限全量请求。

## 证据目录与复核入口

下列目录位于当次测试机器，不随仓库分发。仓库保留结论与证据索引；跨机器复核原始响应、快照或日志，需要提供这些外部目录的副本。原始数据、数据库快照、环境配置、缓存和构建产物不作为文档提交内容。

| 位置 | 主要证据 |
| --- | --- |
| `D:/codex-runs/crawler-db-field-analysis/main20_20261028/` | `singer-scope-profile.json`、`singer-scope-read-only-queries.json`、`database-profile.json`、`supplement-profile.json`、`source-field-evidence.json`：刷新前只读基线与字段归属 |
| `D:/codex-runs/crawler-main-live20/20261028_154114/` | main20 原有两轮元数据实采响应 |
| `D:/codex-runs/crawler-singer-profile-completion/20261028_probe/` | `probe-summary.json`、`responses/`、`integration-results-20261028_225123.json`、`integration-results-20261028_225815.json`、`integration-results-20261028_230702.json`、`source-database-receipt.json`：14 条抽样及独立库迁移、列表、认领、恢复、关系验证 |
| `D:/codex-runs/crawler-singer-profile-completion/full_20261028_231604/` | `final-summary.json`、`final-residuals.json`、`default-avatar-sources.json`、`avatar-restoration-receipt.json`，以及 `before/`、`post-start/`、`after/`、`after-retry/`、`after-avatar-restore/`：全量、重试与数据恢复 |
| `D:/codex-runs/crawler-singer-profile-completion/fix689_20261029_003515/` | `final-summary.json`、`verification-689.json`、`target-689.json`、`batches-started.json`、`final-items.json`、`fixture-results-20261029_004236.json`、`final-offline-tests.log`、`final-build.log`、`clangd-results.json`、`final-api-receipt.json`，以及 `before/`、`after/`：修复后回归与指纹核对 |

两次临时平台测试服务均已停止：全量服务 PID `55036` 于 2026-10-29 00:13 停止，689 条回归服务 PID `51720` 于 00:51 停止，端口均为 `19081`。各阶段的 `server.json`、`server-stop.json` 保存运行设置、二进制 SHA256 和停止凭据；`fix689/final-summary.json` 同时记录实测及最终构建的二进制 SHA256。

后续实测按新的日期、代码版本、数据库和目标范围补充结果；本记录保留本次证据边界，不因 `current.md` 切换任务而覆盖。
