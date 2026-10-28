# 当前任务与会话交接

更新日期：2026-10-29。本会话处理歌手信息完善、无用字段清理和独立资料刷新入口恢复。已从 `origin/main` 基线 `21b19a545d58d4299f65e31c765463f1a6837aba` 创建并关联独立 worktree。

- 开发目录：`C:/Users/nidie/.codex/worktrees/singer-profile-completion/crawler`。
- 开发分支：`codex/singer-profile-completion`。
- 原目录：`C:/Users/nidie/Desktop/ai-code/py-openai/backend-cpp/modules/crawler`，保持在 `main`；本会话的后续改动只在上述开发目录执行。
- 本轮已完成只读分析、小样本实采、资料刷新恢复、字段清理、独立测试库迁移及回归；用户已授权提交、推送功能分支并创建面向 `main` 的 PR，具体交付标识以 Git 记录和会话附件为准。
- 用户随后授权在 main20 测试库实际全量补充并分析残留缺口；已完成升级、13,119 条来源全量刷新、330 条失败重试及头像退化数据恢复。此前的“未写入”结论只适用于实现阶段。运行库 `crawler` 不参与写入。
- 最新一轮已修复默认头像覆盖和资料有限重试，并对原来受影响的 689 条来源真实强制刷新：689 条全部成功、原头像 URL 全部保留、默认图覆盖为 0。见文末复测记录；本轮没有重跑全库。

## 当前目标与约束

1. 完善歌手资料及平台声明的歌曲、专辑总数，覆盖分类索引歌手以及歌曲、专辑响应带入的关联歌手。
2. 清理 `singers.gender`、`singers.status`、`singer_source_refs.metadata_status`，同步处理失去用途的相关索引、写入参数、查询列顺序、C++ 映射、API/DTO、OpenAPI、schema 检查和测试；新增 migration，不修改已执行迁移。
3. 恢复 `POST /api/migu/singers/profile/refresh-from-db` 的独立资料刷新能力，复用既有请求、解析、批次与事务持久化；不因刷新资料而采集整套歌曲或专辑目录，也不默认给索引请求附加全库资料请求。
4. 歌手拼音继续随歌曲、专辑及资料整份响应自然写入，不增加单字段查询，不根据名称自行生成平台拼音。
5. `source_gender=0`、`source_country=NULL` 的未知值保留；不依据姓名、语言、合作歌手或专辑推断，不用空响应覆盖已有分类。
6. 资料刷新不修改目录同步状态、同步数量、同步时间或认领信息；网络请求在事务外，同响应的有效实体、来源、总数及头像 URL 原子保存。
7. 按 `source + source_singer_id` 区分歌手来源，索引归属检查 `singer_index_memberships`，不得硬编码 `id>=11753` 判断来源。
8. 本轮聚焦歌手相关问题，不扩大歌曲、专辑结构清理，不接入歌手或专辑图片下载。迁移试验已在独立库通过；按用户后续授权将已验证迁移及全量资料刷新应用到 main20 测试库，不在运行库 `crawler` 试验。

## 已确认的数据基线

刷新前只读核查数据库：`crawler_ownership_test_main20_20261028_154114`。该库来自 `main / 21b19a5` 的两轮真实元数据采集；下列是全量刷新前的历史基线，最新执行状态见文末，不代表全量刷新后的数据。

| 项目 | 分类索引来源 | 关联新增来源 |
| --- | ---: | ---: |
| 来源总数 | 11,752 | 1,367 |
| 姓名缺失 | 0 | 0 |
| 类型未知 | 0 | 1,367 |
| 地区缺失 | 0 | 1,367 |
| 头像 URL 缺失 | 0 | 1,199 |
| 拼音缺失 | 10,726 | 74 |
| 声明歌曲总数缺失 | 11,732 | 1,367 |
| 声明专辑总数缺失 | 11,732 | 1,367 |
| 两类目录均已完成 | 20 | 0 |

- 公共名称池 `singers` 为 13,091 行，真实来源 `singer_source_refs` 为 13,119 行；名称复用不等于来源重复。
- 来源表的索引歌手 ID 为 1..11752，关联来源从 11757 开始；公共名称池中有 7 条关联来源复用了分界之前的名称记录，不能以自增 ID 作为业务来源标志。
- `singers.gender` 全为 0、`singers.status` 全为 1、`singer_source_refs.metadata_status` 全为 1；当前写入固定这些值，没有有效资料完整度语义。
- 已完成目录的 20 位歌手仅有「あいみょん」「サカナクション」缺拼音，已保存的匹配响应没有提供有效值。
- 全量分类索引已采完，资料请求只覆盖 20 位；资料缺失不能直接表述为全量歌手索引漏采。

