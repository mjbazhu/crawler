# 验证与代码审查

选择验证方案、执行测试、审查代码和交付前读取本文。命令在 crawler 模块根目录执行；根据实际改动选择检查，并区分离线回归、外部集成与历史记录。

## 测试与验证要求

代码改动后按下表选择覆盖实际改动的 target。首次配置或修改 CMake 时重新 configure；已配置环境按 target 构建。下面仅以歌曲来源 parser 为例，其他任务替换为受影响的 target 和实际实现文件：

```powershell
cmake --preset msvc-vcpkg
cmake --build --preset msvc-vcpkg-debug --target MiguSongSourceParserTest
clangd --check=src/infrastructure/music/migu/parser/MiguSongSourceParsing.cpp --compile-commands-dir=build/msvc-debug
```

补充要求：

- 修改头文件后，应至少对一个包含该头的 `.cpp` 运行 `clangd --check`。
- 修改 CMake 后必须重新 configure。
- 修改 SQLite schema、DAO、transaction 或并发逻辑后必须增加或更新测试；现有测试未全部接入 CMake 时，要同步接入或在最终回复说明手动验证方式。
- 修改 provider/parser 时应使用离线 fixture 覆盖解析逻辑，不让单元测试依赖实时网络。
- 无法运行验证时，最终回复必须说明原因、未覆盖风险和建议的下一步。

## 测试环境与执行条件

| 类型 | 执行内容 | 所需条件 |
| --- | --- | --- |
| 离线 | fixture、mock provider/repository 或内存中的协议映射 | 不调用真实音乐平台，不依赖 MySQL；仍按实际测试处理线程及临时文件 |
| 本机 I/O | 本地 HTTP、异步运行时、临时文件及事件日志检查 | 本机端口、线程和临时目录可用；不是外部平台集成测试 |
| 独立 MySQL | 建表、迁移、fixture 写入、认领、恢复及查询验证 | 已有授权和可用测试配置，确认有效配置指向独立测试库；不能使用运行音乐库 |
| 真实平台 | 真实请求、采集或资源下载，部分同时写 MySQL 和文件 | 在用户已授权范围内，具备相应测试库、平台访问条件和资源测试所需 pacmtoken；结果脱敏 |

SQL 测试配置从 [MySqlTestSupport.h](../../tests/migu/support/MySqlTestSupport.h) 的 `makeMySqlConfigFromEnv` 读取，可能加载 `.env.test` 与 `.env`。运行前核对生效来源，不输出密钥；`MySqlMetadataOwnershipTest` 另要求数据库名以 `crawler_ownership_test_` 开头。仅有环境文件不代表可以使用其中的运行数据库做集成测试。

`MySqlMetadataOwnershipTest --check-sql-only` 也会连接并初始化测试 schema，再 prepare 运行期 SQL，仍受独立测试库条件约束，不能当成纯文本检查。

## 按任务选择验证

下表是选择依据，不是每类任务一律全跑。先选覆盖实际改动的 target；接口、数据或状态跨类变更时取相关项，缺失覆盖再补有意义的测试。

