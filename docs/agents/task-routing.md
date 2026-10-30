# 智能体任务分类与执行指南

本文帮助智能体把用户诉求映射到实际代码和验证入口。只展开当前任务及关联类别，项目架构、业务规则和接口正文仍由各自文档维护。

## 先判断任务属于哪类

- 先分清动作：只分析或审查时收集证据和结论；要求修复或新增能力时才进入实现。用一个预期与实际行为的例子确定完成目标。
- 按出错阶段分类，不只匹配关键词。“歌曲时长为空”可能发生在响应解析、数据库写入或 API 映射；沿数据流找到首次丢值的位置再确定主类。
- 多类相交时选择最接近根因的主类并补读关联类：数据库字段变更联动 API 时读“元数据入库与查询”及“API 与 DTO”；下载失败涉及 TLS 时先按“故障排查”定位，再读资源或 HTTP 实现。
- 开始实现前用简短说明交代本次分类、首查入口、联动范围和验证选择。已有明确授权继续使用；无法从本地确定且会改变业务语义的歧义再向用户澄清。

## 快速分类

| 类别 | 常见触发场景 | 具体说明 |
| --- | --- | --- |
| API 与 DTO | 路由、参数校验、响应字段、HTTP 状态码或 OpenAPI 变更 | [处理步骤](#api-与-dto) |
| 采集与解析 | 平台响应变化、分页缺漏、歌手目录、搜索、URL 采集或新增音乐源 | [处理步骤](#采集与解析) |
| 元数据入库与查询 | 字段归属、重复关系、事务、计数、catalog SQL 或 migration | [处理步骤](#元数据入库与查询) |
| 资源下载 | 临时 URL、流式下载、完整提交、资源状态或重试异常 | [处理步骤](#资源下载) |
| 任务状态与并发 | 进度、取消、超时、批次事件、认领冲突或并发限制 | [处理步骤](#任务状态与并发) |
| 配置与构建 | 环境变量、运行设置、装配、CMake、编译或链接失败 | [处理步骤](#配置与构建) |
| 故障排查 | 失败阶段不明、线上与本地表现不同，需先确定根因 | [处理步骤](#故障排查) |
| 代码审查 | 评估现有变更、设计或风险，当前未要求实施修改 | [处理步骤](#代码审查) |

文档整理按 [文档验证](validation.md#仅整理说明文档) 执行，Git 操作按 [Git 流程](git-workflow.md) 执行；新会话接续按下方 [交接要求](#新会话与交接) 执行。

## 通用执行顺序

1. 运行 `git status --short`、`git branch --show-current` 和 `git log -1 --oneline`，核对已有暂存、未暂存及未跟踪内容，保留原有工作。
2. 修改代码前读取根入口要求的构建与格式配置，以及 [架构](../architecture/README.md) 中的架构原则、依赖方向和目标层边界。
3. 用 `rg --files <相关目录>` 确认实际路径，再用 `rg -n <符号或字段> <相关目录>` 沿调用与数据流收敛。路径失效时先在该职责目录查找，不直接全仓加载。
4. 找到可复现输入或离线样本，明确首次偏离预期的层，选择局部改动；接口和字段变化追踪生产者、消费者、持久化与协议引用。
5. 按下方类别实施并执行 [验证表](validation.md#按任务选择验证)。不能运行的检查写清条件和未覆盖项，不用其他测试成功替代。
6. 交付说明实际变化、验证结果和剩余事项；需要跨会话时更新当前交接。仅文档任务采用文档检查，不运行资源采集。

## API 与 DTO

- **识别条件**：请求没有进入正确处理器、合法性校验不符、HTTP code 错误、字段缺失或接口契约变更。若字段进入 controller 前已经错误，转向产生数据的类别。
- **必读范围**：[API 说明](../api/mg/migu-api.md) 的相关路由、[OpenAPI](../api/mg/openapi-migu.yaml) 的对应 path/schema，以及 [开发规范](development.md) 的 API 和错误处理章节。
- **首查代码**：[CrawlerRoutes.cpp](../../src/api/routing/CrawlerRoutes.cpp) 定位路由，再查 [CrawlerController.cpp](../../src/api/controllers/CrawlerController.cpp)、[CrawlDto.cpp](../../src/api/dto/CrawlDto.cpp) 和 [OpenApiDocumentFactory.cpp](../../src/api/controllers/OpenApiDocumentFactory.cpp) 的相关映射。设置 DTO 另查 [CrawlerSettingsDto.h](../../include/crawler/api/dto/CrawlerSettingsDto.h)。
- **处理步骤**：用最小请求核对路由与参数转换，比较 application 输入输出和 HTTP 响应，确定是协议映射还是用例结果出错；只在职责所属层修复。
- **联动修改**：契约变化同步 DTO、序列化、运行期 OpenAPI、静态 YAML 和相关接口说明；涉及查询或状态语义时补读对应业务类，不在 controller 新增 SQL 或采集流程。
- **验证与完成标准**：按验证表选择 API 测试，修改真实路由再选本机 HTTP 测试。合法请求、非法参数、缺失对象和错误映射符合预期，实际响应与两份 OpenAPI 的受影响部分一致。

## 采集与解析

- **识别条件**：平台参数、请求、JSON 字段或分页变化；搜索、URL、歌手目录、专辑详情采集遗漏；新增音乐源或采集入口。
- **必读范围**：架构的窄 port 与 workflow 约束；元数据语义变化读 [元数据模型](../migu/metadata.md)，资源响应变化联动 [资源流程](../migu/resources.md)。
- **首查代码**：请求从 [MiguProvider.cpp](../../src/infrastructure/music/migu/provider/MiguProvider.cpp)、[MiguRequestFactory.cpp](../../src/infrastructure/music/migu/provider/MiguRequestFactory.cpp) 查起。解析入口在 [MiguNarrowParsers.cpp](../../src/infrastructure/music/migu/parser/MiguNarrowParsers.cpp)，歌曲字段实际在 [MiguSongSourceParsing.cpp](../../src/infrastructure/music/migu/parser/MiguSongSourceParsing.cpp) 与 [MiguSongFieldParsing.cpp](../../src/infrastructure/music/migu/parser/MiguSongFieldParsing.cpp)，不要猜测同名 parser `.cpp`。
- **处理步骤**：先用脱敏 fixture 区分请求失败、响应形状变化和分页判断错误。按入口追到 [UrlIngestionWorkflow.cpp](../../src/application/use_cases/UrlIngestionWorkflow.cpp)、[SearchCollectionWorkflow.cpp](../../src/application/use_cases/SearchCollectionWorkflow.cpp)、[SingerCatalogWorkflow.cpp](../../src/application/use_cases/SingerCatalogWorkflow.cpp) 或 [AlbumDetailBackfillWorkflow.cpp](../../src/application/use_cases/AlbumDetailBackfillWorkflow.cpp)，只修改对应流程。
- **联动修改**：按字段用途同步窄 parser/provider port、输入模型和下游消费者；新增平台通过真实需要的窄 port 接入，并在 [ServerApplication.cpp](../../src/infrastructure/app/ServerApplication.cpp) 装配。复用已有能力，不给兼容聚合接口或 `MusicCollectionWorkflow` 继续堆流程。
- **验证与完成标准**：按验证表选择 provider、目标 parser 和受影响采集流程的 mock 测试。正常、缺字段、空页、异常结构和分页结束等受影响分支有证据；fixture 回归不依赖真实平台，新增平台需自己的 fixture 与替身。

## 元数据入库与查询

- **识别条件**：字段落错表、数据未入库、重复实体或关系、计数不符、事务回滚不完整，或需要 schema 与查询调整。
- **必读范围**：[元数据模型](../migu/metadata.md)、[MySQL 表职责](../../sql/mysql/README.md) 的目标实体、事务与迁移章节；SQLite 改动另读开发规范的 SQLite 章节。
- **首查代码**：入库沿 [MetadataBatchBuilder.cpp](../../src/application/use_cases/MetadataBatchBuilder.cpp) → [MetadataPersistenceService.cpp](../../src/application/use_cases/MetadataPersistenceService.cpp) / [MetadataPersistenceBatch.cpp](../../src/application/use_cases/MetadataPersistenceBatch.cpp) → [MySqlMetadataBatchWriter.cpp](../../src/infrastructure/music/persistence/mysql/MySqlMetadataBatchWriter.cpp) 查找。查询从 [MusicCatalogQueryService.cpp](../../src/application/use_cases/MusicCatalogQueryService.cpp) 与 [MySqlMusicCatalogQueryRepository.cpp](../../src/infrastructure/music/persistence/mysql/MySqlMusicCatalogQueryRepository.cpp) 查起。
- **处理步骤**：先确定字段属于公共实体、平台来源还是关系，逐段比对 parser 输入、batch 合并、事务 writer、表级 SQL 和查询映射。数据库值正确而响应错误时联动 API 类；样本本来缺字段时联动采集解析类。
- **联动修改**：writer 的实体与关系 helper、[表级 SQL](../../sql/mysql/)、绑定顺序和查询消费者一起检查；schema 变更新增 [migration](../../sql/mysql/migrations/)，同步结构快照、[schema 检查](../../sql/mysql/schema/schema_status.sql)、查询和必要契约。已执行 migration 不改内容，删列与 C++ 绑定及依赖查询不拆断交付。
- **验证与完成标准**：先选 parser/`MetadataOwnershipTest`，涉及 SQL 再按独立 MySQL 条件选对应集成测试。受影响身份键、幂等、关系、事实计数与同步计数、失败回滚、历史结构升级及 SQL prepare 有针对性验证；不在运行音乐库中试验。

## 资源下载

- **识别条件**：资源获取 URL 失败、音频或歌词不完整、重复下载、临时 URL 过期、状态回写或重试行为异常。
- **必读范围**：[资源流程](../migu/resources.md) 的受影响阶段、状态码与重试规则；改事件读 [批次事件规范](../observability/resource-batch-events.md)，改 SQL 或认领同时读元数据类。
- **首查代码**：正式异步流程是 [AsyncSongResourceDownloadWorkflow.cpp](../../src/application/use_cases/AsyncSongResourceDownloadWorkflow.cpp)，同步兼容流程在 [SongResourceDownloadWorkflow.cpp](../../src/application/use_cases/SongResourceDownloadWorkflow.cpp)。按失败阶段再定位 [MiguAsyncSongResourceProvider.cpp](../../src/infrastructure/music/migu/provider/MiguAsyncSongResourceProvider.cpp)、[MiguSongResourceParsing.cpp](../../src/infrastructure/music/migu/parser/MiguSongResourceParsing.cpp)、[FileStorage.cpp](../../src/infrastructure/storage/filesystem/FileStorage.cpp) 和 [MySqlSongResourceQueue.cpp](../../src/infrastructure/music/persistence/mysql/MySqlSongResourceQueue.cpp)。
- **封面专项入口**：歌手/专辑图片从 [MetadataCoverService.cpp](../../src/application/use_cases/MetadataCoverService.cpp) → [MetadataCoverDownloadWorkflow.cpp](../../src/application/use_cases/MetadataCoverDownloadWorkflow.cpp) → [封面队列](../../src/infrastructure/music/persistence/mysql/MySqlMetadataCoverQueue.cpp) 与 [保存器](../../src/infrastructure/storage/filesystem/MetadataCoverStore.cpp) 查起，协议在 [MetadataCoverController.cpp](../../src/api/controllers/MetadataCoverController.cpp)；读取[本地封面流程](../migu/resources.md#歌手头像与专辑封面本地下载)，不套用歌曲 listen/token 流程。
- **封面云端入口**：获准执行后仍由 [MetadataCoverCloudWorkflow.cpp](../../src/application/use_cases/MetadataCoverCloudWorkflow.cpp) 在本地成功后编排单端；目标装配见 [ResourceTransferFactory.cpp](../../src/infrastructure/app/ResourceTransferFactory.cpp)，内部复用 `CloudFileStoreComposition`。读[云端专项](../roadmap/metadata-cover-cloud-storage.md)及[配置区别](../configuration.md#存储模式与封面云端)。V2902 历史 skip 已经 `ResourceTransferPreflight` / `MySqlResourceTransferAccess` 接入歌曲和封面，正式认领 SQL 另有维护闸门；库内持久三路、独立来源、本地及双端文件策略和版本确认已实施，显式 parallel 已接目标身份/后台调度/配置、旧 process 兼容投影与独立 producer，仍需真实部署和规模验收，见[方案进度](../roadmap/resource-cloud-receipts.md)和[维护工具](../operations/resource-cloud-import.md)，库内执行与真实网盘服务要区分，见[持久任务与三路传输](../operations/resource-transfer-tasks.md)。
- **处理步骤**：按认领、换取 URL、解析、下载、完整提交、状态回写逐段核对，保留稳定错误分类。传输失败再定位 HTTP client；参数或解析错误不通过加重试掩盖。
- **联动修改**：同步和异步流程、path planner、blob store、关联歌词去重、事件及各自资源表 SQL 按实际影响同步检查；元数据继续只存图片 URL，歌曲主装配不因封面云端开关而接回 uploader。
- **验证与完成标准**：按验证表选资源 workflow；流式或存储变更加本机 I/O 测试。完整对象提交后才写成功，失败清理本次 staging，重试重新获取临时音频 URL，永久跳过不会被普通认领、retry 或 reset 拉回。

## 任务状态与并发

- **识别条件**：进度卡住、取消后继续认领、超时无效、批次事件缺失、内存和数据库状态不一致，或并发限制失效。
- **必读范围**：开发规范的并发和错误处理章节；涉及资源状态读资源类，涉及恢复或认领 SQL 读元数据类；事件变更读批次事件规范。
- **首查代码**：[BatchTaskManager.cpp](../../src/application/use_cases/BatchTaskManager.cpp) 的 `start/cancel/progress`、[TaskRunner.h](../../include/crawler/application/use_cases/TaskRunner.h)、[CrawlerBatchQueryService.cpp](../../src/application/use_cases/CrawlerBatchQueryService.cpp) 与 [MySqlBatchTaskRepository.cpp](../../src/infrastructure/music/persistence/mysql/MySqlBatchTaskRepository.cpp)。设置生效问题联动 [CrawlerRuntimeSettings.h](../../include/crawler/application/model/CrawlerRuntimeSettings.h) 及 [RuntimeSettingsSnapshotFactory.cpp](../../src/infrastructure/app/RuntimeSettingsSnapshotFactory.cpp)。
- **处理步骤**：先区分任务仍执行、执行结束但快照未更新、持久化失败和只读查询映射错误。围绕同一批次追踪取消标志、计数、事件和队列状态；修改锁或等待前确认所在线程及共享所有者。
- **联动修改**：内存快照、批次/明细持久化、恢复认领 SQL、查询 DTO 与事件按影响联动。执行器只执行任务，状态留在 application；运行设置不引入基础设施只读字段。
- **验证与完成标准**：按验证表选择异步、批次事件、认领或恢复检查。复现并验证本次涉及的取消、超时、重复认领或终态一致性；已有 target 未覆盖具体分支时补针对性测试，不以 `AsyncTaskTest` 单独通过代替所有任务业务回归。

## 配置与构建

- **识别条件**：编译/链接失败、target 找不到、头文件路径错误、环境配置不生效、运行设置无法热更新或默认装配变化。
- **必读范围**：[CMakeLists.txt](../../CMakeLists.txt)、[CMakePresets.json](../../CMakePresets.json)、[.clangd](../../.clangd) 和开发规范的 C++20 规则；配置读取与生效见[配置说明](../configuration.md)，初次运行见[快速启动](../getting-started.md)，新增目录或改依赖读架构相关边界。
- **首查代码**：启动配置从 [main.cpp](../../src/main.cpp)、[ServerApplication.h](../../include/crawler/infrastructure/app/ServerApplication.h)、[ServerApplication.cpp](../../src/infrastructure/app/ServerApplication.cpp) 查起。存储选型定位 [PersistenceAssembly.cpp](../../src/infrastructure/app/PersistenceAssembly.cpp)，出站请求选项定位 [HttpClientOptionsFactory.cpp](../../src/infrastructure/app/HttpClientOptionsFactory.cpp)，热更新定位运行设置模型与快照 factory。
- **处理步骤**：构建问题从第一条有效错误确认工具链、依赖、include 或 target 注册问题；配置问题沿环境读取、Config 默认值、装配和运行快照追踪，区分启动参数、可热更新策略及只读技术快照。
- **联动修改**：新增源文件/测试检查 target 可编译及 CTest 注册，保留 C++20 与标准扩展关闭；设置变化同步读取、默认值、校验、DTO、快照和实际消费者，不为解决局部问题改全局工具链或输出私有配置。
- **验证与完成标准**：CMake 变化重新 configure 并构建受影响 target；设置变化选 API/相关消费者测试，头文件变化检查包含它的实现。编译、实际生效和回归测试分别报告；构建 `demo` 不等于需要启动服务和初始化数据库。

## 故障排查

- **识别条件**：用户只给出失败、卡住、错误响应或环境差异，尚不能确定所属层。
- **必读范围**：先读用户现象和当前环境，按最早失败阶段选上面类别；配置来源见[配置说明](../configuration.md)，停止、stale、迁移失败或备份恢复见[恢复指南](../operations/service-lifecycle.md)，本机上传核对或 TLS 背景见[资源运行说明](../operations/resource-storage.md)。
- **首查代码**：从请求路由、用例阶段、结构化错误或脱敏批次事件定位；传输问题再到 [AsioHttpClient.cpp](../../src/infrastructure/http/client/AsioHttpClient.cpp)、[AsioHttpUrl.cpp](../../src/infrastructure/http/client/AsioHttpUrl.cpp)、[AsioHttpRequestBuilder.cpp](../../src/infrastructure/http/client/AsioHttpRequestBuilder.cpp) 和 [AsioHttpResponseParser.cpp](../../src/infrastructure/http/client/AsioHttpResponseParser.cpp) 查对应阶段，不把协议逻辑重新堆回 session。
- **处理步骤**：记录预期、实际、最小复现、输入来源和错误阶段，用同一任务或资源标识串联证据。优先离线复现；判断是数据、协议、网络、存储、状态还是配置问题后再提出局部修复。
- **联动修改**：只有用户要求修复时进入对应开发类别；记录源码与文档、当前装配与兼容分支的差异，不凭一条日志批量 reset、重爬或修改运行库。
- **验证与完成标准**：能指出根因或明确未证实的假设，并给出对应验证；修复时原复现不再失败且相关失败分支未退化。缺真实环境时报告尚未验证的部分，不把历史成功率当作当前结果。

## 代码审查

- **识别条件**：用户要求检查代码、评估方案、审查 diff 或判断是否符合现有约束，未要求直接实施修复。
- **必读范围**：被审查变更、目标层架构约束、开发规范和验证要求的 [代码审查规范](validation.md#代码审查规范)；根据被审查对象补读相应业务类别。
- **首查代码**：从明确指定文件、提交或 diff 出发，沿调用者、被调用者、字段消费者和相应测试扩展，不无关扫描全仓。
- **处理步骤**：先确认改动范围与预期，再核对成功、失败、边界和回滚路径，区分新增问题与既有风险。没有可复现证据时标明推断依据，不用个人风格替代项目规则。
- **联动修改**：只读审查不顺手改文件、暂存或提交；用户明确要求修复后，转到相应开发类别并遵循既有授权范围。
- **验证与完成标准**：结论包含可操作的问题、文件/行号、影响与依据；没有发现也说明检查范围和未验证项。实际运行、仅阅读和未运行的测试分别列明。

历史盘点任务首查 [ResourceDiscoveryWorkflow.cpp](../../src/application/use_cases/ResourceDiscoveryWorkflow.cpp)、[发现操作](../operations/resource-history-discovery.md)及 MySqlResourceDiscovery；容量任务首查 [MySqlResourceQueueCapacity.cpp](../../src/infrastructure/music/persistence/mysql/MySqlResourceQueueCapacity.cpp)与统一 enqueue，联动 producer 重试和旧批次。按验证表选择对应隔离测试。

单端采信接续首查 [ResourceHistoryCli.cpp](../../src/infrastructure/app/ResourceHistoryCli.cpp)、MySqlResourceHistoryAdoption及[采信操作](../operations/resource-history-adoption.md)，联动基线supplement和导入控制；旧writer/清理首查ResourceLegacyMigrationGuide与MySqlResourceCloudCleanup。业务启动及整个旧协议退役先读[证据门禁](../operations/resource-release-readiness.md)；旧云物理列清理先读[收缩说明](../operations/resource-schema-retirement.md)，按已获授权在克隆库逐值核验后再执行实际库，不以服务运行或未来旧协议退出窗口为前提。不得把准备自测写成实际执行。

## 新会话与交接

接续前读 [current.md](current.md)，核对工作目录、分支、HEAD、暂存、未暂存与未跟踪内容；先区分前序修改和当前任务，不能重做已完成迁移或覆盖已有工作。

需要交接时，当前状态至少写清：用户目标与任务类别、分支/基线、已有改动与本轮文件、已完成和未完成事项、实际检查命令与结果、未执行检查的条件、下一步应从哪个文件或 target 开始。已完成的文档整理不自动授权其他业务修复。

## 本指南怎样维护

- 新增任务类别必须补齐上述六项内容，并在快速分类登记；新能力没有现成测试时列出应补覆盖，不捏造 target。
- 代码入口与测试对应关系改名时，分别核对真实文件和 CMake；只更新索引指向，不在此复制完整项目架构、SQL 或接口正文。
- 现有类别足以覆盖时只修正入口和步骤，避免为每个类、每次会话新增文档。