基线分析证据目录：`D:/codex-runs/crawler-db-field-analysis/main20_20261028/`，重点查看 `singer-scope-profile.json`、`singer-scope-read-only-queries.json`、`database-profile.json`、`supplement-profile.json` 和 `source-field-evidence.json`。历史 main20 实采响应位于 `D:/codex-runs/crawler-main-live20/20261028_154114/`；本轮新增抽样及验证记录另存如下。

## 本轮实现与行为

- 恢复 `POST/OPTIONS /api/migu/singers/profile/refresh-from-db`，返回可查询的 `singerProfileRefresh` 批次。请求示例为 `{"limit":50}`；显式目标使用 `source_ids:["123"]`。默认只选任一声明总数为 NULL 的来源，0 是已知值；`force=true` 重取已知资料，`limit=0` 选择全部匹配来源，正数上限 500。显式 ID 去重，选定目标后再启动批次。
- DTO 严格校验 JSON 类型、来源、数值范围、筛选长度及正整数 ID；拒绝未知键和显式空 ID 数组，避免参数拼写错误扩大目标范围。已知资料全部跳过时返回 200，无请求/解析/查询/事务 writer 装配时返回 503，创建批次返回 202。
- `SingerCatalogWorkflow::fetchAndSaveSingerSourceProfile` 改为 `ServiceResult`，复用 `CatalogMetadataCompletion::singerContext` 校验 HTTP 状态和目标身份；解析器拒绝错误业务码，持久化失败不能报告成功。沿用 `MetadataPersistenceService`、`MetadataBatchBuilder` 和原子 writer，不增加第二套逐字段补齐实现，体现 DRY 与 KISS。
- 移除独立资料刷新中的目录状态回写。刷新仅保存响应提供的实体、来源、总数、拼音及头像 URL，不触碰目录状态、同步时间、同步数量或认领信息，不请求歌曲/专辑目录，不下载图片。
- 新增 `V2026102801__singer_profile_field_cleanup.sql`，删除 `singers.gender/status`、`idx_singers_status` 和 `singer_source_refs.metadata_status` 并重建来源视图；保留来源类型、地区和身份。同步更新建表快照、写入/查询 SQL、C++ 列映射、API 与两份 OpenAPI。API 不再返回公共 `status`；`gender` 继续来自 `source_gender`。
- 更新 schema 最新版本及校验清单。同时纠正清单中已有 `V2026102602` 的过期 checksum；已核对 main、worktree 与 Git 中该迁移内容一致，未改写任何既有迁移。全部 31 份迁移文件与清单一致。
- `normalizeMiguSingerAvatarUrl` 统一将已核验的默认歌手头像归为空值，索引、资料及歌曲/专辑关联歌手共用该规则；沿用原有空值保留旧地址的事务写入，不将平台判断散入通用 SQL，不改变歌曲/专辑封面规则。
- 批次执行器接受具体用例显式启用的有限重试，资料刷新使用启动时 `max_retries`；默认其他批次重试上限仍为 0，避免与各自策略重复。临时请求、5xx、无效 JSON/空实体可重取，429 至少等待 20 秒；等待时检查取消和超时。身份错配、明确业务拒绝、其他 4xx、解析器异常和数据库错误不重试。
- 批次 parser 增加兼容的 `ServiceResult` 返回入口，保留明确业务拒绝的语义，避免将所有空解析都当瞬时故障；未新增第三套结果类型。重试次数及 `itemRetryScheduled` 事件落库，成功后清除 item 的旧错误；更新 `crawl_batch_items/update_item.sql`，未新增或修改 migration。

## 实现阶段验证（全量实测前）

已读取根 `AGENTS.md`、`docs/agents/README.md`、任务分类指南、开发规范、验证要求、Git 流程、元数据模型、MySQL 表职责、相关 API/源码及构建/格式配置。以下为全量实测前的验证，证据位于 `D:/codex-runs/crawler-singer-profile-completion/20261028_probe/`。