| 任务或改动 | 已注册 target | 类型及选择条件 |
| --- | --- | --- |
| API/DTO 与 OpenAPI | `MiguCatalogApiSmokeTest`、`MiguApiDocumentTest`；真实路由另选 `MiguHttpRouteSmokeTest` | 前两者离线；文档测试核对实际注册/运行期 OpenAPI 双向覆盖、必需路径参数及本地引用；路由测试使用本机 `127.0.0.1:19080` 和替身服务 |
| 平台请求与解析 | `MiguProviderTest`，及 `MiguSongSourceParserTest` / `MiguSingerIndexParserTest` / `MiguAlbumSourceParserTest` / `MiguSongResourceParserTest` 中受影响者 | 离线；按解析能力选，不为 parser 变更运行真实采集 |
| 歌手目录和采集编排 | `MiguSingerIndexIngestionFlowTest`、`MiguSingerAlbumIngestionFlowTest` 中受影响者 | mock 数据源的本机流程；URL/搜索等没有对应 target 的分支需补覆盖，不能声称这些测试覆盖所有 workflow |
| 独立歌手资料刷新、默认头像及有限重试 | `SingerProfileRefreshTest`；协议变更另选 API 行 | 离线；覆盖有效零总数、未知分类、默认头像、HTTP/业务错误与身份校验、写入失败，以及重试上限、禁用、取消和超时；不请求真实平台或 MySQL |
| 元数据合并、字段归属及本地封面 | `MetadataOwnershipTest`，加受影响 parser | mock HTTP 与临时文件；SQL/迁移行为另选下一行 |
| MySQL writer、migration、关系或 catalog | `MySqlMetadataOwnershipTest`、`MiguSongRelationIntegrationTest`、`MiguCatalogQueryIntegrationTest` 中受影响者 | 独立 MySQL；写入/升级选 ownership，关系选 relation，事实计数/查询/资源治理选 catalog |
| 歌手资料刷新真实 adapter 与字段清理迁移 | `MySqlSingerProfileRefreshTest`；运行期 SQL prepare 另选 `MySqlMetadataOwnershipTest --check-sql-only` | 独立 MySQL；前者要求空库且库名以 `crawler_ownership_test_singer_profile_` 开头，使用离线平台响应；验证旧结构升级及重复迁移、资料/头像保存、重试、错误清理及目录生命周期隔离 |
| 资源 workflow 与资源解析 | `SongResourceDownloadWorkflowTest`、`AsyncSongResourceDownloadWorkflowTest`，解析变化加 `MiguSongResourceParserTest` | workflow 使用替身端口，不要求真实下载；同步与异步实现都受影响时都检查 |
| 歌手/专辑封面本地下载 | `MetadataCoverDownloadWorkflowTest`、`MetadataCoverStoreTest`、`MetadataCoverApiTest`；持久化另选 `MySqlMetadataCoverQueueTest` | 覆盖完整内容复用、同大小损坏/缺失的失败计数及旧回执保留、旧代次快照、排队/在途析构；MySQL 用 `crawler_ownership_test_*` 验证含真实 SQL 写入故障的确认边界。共享设施变化继续选对应行 |
| 歌手/专辑封面云端存储 | `MetadataCoverCloudWorkflowTest`、`CloudFileStoresTest`、`ProcessRunnerTest`；持久化另选 `MySqlMetadataCoverCloudTest` | 共用流程、百度回读、Google RC multipart/摘要、真实本机进程超时/取消；MySQL 用独立库验证云端代次、快照和别名。真实云端另需有效登录态/RC，不能把替身结果计作真实上传 |
| 迁移恢复与独立 schema CLI | `MySqlMigrationRecoveryTest`、`python scripts/resource-schema-cli-tests.py --cli <binary>`，联动 `MySqlResourceCloudSchemaTest --upgrade-from-legacy`、`SqlMigrationStatementsTest` | 恢复测试须空的 crawler_ownership_test_migration_recovery_*；覆盖组件标记滞后/缺失、时间戳幂等、V2910 已提交 DDL 恢复、部分/错误结构及未来版本拒绝。CLI 参数测试不连库；实际库迁移须用户明确授权，先备份及隔离恢复，再按[数据库迁移](../operations/resource-database-migration.md)核对数据，不以服务启动为前提 |
| 旧云物理列收缩与单端兼容回写 | `MySqlResourceCloudRetirementTest`、`MySqlSongCloudReceiptWritebackTest`，联动 schema/import、cover cloud、deletion/cleanup | 分别使用新空 `crawler_ownership_test_retirement_*` / `crawler_ownership_test_song_cloud_*` 库；覆盖四 owner 精确原值归档、未决拒绝、分表 DDL 中断续迁、重复幂等、强回执及 skip 围栏、旧 DTO 投影和事务回滚。实际库删列须用户明确授权，先同备份克隆核验全部保留字段和历史快照；当前物理收缩与未来移除旧 HTTP 契约、`resource_legacy_*` 分开，见[收缩说明](../operations/resource-schema-retirement.md)。 |
| 资源回执扩展、历史导入及策略 | `SqlMigrationStatementsTest`、`ResourceTransferPolicyTest`、`ResourceCloudImportManifestTest`；持久化选 `MySqlResourceCloudSchemaTest`、`MySqlLegacyResourceImportTest` | 前三项离线/本机文件；两项 MySQL 分别需要空的 `crawler_ownership_test_receipts_*`、`crawler_ownership_test_receipt_import_*` 库；schema 测试可带 `--upgrade-from-legacy`。导入测试另覆盖实时准入、状态重置后跳过仍生效、正式认领 SQL 闸门与单端暂停；当前 skip 已接入歌曲/封面；库内持久任务另见下一行，显式 parallel 服务已接线，真实平台仍需独立验收 |
| 持久三路、来源及本地策略 | `ResourceSnapshotSourceTest`、`MySqlResourceTransferJobsTest` | 前者离线/本机文件；后者必须使用显式空的 `crawler_ownership_test_transfer_jobs_*` 库，验证登记/回滚、四类跳过、独立认领、幂等、工具冻结与缺失旧值、切换工具不接管未决对象、版本、取消、修复、晚到超时、恢复、删除，以及 9 MiB 本地分片和云端替身三路。不能当作真实网盘、正式后台调度或生产放行验收 |
| parallel 运行、实际目标身份及 v2 | `ResourceTransferRuntimeTest`、`ResourceTargetVerifierTest`、`ResourceContextHttpTest`、`ResourceTransferApiTest`，加三项既有 API 测试 | 本机文件、TCP与协议替身；覆盖三路独立、保留配额、截止/取消、关闭宽限、发布前后身份变化、WebDAV挂载范围、Google交互OAuth实际账号/根及token刷新、固定官方GET/认证隔离、冻结策略、维护重放及接口校验。服务级 HTTP 另用空隔离库；真实云端选下面显式驱动，不能从替身推导真实成功 |
| 单端历史采信、导入控制与清理预演 | `ResourceHistoryManifestTest`、`ResourceLegacyUpgradeGuidanceTest`、`ResourceCloudCleanupPolicyTest`；隔离库 `MySqlResourceHistoryAdoptionTest`、`MySqlLegacyResourceImportTest`、`MySqlResourceBaselineSupplementTest`、`MySqlResourceCloudCleanupTest` | 分别使用空的history_adoption/receipt_import/supplement/cleanup前缀；验证固定审阅SHA/原观察/来源/目标/控制代次、旧取消态、跨平台run审计隔离、单端normal与skip、基线完整覆盖/封存/激活、捕获去重审计/共享引用/未决/别名与零远端删除。公共schema/SQL变化另测升级和prepare |
| 上线与兼容期收缩准备 | `python scripts/resource-release-tests.py`、release模板/证据检查及retirement inventory；两进程另构建 `MySqlResourceCrashProbe` 后运行 `scripts/resource-crash-rehearsal.py` | 前者标准库离线与本机fake进程；后者须新空 crawler_ownership_test_crash_* 库和全新目录，实际强杀两个仓储/本地文件fixture进程，核对未决保留/修复/旧认领拒绝，不调用真实云端或服务worker。真实三路/规模/兼容期证据另行取得；脚本通过不自动授权生产或删列 |
| 规模预演与部署检查 | 构建手动 `ResourceScaleProbe`，`python scripts/resource-scale-rehearsal.py self-test`；`python scripts/resource-deployment-tests.py` | 规模 prepare/measure 限定已核验的恢复工作副本，先封存、再独占窗口实测，保留旧数据及路径；结果不是千万行/生产 SLA。部署测试为离线与认证 RC 本机替身，实际 inventory/config/declarations 按[部署说明](../operations/resource-production-deployment.md)区分本机扫描、未知外部 writer 和操作者声明。路径摘要工具只读隔离库元数据，不访问历史文件；用户免验通过显式 scope 排除 file_restore，不填作通过 |
| 历史自动发现与队列容量 | `ResourceDiscoveryProbeTest`、`MySqlResourceDiscoveryTest`、`MySqlResourceQueueCapacityTest`，加 `ResourceTransferApiTest` | 离线stat/meta替身；MySQL分别须空的crawler_ownership_test_discovery_*、crawler_ownership_test_capacity_*。覆盖页租约/暂停取消/目标与来源变化/回滚恢复/四类导出、并发容量/整笔拒绝/重放和skip/未决占用/配置审计/producer重试；升级和SQL prepare另选schema/ownership。无真实网盘或正式数据 |
| 入队热点、批量登记及锁定计数 | `MySqlResourceTransferJobsTest`、`MySqlResourceQueueCapacityTest`、`MySqlLegacyResourceTransferTest`、`MySqlResourceProducerTasksTest` | 四个对应前缀的全新空隔离库，迁移顺序执行；覆盖 Process 批量字段/顺序/uint64、任务及后段云回执故障整组回滚、非 Process 逐项路径、嵌套 producer/legacy，以及 RR 旧快照后的当前计数、maximum+1 和 NOWAIT 精确锁冲突/回滚释放。性能另用原封存负载的 `ResourceScaleProbe` 独占完整测量；RSS须同进程终态高水位，staging须实际EOF和周期扫描，不能用单独本机自检补填 |
| 统一批量资源状态 | `ResourceTransferApiTest`、`MySqlResourceStatusTest` | 前者离线，后者显式空的 crawler_ownership_test_status_*；覆盖四类同ID、顺序/缺失、三端参考、历史skip/旧摘要、失败后确认保留、目标修订、严格策略、维护只读及零任务副作用。SQL模板变动另检查 prepare |
| 基线与生命周期 | `MySqlResourceBaselineTest`、`MySqlResourceLifecycleTest` | 分别要求空的 `crawler_ownership_test_baseline_*` / `crawler_ownership_test_lifecycle_*` 库；覆盖四类范围/排除、摘要变化、原子激活、退回维护/审计故障整笔回滚/幂等重新激活、双端别名证据、目标占用及producer登记。MySQL迁移检查按顺序执行，避免同实例迁移锁竞争；不得把锁竞争失败写作业务回归通过 |
| 私有歌词生成/再生 | `ResourceArtifactProducerTest`、`ResourceProducerWorkerTest`、`ResourceSnapshotSourceTest`，持久部分选 `MySqlResourceProducerTasksTest` | 前三项离线/本机；producer MySQL 必须空的 crawler_ownership_test_producer_*。验证独立预算、共享配额、缺key零来源、版本/摘要围栏、原子登记/子操作受理、显式历史生成保留skip、永久来源限制、失败补排/恢复与取消。缓存命中不代表整个批次永不重复读取/解码 |
| 旧治理语义与自动删除捕获 | `MySqlLegacyResourceGovernanceTest`、`MySqlResourceDeletionTest`、`ResourceLegacyUpgradeGuidanceTest`；联动 metadata ownership、cover queue/cloud、cleanup/lifecycle | 前两项分别须新空 crawler_ownership_test_legacy_governance_* / crawler_ownership_test_deletion_*。覆盖原处理器与准入、仅重置不入队、真实明细重读、skip/占用/未决保护；四owner父级联/多来源/artifact、别名和占位清理自动捕获、审计/DELETE故障整笔回滚、三端waiting_input分别取消及终态父下producer围栏。无真实资源删除或远端请求 |
| 旧接口并行兼容与音频关联 | `LegacyResourceTransferApiTest`、`ResourceRelatedSourceTest`、`ResourceTokenRefreshTest`；持久部分选 `MySqlLegacyResourceTransferTest`、`MySqlResourceRelatedTest` | 前三项离线；后两项分别要求空的 crawler_ownership_test_legacy_transfer_* / crawler_ownership_test_related_*。覆盖同步等待/异步受理、分片原子映射、重复请求、并发上限、固定平台、历史弱证据、截止与两层取消；关联发现/确认/修复同事务，token零队列/周期/强制刷新及等待取消。均无真实源站和网盘请求 |
| 云端文件快照上传及只读核对 | `HttpStreamUploadTest`、`CloudResourceTransferTest`，持久闭环另选 `MySqlResourceTransferJobsTest` | 前两项本机 TCP/文件及 HTTP/进程替身，覆盖 16 MiB 按需发送、64 KiB 缓冲、返回时释放 reader、限速/主动取消、9 MiB 双端文件策略、摘要等级、响应未知、超限回读、旧 RC 配置复用、百度选择 rclone、双 rclone 配置/取消隔离及同目标重复声明拒绝；持久用例网盘 I/O 仍为替身。旧封面回归另选 `CloudFileStoresTest` / `MySqlMetadataCoverCloudTest`，不能把本机结果写成真实云端验收 |
| 维护入口、历史跳过接线及目标工厂 | `ResourceTransferAdmissionTest`、`AsyncSongResourceDownloadWorkflowTest`、`CloudFileStoresTest`，加受影响歌曲/封面 workflow | 离线/本机文件；覆盖标准 503、只读/停止可用、依赖故障关闭、skip 先于内容 I/O、local 暂停不认领/token 刷新、工厂双端独立与单端配置错误、延迟本地根和完整提交/清理。适配器屏障只证明双端调用独立，不代表三路已登记、云端独立取得来源或可恢复 |
| 流式传输、完整文件提交或异步运行时 | `AsyncHttpStreamTest`、`AsyncFileBlobStoreTest`、`SongResourceDownloadStorageTest`、`AsyncTaskTest` 中受影响者 | 本机 I/O；分别覆盖回环 HTTP、文件存储或异步调度，不能互相替代 |
| 批次事件、认领与恢复 | `CrawlerServiceResourceBatchEventTest`；持久化认领/恢复选 `MiguBackfillClaimIntegrationTest` / `MiguMetadataRecoveryIntegrationTest` | 事件测试使用替身及本地日志；后两者使用独立 MySQL 并写 fixture |
| 配置、构建与运行设置 | 构建 `demo` 或受影响 target；设置/协议变化选 API 行及实际消费者测试 | 构建不等于启动；启动可能初始化数据库，需按任务范围判断是否执行 |
| 故障排查或代码审查 | 选上面与证据所指阶段相符的项 | 只读任务不默认运行会改库或采集的测试；准确报告实际执行范围 |