- 真实平台只读抽样 14 条：6 条索引缺总数、6 条关联缺总数、2 条已有资料对照；全部 HTTP 200、业务码正常、ID 匹配。12 条缺口均返回歌曲及专辑总数，5 条返回原先缺少的头像地址；14 条均未返回 gender、country、拼音。实际 C++ parser 与保存的响应总数一致。详见 `probe-brief.json`、`probe-summary.json` 和 `responses/`。
- 回归先捕获旧实现错误：HTTP 503 响应可被错误地作为成功资料保存；修复后新 `SingerProfileRefreshTest` 覆盖有效零总数、原子输入、未知分类、头像仅 URL、HTTP 错误、身份错配、错误业务码、事务失败和重试。
- 最终 8 项离线回归全部通过：`SingerProfileRefreshTest`、`MetadataOwnershipTest`、`MetadataIngestionWorkflowTest`、`CatalogMetadataCompletionTest`、`MiguSingerIndexParserTest`、`MiguApiDocumentTest`、`MiguCatalogApiSmokeTest`、`MiguHttpRouteSmokeTest`。日志为 `final-offline-tests.log`。
- 独立 MySQL 验证 4 项全部通过：14 条保存响应回放、零总数控制、历史身份/类型迁移与筛选、160 份运行期 SQL prepare；数据库后缀为 `20261028_225123`，汇总见 `integration-results-20261028_225123.json`。
- 在最终业务逻辑再次运行资料回放和零总数两项，均通过；数据库后缀为 `20261028_225815`，汇总见 `integration-results-20261028_225815.json`。覆盖默认补 12 条、强制刷新 14 条、重复刷新跳过、显式 ID 去重与 limit、旧结构升级与重复迁移。来源分类、拼音及目录状态/数量/错误/时间/认领字段逐行指纹一致，歌曲/专辑行数为 0，图片下载为 0。
- 补充验证列表和认领时发现既有测试造数仍写入已删列，首轮失败记录为 `catalog/claims-20261028_230448.log`；已同步修改 4 个相关集成测试的公共名称池 INSERT，保留各来源分类。随后 `MiguCatalogQueryIntegrationTest`、`MiguBackfillClaimIntegrationTest`、`MiguMetadataRecoveryIntegrationTest`、`MiguSongRelationIntegrationTest` 在 4 个新库全部通过，汇总见 `integration-results-20261028_230702.json`，证明删列后真实列表/筛选、认领映射、超时恢复及歌曲关系正常。
- main20 库歌手名称、来源、索引归属、头像资源表全行 SHA256 与抽样前相同，见 `source-database-receipt.json`；未对其迁移或写入。所有迁移试验在新建独立库完成。
- 生产服务及相关测试 target 构建通过。clangd 初次检查中 3 个文件仅触发 IDE 重构 tweak 自测错误，关闭 tweak 后语义检查通过；另 3 个文件直接通过，最终 `CrawlerService.cpp` 语义检查为 0 错误。记录为 `clangd-*.log`。最终差异检查及 UTF-8 无 BOM 检查通过。
- 构建环境曾出现并行共享 PDB 写入冲突和部分大型测试的 PDB 容量错误；本机改为串行构建，并仅在外部 `local-link-options.cmake` 为 7 个 MySQL 测试关闭 PDB 链接，未改动仓库的全局工具链设置。

## 未覆盖项与后续

- 全量与一次失败重试已完成，当前总数和头像 URL 均无空值；仍缺拼音 10,800 条、关联歌手类型及地区各 1,367 条，另确认 336 条使用平台默认头像。未知值保持原样，不额外推断。
- 默认头像合并缺陷已修复，并通过 689 条真实来源回归。识别范围是本轮实际核验的默认资源路径，历史仍有 336 条默认头像待取得真实图片；没有把 URL 非空当作真实头像齐全，也没有删除或猜测其资料。
- 资料刷新已支持有限自动重试；超过上限的失败仍可按来源 ID 通过同一路由重新提交。本轮不扩展通用批次克隆/失败重跑到新的 item 类型。
- 按用户本次授权交付功能分支和 PR，遵循 `docs/agents/git-workflow.md` 精确暂存并保持删列、SQL 绑定和依赖查询完整配套；不在 main 直接修改。

## 全量实测最终结果（2026-10-28 至 2026-10-29）

- 证据目录：`D:/codex-runs/crawler-singer-profile-completion/full_20261028_231604/`。汇总为 `final-summary.json`，逐条残留为 `final-residuals.json`，默认头像来源为 `default-avatar-sources.json`。`before/` 保存升级和采集前 22 张表的数据、结构及全行指纹；`post-start/` 证明启动只改变版本记录和两张删列涉及的表。
- 实际核查 main20 原来仍为 `2026102703` 且三个旧列存在；启动本分支 `demo.exe` 后升级到 `2026102801`，`schema-status.json` 检查通过。运行库 `crawler` 原来为 `2026102601`，未升级或回写。
- 临时服务曾监听 `127.0.0.1:19081`，PID `55036`，已于 2026-10-29 00:13 停止；见 `server-stop.json`。服务配置与二进制 SHA256 见 `server.json`；并发 4、整体超时 2 小时、TLS 校验关闭，资源目录独立。诊断用 C++ probe 的源码和响应保存在证据目录，未加入仓库目标，临时本机构建注册已移除。
- 正式入口请求 `{"source":"migu","limit":0,"force":true}`，全量批次 `batch_1793200870850_0` 精确覆盖 13,119 条来源，包括已有总数的 20 条，无重复或遗漏。首轮成功 12,789，失败 330（328 次未解析到有效实体、2 次请求失败），该历史批次如实保留 `Failed`。
- 通过同一入口按显式 ID 创建 `batch_1793203433251_1`，330 条一次重试全部成功；最终 13,119 条来源全部刷新成功，没有仍缺声明总数的成功目标，也没有未恢复的失败目标。两轮进度、完整 item 和 `after/`、`after-retry/` 快照均已保存。
- 缺失数量：歌曲总数 13,099→0；专辑总数 13,099→0；头像 URL 1,199→0；拼音 10,800→10,800；类型未知 1,367→1,367；地区缺失 1,367→1,367。拼音分别为索引歌手 10,726、关联歌手 74；所有类型/地区缺口均为关联歌手。名称及标准化名称缺失均为 0。
- 500 条歌曲总数为 0、1,306 条专辑总数为 0，均按有效已知值保留。默认刷新 `{"limit":0}` 返回 HTTP 200、`batch_id=null`、选择 0 条；schema 状态通过，见 `final-api-receipt.json`。
- 头像质量检查发现 1,025 条来源共用同一地址；单独查看该图片确认是灰色通用人像，图像 SHA256 为 `73f123b3c7cf684ad7832c3c0a4199c560cd0bcbdc4911511df17d130620aa23`。其中 689 条原有非空 URL 被本次刷新覆盖、335 条原无 URL、1 条原来已用该默认图。`restore-existing-avatars.py` 以来源/资源主键及当前默认 URL 为条件，在事务内恢复那 689 条旧地址，未改动其他资料；见 `avatar-restoration-receipt.json`。
- 当前仍有 336 条已确认默认头像（索引 1、关联 335），真实头像仍待补充；新补的 1,199 条 URL 中，864 条为非该默认地址，未逐一下载验证图像内容。最终数据库快照和比对为 `after-avatar-restore/`、`analysis-after-avatar-restore.json`；原始全量及重试后的结果完整保留，没有重写历史证据。
- 来源身份、类型/地区、全部目录状态/计数/时间/认领字段逐行一致；其他 14 张受保护表全行指纹一致，公共名称池仍为 13,091 行，无无引用旧名称。两类目录均完成仍为 20 条，未新增目录采集或资源下载。仅为确认默认图单独保存 1 张诊断图片，未写资源下载字段或采集存储目录。
- 当前缺口代表现有资料接口本次未能提供的内容；不把拼音、类型、地区或真实头像缺失标为请求失败。后续歌曲/专辑响应可继续自然补入拼音和头像；关联歌手类型及地区维持未知，不做额外推断。

## 默认头像修复与 689 条复测（2026-10-29）

- 本轮证据目录：`D:/codex-runs/crawler-singer-profile-completion/fix689_20261029_003515/`。目标严格来自上一轮 `avatar-restoration-receipt.json` 的 689 个来源主键，再映射为平台 ID；没有用自增 ID 分界或名称判断来源。
- 修复前保存 `before/` 完整数据快照。正式路由按显式 ID、`force=true,limit=0` 分成 500 和 189 两批：`batch_1793205875244_0`、`batch_1793205936576_1`。两批均 Completed，689 条全部成功，实际没有发生追加重试。
- `verification-689.json` 验证：689 条头像 URL 全部保留，无空值或默认图降级；头像资源整表指纹一致。声明总数未变化且无缺失；身份、类型、地区、拼音以及目录状态/计数/时间/认领字段逐行一致，未选中的歌手来源与资源整行一致，其他 16 张表指纹一致，未下载头像。
- 全库来源仍为 13,119、公共名称仍为 13,091，历史默认头像保持 336 条。默认补缺路由返回 HTTP 200、选择 0 条，见 `final-api-receipt.json`。本轮没有为拼音或未知分类添加单字段查询。
- MSVC 构建服务和相关 target 通过；10 项离线回归通过：资料刷新、元数据归属/入库、响应复用、歌手索引/歌曲 parser、API 文档/路由/目录 smoke 和资源批次事件。新增覆盖默认图的绝对、相对及带参数地址，正常封面保护，重试成功/上限/禁用、不可重试错误、取消与超时。
- 独立库 `crawler_ownership_test_singer_profile_fix_20261029_004236` 的真实 adapter 验证通过：旧结构升级、默认头像保留、503/空实体重试、上限耗尽、401/业务拒绝/身份错配不重试、成功清除旧错误、目录隔离及无下载；同库 160 份运行期 SQL prepare 通过。详见 `fixture-results-20261029_004236.json`。
- 六个实际实现文件的 clangd 语义检查均为 0 错误，显式关闭 IDE tweak 自测；见 `clangd-results.json`。本轮只对新修改的逻辑与相关依赖验证，没有将前一轮全量结果冒充修复后全库实测。
- 临时服务 PID `51720`、端口 `19081` 已停止，见 `server-stop.json`。后续接续无需等待后台采集；提交和 PR 状态请核对上述功能分支及会话附件。