target 的来源是 [CMakeLists.txt](../../CMakeLists.txt)。`tests/legacy/` 中存在源码不代表当前已注册；新增或改名后先核对 CMake，不能用不存在的测试名交付。

全量路由核对按 HTTP 方法和路径进行，补池通用参数展开四种有效类型，其他占位参数按模板比较；统一 CORS OPTIONS 不算业务操作。`MiguApiDocumentTest` 不自动读取静态 YAML 或 Markdown，也不代表所有字段语义均已核对。可在执行该测试前设置 `CRAWLER_TEST_OPENAPI_OUTPUT`、`CRAWLER_TEST_ROUTES_OUTPUT` 为仓库外的绝对文件路径，分别导出运行期文档和原始注册清单，再与静态/API 清单比较。新增业务路由仍须同步两份 OpenAPI 与 API 文档；本轮覆盖证据见[路线记录](../roadmap/README.md#m1-现状校准)。

已配置环境中，按 target 构建并精确选择测试，例如：

```powershell
cmake --build --preset msvc-vcpkg-debug --target MiguSongSourceParserTest
ctest --test-dir build/msvc-debug -R '^MiguSongSourceParserTest$' --output-on-failure
```

该例只演示离线 parser 测试的选择方式。不要为一次局部变更不加筛选地运行全部 CTest；当前注册项也包含真实平台和 MySQL 测试。CMake 变化先重新 configure，工具链未就绪时记录缺失条件。

结果至少记录命令、退出结果和验证范围；`SKIP`、环境不足、只构建成功、只读过测试源码都不能写成业务回归通过。

## 仅整理说明文档

- 仅调整根 `AGENTS.md`、文档导航、说明文档的组织结构和引用，且未改变 API 契约、SQL、构建配置或生产代码时，检查迁移内容覆盖、Markdown 链接、UTF-8 无 BOM、格式和 Git diff。
- 此类整理不要求编译整个 C++ 模块或运行依赖外部平台、MySQL 的测试；如果同时修改契约或代码，仍执行对应检查。
- 核对原规范中的禁止事项、状态不变量和操作前提是否保留，不把历史实测写成当前验证结果。

涉及启动、配置和恢复操作的说明时，逐项追踪入口默认值与消费者，检查 PowerShell/JSON 示例及链接；按[快速启动](../getting-started.md)或[恢复指南](../operations/service-lifecycle.md)选择实际演练。只读查询、设置更新、迁移初始化、任务恢复、备份还原是不同范围；有条件执行的结果单独记录，未演练场景保留限制。文档改动不自动授权对运行音乐库做迁移或恢复。

## 资源链路专项回归

- 修改资源下载链路后至少运行：

```powershell
.\.vscode\build-msvc-target-debug.cmd SongResourceDownloadWorkflowTest
.\build\msvc-debug\Debug\SongResourceDownloadWorkflowTest.exe
.\.vscode\build-msvc-target-debug.cmd AsyncSongResourceDownloadWorkflowTest
.\build\msvc-debug\Debug\AsyncSongResourceDownloadWorkflowTest.exe
```

- 修改异步 HTTP 流式下载、blob store 或 application async 基础设施后继续运行：

```powershell
.\.vscode\build-msvc-target-debug.cmd AsyncTaskTest
.\.vscode\build-msvc-target-debug.cmd AsyncHttpStreamTest
.\.vscode\build-msvc-target-debug.cmd AsyncFileBlobStoreTest
```

- 修改 API、DTO 或 API 契约文档后运行：

```powershell
.\.vscode\build-msvc-target-debug.cmd MiguCatalogApiSmokeTest
.\.vscode\build-msvc-target-debug.cmd MiguHttpRouteSmokeTest
.\.vscode\build-msvc-target-debug.cmd MiguApiDocumentTest
```

- 真实资源下载测试只在用户明确提供可用 MySQL 和 pacmtoken 后执行，且输出必须脱敏。推荐命令模式：

```powershell
$env:CRAWLER_OUTBOUND_VERIFY_TLS='false'
$env:CRAWLER_TEST_MIGU_ASYNC_RESOURCE_FORMAT='PQ'
$env:CRAWLER_TEST_MIGU_ASYNC_RESOURCE_LIMIT='30'
$env:CRAWLER_TEST_MIGU_ASYNC_RESOURCE_CONCURRENCY='5'
.\build\msvc-debug\Debug\MiguAsyncSongResourceBatchIngestionTest.exe
```

构建脚本位于本机 `.vscode/`，该目录由 `.gitignore` 忽略。新环境缺少脚本时，可使用 `cmake --build --preset msvc-vcpkg-debug --target <Target>` 构建对应 target，再运行 `build/msvc-debug/Debug/<Target>.exe`。涉及真实 MySQL 和平台的测试仍受上面的显式条件约束。

## 历史结果

历史验收按项目主题记录日期、交付版本、环境、结果和证据位置；[当前交接](current.md) 只引用相关记录。歌手资料的全量刷新及修复回归见 [歌手资料完善与验收记录](../migu/singer-profile-validation.md)，本机资源历史见 [运行操作](../operations/resource-storage.md)。验证命令存在不代表本次已经执行。

## 代码审查规范

审查或修改前先确认：

- 当前工作目录、已有规范文件、CMake、clang-format、clangd 配置。
- 如果当前目录不是 Git 仓库根目录，应向上查找；若仍不可用，在回复中说明无法获得 Git 状态。
- 用户已有改动不得回滚；无关改动忽略，相关改动先理解再协作。
- 本次改动是否仍然只服务 crawler 模块职责。
- include 方向是否违反分层依赖。
- 是否新增了无必要抽象、重复逻辑、跨层直接依赖、大而全工具类或无法测试的全局状态。

审查重点：

- 设计是否符合 api/application/domain/infrastructure 边界。
- 资源管理是否完整 RAII，是否存在泄漏、悬垂引用、共享所有权滥用。
- SQLite/MySQL/Asio/文件系统错误是否完整处理。
- 并发路径是否存在竞态、死锁、长时间持锁或线程不安全对象共享。
- 测试是否覆盖核心成功路径、失败路径、边界条件和回滚语义。

审查输出：

- 优先列问题和风险，带文件/行号。
- 没有问题时明确说明剩余风险或未验证项。
- 风格争议以 `.clang-format` 和 [开发规范](development.md) 为准；没有规则时跟随局部既有风格。
